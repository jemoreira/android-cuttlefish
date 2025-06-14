/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/run_cvd/server_loop_impl.h"

#include <sstream>
#include <string>

#include <android-base/file.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/command_util/runner/defs.h"
#include "cuttlefish/host/libs/command_util/runner/run_cvd.pb.h"
#include "cuttlefish/host/libs/command_util/snapshot_utils.h"
#include "cuttlefish/host/libs/command_util/util.h"
#include "cuttlefish/host/libs/config/ap_boot_flow.h"
#include "cuttlefish/host/libs/vm_manager/crosvm_manager.h"
#include "cuttlefish/host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {
namespace run_cvd_impl {

std::unordered_map<std::string, std::string>
ServerLoopImpl::InitializeVmToControlSockPath(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return std::unordered_map<std::string, std::string>{
      // TODO(kwstephenkim): add the following two lines to support QEMU
      // {ToString(VmmMode::kQemu),
      // instance.PerInstanceInternalUdsPath("qemu_monitor.sock")},
      {ToString(VmmMode::kCrosvm), instance.CrosvmSocketPath()},
      {cuttlefish::kApName, instance.OpenwrtCrosvmSocketPath()},
  };
}

static std::string SubtoolPath(const std::string& subtool_name) {
  auto my_own_dir = android::base::GetExecutableDirectory();
  std::stringstream subtool_path_stream;
  subtool_path_stream << my_own_dir << "/" << subtool_name;
  auto subtool_path = subtool_path_stream.str();
  if (my_own_dir.empty() || !FileExists(subtool_path)) {
    return HostBinaryPath(subtool_name);
  }
  return subtool_path;
}

static std::string GetSocketPath(
    const std::string name,
    std::unordered_map<std::string, std::string>& vm_name_to_control_sock_) {
  if (!Contains(vm_name_to_control_sock_, name)) {
    return "";
  }
  return vm_name_to_control_sock_.at(name);
}

static Result<void> SuspendCrosvm(const std::string& vm_sock_path) {
  const auto crosvm_bin_path = SubtoolPath("crosvm");
  std::vector<std::string> command_args{crosvm_bin_path, "suspend",
                                        vm_sock_path, "--full"};
  auto infop = CF_EXPECT(Execute(command_args, SubprocessOptions(), WEXITED));
  CF_EXPECT_EQ(infop.si_code, CLD_EXITED);
  CF_EXPECTF(infop.si_status == 0, "crosvm suspend returns non zero code {}",
             infop.si_status);
  return {};
}

static Result<void> ResumeCrosvm(const std::string& vm_sock_path) {
  const auto crosvm_bin_path = SubtoolPath("crosvm");
  std::vector<std::string> command_args{crosvm_bin_path, "resume", vm_sock_path,
                                        "--full"};
  auto infop = CF_EXPECT(Execute(command_args, SubprocessOptions(), WEXITED));
  CF_EXPECT_EQ(infop.si_code, CLD_EXITED);
  CF_EXPECTF(infop.si_status == 0, "crosvm resume returns non zero code {}",
             infop.si_status);
  return {};
}

Result<void> ServerLoopImpl::SuspendGuest() {
  // If openwrt is running in crosvm, suspend it.
  const auto ap_vm_name = config_.ap_vm_manager();
  if (instance_.ap_boot_flow() != APBootFlow::None &&
      ap_vm_name == cuttlefish::kApName) {
    const auto openwrt_sock =
        GetSocketPath(ap_vm_name, vm_name_to_control_sock_);
    if (openwrt_sock.empty()) {
      return CF_ERR("The vm_manager " + ap_vm_name + " is not supported yet");
    }
    CF_EXPECT(SuspendCrosvm(openwrt_sock),
              "failed to suspend openwrt crosvm instance.");
  }
  const auto main_vmm = config_.vm_manager();
  if (main_vmm == VmmMode::kCrosvm) {
    const auto& vm_sock =
        GetSocketPath(ToString(main_vmm), vm_name_to_control_sock_);
    if (vm_sock.empty()) {
      return CF_ERR("The vm_manager " << main_vmm << " is not supported yet");
    }
    return SuspendCrosvm(vm_sock);
  } else {
    return CF_ERR("The vm_manager " << main_vmm << " is not supported yet");
  }
}

Result<void> ServerLoopImpl::ResumeGuest() {
  // If openwrt is running in crosvm, resume it.
  const auto ap_vm_name = config_.ap_vm_manager();
  if (instance_.ap_boot_flow() != APBootFlow::None &&
      ap_vm_name == cuttlefish::kApName) {
    const auto& openwrt_sock =
        GetSocketPath(ap_vm_name, vm_name_to_control_sock_);
    if (openwrt_sock.empty()) {
      return CF_ERR("The vm_manager " + ap_vm_name + " is not supported yet");
    }
    CF_EXPECT(ResumeCrosvm(openwrt_sock),
              "failed to resume openwrt crosvm instance.");
  }
  const auto main_vmm = config_.vm_manager();
  if (main_vmm == VmmMode::kCrosvm) {
    const auto& vm_sock =
        GetSocketPath(ToString(main_vmm), vm_name_to_control_sock_);
    if (vm_sock.empty()) {
      return CF_ERR("The vm_manager " << main_vmm << " is not supported yet");
    }
    return ResumeCrosvm(vm_sock);
  } else {
    return CF_ERR("The vm_manager " << main_vmm << " is not supported yet");
  }
}

static Result<void> RunAdbShellCommand(
    const CuttlefishConfig::InstanceSpecific& ins,
    const std::vector<std::string>& command_args) {
  // Make sure device is connected, otherwise the following `adb -s SERIAL
  // wait-for-device shell ...` would get stuck.
  Command connect_cmd(SubtoolPath("adb"));
  // Avoid the adb server being started in the runtime directory and looking
  // like a process that is still using the directory.
  connect_cmd.SetWorkingDirectory("/");
  connect_cmd.AddParameter("connect");
  connect_cmd.AddParameter(ins.adb_ip_and_port());
  CF_EXPECT_EQ(connect_cmd.Start().Wait(), 0);

  // Run the shell commands.
  Command shell_cmd(SubtoolPath("adb"));
  shell_cmd.AddParameter("-s").AddParameter(ins.adb_ip_and_port());
  shell_cmd.AddParameter("shell");
  for (const auto& argument : command_args) {
    shell_cmd.AddParameter(argument);
  }
  CF_EXPECT_EQ(shell_cmd.Start().Wait(), 0);
  return {};
}

Result<void> ServerLoopImpl::HandleSuspend(ProcessMonitor& process_monitor) {
  // right order: guest -> host
  LOG(DEBUG) << "Suspending the guest..";
  CF_EXPECT(
      RunAdbShellCommand(instance_, {"/vendor/bin/snapshot_hook_pre_suspend"}));
  CF_EXPECT(SuspendGuest());
  LOG(DEBUG) << "The guest is suspended.";
  CF_EXPECT(process_monitor.SuspendMonitoredProcesses(),
            "Failed to suspend host processes.");
  LOG(DEBUG) << "The host processes are suspended.";
  return {};
}

Result<void> ServerLoopImpl::HandleResume(ProcessMonitor& process_monitor) {
  // right order: host -> guest
  CF_EXPECT(process_monitor.ResumeMonitoredProcesses(),
            "Failed to resume host processes.");
  LOG(DEBUG) << "The host processes are resumed.";
  LOG(DEBUG) << "Resuming the guest..";
  CF_EXPECT(ResumeGuest());
  CF_EXPECT(
      RunAdbShellCommand(instance_, {"/vendor/bin/snapshot_hook_post_resume"}));
  LOG(DEBUG) << "The guest resumed.";
  return {};
}

Result<void> ServerLoopImpl::TakeCrosvmGuestSnapshot(
    const Json::Value& meta_json) {
  const auto snapshots_parent_dir =
      CF_EXPECT(InstanceGuestSnapshotPath(meta_json, instance_.id()));
  const auto crosvm_bin = config_.crosvm_binary();
  const std::string snapshot_guest_param =
      snapshots_parent_dir + "/" + kGuestSnapshotBase;
  // If openwrt is running in crosvm, snapshot it.
  const auto ap_vm_name = config_.ap_vm_manager();
  if (instance_.ap_boot_flow() != APBootFlow::None &&
      ap_vm_name == cuttlefish::kApName) {
    const auto& openwrt_sock =
        GetSocketPath(ap_vm_name, vm_name_to_control_sock_);
    if (openwrt_sock.empty()) {
      return CF_ERR("The vm_manager " + ap_vm_name + " is not supported yet");
    }
    std::vector<std::string> openwrt_crosvm_command_args{
        crosvm_bin, "snapshot", "take", snapshot_guest_param + "_openwrt",
        openwrt_sock};
    LOG(DEBUG) << "Running the following command to take snapshot..."
               << std::endl
               << "  ";
    for (const auto& arg : openwrt_crosvm_command_args) {
      LOG(DEBUG) << arg << " ";
    }
    CF_EXPECT(Execute(openwrt_crosvm_command_args) == 0,
              "Executing openwrt crosvm command returned -1");
    LOG(DEBUG) << "Guest snapshot for openwrt instance #" << instance_.id()
               << " should have been stored in " << snapshots_parent_dir
               << "_openwrt";
  }
  const auto control_socket_path =
      CF_EXPECT(VmControlSocket(), "Failed to find crosvm control.sock path.");
  std::vector<std::string> crosvm_command_args{crosvm_bin, "snapshot", "take",
                                               snapshot_guest_param,
                                               control_socket_path};
  LOG(DEBUG) << "Running the following command to take snapshot..." << std::endl
             << "  ";
  for (const auto& arg : crosvm_command_args) {
    LOG(DEBUG) << arg << " ";
  }
  CF_EXPECT(Execute(crosvm_command_args) == 0,
            "Executing crosvm command failed");
  LOG(DEBUG) << "Guest snapshot for instance #" << instance_.id()
             << " should have been stored in " << snapshots_parent_dir;
  return {};
}

/*
 * Parse json file at json_path, and take guest snapshot
 */
Result<void> ServerLoopImpl::TakeGuestSnapshot(VmmMode vm_manager,
                                               const std::string& json_path) {
  // common code across vm_manager
  CF_EXPECTF(FileExists(json_path), "{} must exist but does not.", json_path);
  SharedFD json_fd = SharedFD::Open(json_path, O_RDONLY);
  CF_EXPECTF(json_fd->IsOpen(), "Failed to open {}", json_path);
  std::string json_contents;
  CF_EXPECT_GE(ReadAll(json_fd, &json_contents), 0,
               std::string("Failed to read from ") + json_path);
  Json::Value meta_json = CF_EXPECTF(
      ParseJson(json_contents), "Failed to parse json: \n{}", json_contents);
  CF_EXPECTF(vm_manager == VmmMode::kCrosvm,
             "{}, which is not crosvm, is not yet supported.", vm_manager);
  CF_EXPECT(TakeCrosvmGuestSnapshot(meta_json),
            "TakeCrosvmGuestSnapshot() failed.");
  return {};
}

Result<void> ServerLoopImpl::HandleSnapshotTake(
    const run_cvd::SnapshotTake& snapshot_take) {
  CF_EXPECT(!snapshot_take.snapshot_path().empty(),
            "snapshot_path must be non-empty");
  CF_EXPECT(
      TakeGuestSnapshot(config_.vm_manager(), snapshot_take.snapshot_path()),
      "Failed to take guest snapshot");
  return {};
}

}  // namespace run_cvd_impl
}  // namespace cuttlefish
