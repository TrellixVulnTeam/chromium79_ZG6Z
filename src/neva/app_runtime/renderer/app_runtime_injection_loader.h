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

#ifndef NEVA_APP_RUNTIME_RENDERER_APP_RUNTIME_INJECTION_LOADER_H_
#define NEVA_APP_RUNTIME_RENDERER_APP_RUNTIME_INJECTION_LOADER_H_

#include <list>
#include <set>
#include <string>

#include "base/macros.h"
#include "neva/injection/common/public/renderer/injection_install.h"

namespace neva_app_runtime {

class InjectionLoader {
 public:
  explicit InjectionLoader();
  ~InjectionLoader();

  void Add(const std::string& name);
  void Load(blink::WebLocalFrame* frame);
  void Unload();

 private:
  std::set<std::string> injections_;

  struct InjectionContext {
    injections::InstallAPI api;
    blink::WebLocalFrame* frame = nullptr;
  };

  std::list<InjectionContext> injections_contexts_;
  DISALLOW_COPY_AND_ASSIGN(InjectionLoader);
};

}  // namespace neva_app_runtime

#endif  // NEVA_APP_RUNTIME_RENDERER_APP_RUNTIME_INJECTION_LOADER_H_
