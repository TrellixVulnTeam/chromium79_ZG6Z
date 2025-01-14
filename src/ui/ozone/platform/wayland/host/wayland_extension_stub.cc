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

#include "ui/ozone/platform/wayland/host/wayland_extension.h"

#include "base/logging.h"
#include "base/macros.h"
#include "ui/ozone/platform/wayland/host/shell_popup_wrapper.h"
#include "ui/ozone/platform/wayland/host/shell_surface_wrapper.h"

namespace ui {

namespace {

class StubWaylandExtension : public WaylandExtension {
 public:
  StubWaylandExtension() = default;
  ~StubWaylandExtension() override = default;

  // WaylandExtension:
  bool Bind(wl_registry* registry,
            uint32_t name,
            const char* interface,
            uint32_t version) override {
    return false;
  }

  bool HasShellObject() override { return false; }

  std::unique_ptr<ShellSurfaceWrapper> CreateShellSurface(
      WaylandWindow* wayland_window) override {
    return nullptr;
  }

  std::unique_ptr<ShellPopupWrapper> CreateShellPopup(
      WaylandWindow* wayland_window) override {
    return nullptr;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(StubWaylandExtension);
};

}  // namespace

std::unique_ptr<WaylandExtension> CreateWaylandExtension() {
  return std::make_unique<StubWaylandExtension>();
}

}  // namespace ui
