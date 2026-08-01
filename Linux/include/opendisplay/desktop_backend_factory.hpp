#pragma once

#include "opendisplay/desktop_backend.hpp"

#include <memory>
#include <string_view>

namespace od {

CompositorKind detectCompositor(CompositorKind requested,
                                std::string_view currentDesktop,
                                std::string_view hyprlandSignature,
                                std::string_view kdeSession);
std::unique_ptr<DesktopBackend> makeDesktopBackend(CompositorKind requested);

}  // namespace od
