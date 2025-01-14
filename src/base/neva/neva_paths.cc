// Copyright 2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "base/neva/neva_paths.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/neva/base_switches.h"

namespace base {

bool PathProviderNeva(int key, base::FilePath* result) {
  switch (key) {
    case DIR_NEVA_CERTIFICATES: {
      FilePath certificates_path =
          base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
              switches::kNevaCertificatesPath);
      if (!certificates_path.empty()) {
        *result = certificates_path;
        return true;
      }
      return false;
    } break;
    default:
      return false;
  }
  return false;
}

}  // namespace base
