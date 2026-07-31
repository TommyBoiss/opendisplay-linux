#pragma once

#include <spa/pod/builder.h>

namespace od {

/// Builds the raw-video capabilities accepted by the capture and encoder path.
/// The returned pod is owned by the caller-provided builder storage.
const spa_pod* buildPipeWireFormatOffer(spa_pod_builder& builder, int width, int height,
                                        int fps);

}  // namespace od
