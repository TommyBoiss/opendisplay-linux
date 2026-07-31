#include "opendisplay/pipewire_format.hpp"

#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/filter.h>

#include <array>
#include <cassert>
#include <cstdint>

namespace {

const spa_pod* fixedFormat(spa_pod_builder& builder, const spa_video_format format,
                           const std::uint32_t width, const std::uint32_t height,
                           const std::uint32_t fps) {
    const spa_rectangle size = SPA_RECTANGLE(width, height);
    const spa_fraction rate = SPA_FRACTION(fps, 1);
    return static_cast<spa_pod*>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(format),
        SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate)));
}

void acceptsKwinCompatibleAlternative() {
    std::array<std::uint8_t, 1024> offerStorage{};
    spa_pod_builder offerBuilder = SPA_POD_BUILDER_INIT(
        offerStorage.data(), static_cast<std::uint32_t>(offerStorage.size()));
    const spa_pod* offer = od::buildPipeWireFormatOffer(offerBuilder, 2420, 1668, 60);

    std::array<std::uint8_t, 512> producerStorage{};
    spa_pod_builder producerBuilder = SPA_POD_BUILDER_INIT(
        producerStorage.data(), static_cast<std::uint32_t>(producerStorage.size()));
    const spa_pod* producer = fixedFormat(producerBuilder, SPA_VIDEO_FORMAT_RGBx, 1920, 1080, 30);

    std::array<std::uint8_t, 1024> resultStorage{};
    spa_pod_builder resultBuilder = SPA_POD_BUILDER_INIT(
        resultStorage.data(), static_cast<std::uint32_t>(resultStorage.size()));
    spa_pod* result = nullptr;
    assert(spa_pod_filter(&resultBuilder, &result, producer, offer) >= 0);
    assert(result != nullptr);

    spa_video_info_raw parsed{};
    assert(spa_format_video_raw_parse(result, &parsed) >= 0);
    assert(parsed.format == SPA_VIDEO_FORMAT_RGBx);
}

void rejectsUnsupportedPlaneLayout() {
    std::array<std::uint8_t, 1024> offerStorage{};
    spa_pod_builder offerBuilder = SPA_POD_BUILDER_INIT(
        offerStorage.data(), static_cast<std::uint32_t>(offerStorage.size()));
    const spa_pod* offer = od::buildPipeWireFormatOffer(offerBuilder, 2420, 1668, 60);

    std::array<std::uint8_t, 512> producerStorage{};
    spa_pod_builder producerBuilder = SPA_POD_BUILDER_INIT(
        producerStorage.data(), static_cast<std::uint32_t>(producerStorage.size()));
    const spa_pod* producer = fixedFormat(producerBuilder, SPA_VIDEO_FORMAT_NV12, 1920, 1080, 60);

    std::array<std::uint8_t, 1024> resultStorage{};
    spa_pod_builder resultBuilder = SPA_POD_BUILDER_INIT(
        resultStorage.data(), static_cast<std::uint32_t>(resultStorage.size()));
    spa_pod* result = nullptr;
    assert(spa_pod_filter(&resultBuilder, &result, producer, offer) < 0);
}

}  // namespace

int main() {
    acceptsKwinCompatibleAlternative();
    rejectsUnsupportedPlaneLayout();
}
