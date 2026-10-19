#pragma once
#include "spriteloop/spla_baked.hpp"

namespace spla_defold {
struct MaskGroup {
    std::size_t index_start = 0;
    std::size_t index_count = 0;
    int source = -1;
    float padding = 0;
};

// Empty ordinary draws remain boundaries: joining across one changes soft-alpha
// composition even when UV clipping removed all of its triangles.
inline std::vector<MaskGroup> mask_groups(const spriteloop::SplaBakedFrame& frame) {
    std::vector<MaskGroup> groups;
    for (const auto& draw : frame.draws) {
        if (!groups.empty()) {
            auto& group = groups.back();
            if (group.source == draw.mask_source_part_index &&
                (group.source < 0 || group.padding == draw.mask_padding) &&
                group.index_start + group.index_count == draw.index_start) {
                group.index_count += draw.index_count;
                continue;
            }
        }
        groups.push_back({draw.index_start, draw.index_count,
                          draw.mask_source_part_index, draw.mask_padding});
    }
    return groups;
}
}
