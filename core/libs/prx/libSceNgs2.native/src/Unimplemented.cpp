#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "prx/libc/include/General.hpp"
#include "prx/libSceNgs2.native/include/Ngs2Types.hpp"

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceNgs2GeomApply(const Ngs2GeomListenerWork* listener, const Ngs2GeomSourceParam* source, Ngs2GeomAttribute* out_attrib, std::uint32_t flags) {
    if (listener == nullptr || source == nullptr) APS5_INVALID_ARG_EX;
    if (out_attrib == nullptr) return SCE_NGS2_ERROR_INVALID_OUT_ADDRESS;
    (void)flags;
    std::memset(out_attrib, 0, sizeof(*out_attrib));
    out_attrib->pitch_ratio = 1.0f;
    out_attrib->a3d_attrib.position = source->position;
    out_attrib->a3d_attrib.volume = std::max(source->min_level, source->max_level);
    const std::uint32_t channels = std::min(source->matrix_format == 0u ? 2u : source->matrix_format, 8u);
    const float level = source->max_level > 0.0f ? source->max_level : 1.0f;
    for (std::uint32_t channel = 0; channel < channels; channel++) {
        out_attrib->level[static_cast<std::size_t>(channel) * 8 + channel] = level;
    }
    return SCE_NGS2_OK;
}

int APS5_VABI sceNgs2ReportRegisterHandler(uint32_t report_type, void* handler, uintptr_t user_data, Ngs2Handle* out_handle) {
    (void)report_type;
    (void)handler;
    (void)user_data;
    (void)out_handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceNgs2ReportUnregisterHandler(Ngs2Handle report_handle) {
    (void)report_handle;
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}

#pragma GCC visibility pop
