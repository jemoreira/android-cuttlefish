//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/feature/feature.h"

namespace cuttlefish {

struct MonitorCommand {
  Command command;
  bool is_critical;

  MonitorCommand(Command command, bool is_critical = false)
      : command(std::move(command)), is_critical(is_critical) {}
};

class CommandSource : public virtual SetupFeature {
 public:
  virtual ~CommandSource() = default;
  virtual Result<std::vector<MonitorCommand>> Commands() = 0;
};

class StatusCheckCommandSource : public virtual CommandSource {
 public:
  virtual Result<void> WaitForAvailability() const = 0;
};

}  // namespace cuttlefish
