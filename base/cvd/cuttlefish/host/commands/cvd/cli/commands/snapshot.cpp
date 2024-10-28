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

#include "host/commands/cvd/cli/commands/snapshot.h"

#include <android-base/file.h>
#include <android-base/strings.h>

#include <sstream>
#include <string>
#include <vector>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/utils/common.h"
#include "host/commands/cvd/instances/instance_group_record.h"
#include "host/commands/cvd/cli/commands/host_tool_target.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Suspend/resume the cuttlefish device, or take snapshot of the device";

constexpr char kDetailedHelpText[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

Suspend/resume the cuttlefish device, or take snapshot of the device

usage: cvd [selector flags] suspend/resume/snapshot_take [--help]

Common:
  Selector Flags:
    --group_name=<name>       The name of the instance group
    --snapshot_path=<path>>   Directory that contains saved snapshot files

Crosvm:
  --snapshot_compat           Tells the device to be snapshot-compatible
                              The device to be created is checked if it is
                              compatible with snapshot operations

QEMU:
  No QEMU-specific arguments at the moment

)";

class CvdSnapshotCommandHandler : public CvdServerHandler {
 public:
  CvdSnapshotCommandHandler(InstanceManager& instance_manager)
      : instance_manager_{instance_manager},
        cvd_snapshot_operations_{"suspend", "resume", "snapshot_take"} {}

  Result<bool> CanHandle(const CommandRequest& request) const override {
    auto invocation = ParseInvocation(request);
    return Contains(cvd_snapshot_operations_, invocation.command);
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    auto [subcmd, subcmd_args] = ParseInvocation(request);

    std::stringstream ss;
    for (const auto& arg : subcmd_args) {
      ss << arg << " ";
    }
    LOG(DEBUG) << "Calling new handler with " << subcmd << ": " << ss.str();

    // may modify subcmd_args by consuming in parsing
    Command command =
        CF_EXPECT(GenerateCommand(request, subcmd, subcmd_args, request.Env()));

    siginfo_t infop;
    command.Start().Wait(&infop, WEXITED);

    return ResponseFromSiginfo(infop);
  }

  cvd_common::Args CmdList() const override {
    return cvd_common::Args(cvd_snapshot_operations_.begin(),
                            cvd_snapshot_operations_.end());
  }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

 private:
  Result<Command> GenerateCommand(const CommandRequest& request,
                                  const std::string& subcmd,
                                  cvd_common::Args& subcmd_args,
                                  cvd_common::Envs envs) {
    // create a string that is comma-separated instance IDs
    auto instance_group =
        CF_EXPECT(instance_manager_.SelectGroup(request.Selectors(), envs));

    const auto& home = instance_group.HomeDir();
    const auto& android_host_out = instance_group.HostArtifactsPath();
    auto cvd_snapshot_bin_path =
        android_host_out + "/bin/" + CF_EXPECT(GetBin(android_host_out));
    const std::string& snapshot_util_cmd = subcmd;
    cvd_common::Args cvd_snapshot_args{"--subcmd=" + snapshot_util_cmd};
    cvd_snapshot_args.insert(cvd_snapshot_args.end(), subcmd_args.begin(),
                             subcmd_args.end());
    // This helps snapshot_util find CuttlefishConfig and figure out
    // the instance ids
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;

    std::cerr << "HOME=" << home << " " << kAndroidHostOut << "="
              << android_host_out << " " << kAndroidSoongHostOut << "="
              << android_host_out << " " << cvd_snapshot_bin_path << " ";
    for (const auto& arg : cvd_snapshot_args) {
      std::cerr << arg << " ";
    }

    ConstructCommandParam construct_cmd_param{
        .bin_path = cvd_snapshot_bin_path,
        .home = home,
        .args = cvd_snapshot_args,
        .envs = envs,
        .working_dir = CurrentDirectory(),
        .command_name = android::base::Basename(cvd_snapshot_bin_path),
    };
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    return command;
  }

  Result<std::string> GetBin(const std::string& host_artifacts_path) const {
    return CF_EXPECT(HostToolTarget(host_artifacts_path).GetSnapshotBinName());
  }

  InstanceManager& instance_manager_;
  std::vector<std::string> cvd_snapshot_operations_;
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdSnapshotCommandHandler(
    InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdSnapshotCommandHandler(instance_manager));
}

}  // namespace cuttlefish