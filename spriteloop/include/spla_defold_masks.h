#pragma once
#include "spla_defold.h"
#include <dmsdk/render/render.h>
#include <cstdint>

namespace spla_defold {
bool package_has_masks(const spriteloop::SplaPackage& package);
bool submit_component_masks(SplaDefoldComponent* component, dmResource::HFactory factory,
                            dmRender::HRenderContext context);
void destroy_component_masks(SplaDefoldComponent* component);
void destroy_all_component_masks();
const SplaDefoldBakedFrame* component_mask_frame(const SplaDefoldComponent* component);
dmGraphics::HTexture component_mask_texture(const SplaDefoldComponent* component);
bool render_component_masks(const SplaDefoldComponent* component);
// Called before render-script update, then explicitly from custom render scripts.
void begin_mask_frame();
bool prepare_component_masks();
// Payload counters exclude graphics-driver overhead. Values are in bytes or counts.
struct MaskCompositionCacheStats {
    uint64_t budget = 0;
    uint64_t retained_bytes = 0;
    uint64_t live_bytes = 0;
    uint64_t peak_live_bytes = 0;
    uint64_t live_entries = 0;
    uint64_t scratch_bytes = 0;
    uint64_t owners = 0;
    uint64_t entries = 0;
    uint64_t fallback_bytes = 0;
    uint64_t pinned_unretained_bytes = 0;
    uint64_t requests = 0;
    uint64_t hits = 0;
    uint64_t render_hits = 0;
    uint64_t misses = 0;
    uint64_t compositions = 0;
    uint64_t draws = 0;
    uint64_t clears = 0;
    uint64_t binds = 0;
    uint64_t evictions = 0;
    uint64_t fallbacks = 0;
    uint64_t allocations = 0;
    uint64_t deletions = 0;
};
MaskCompositionCacheStats mask_composition_cache_stats();
// Returns false for budgets above 128 MiB; zero disables result retention.
bool set_mask_composition_cache_budget(uint64_t bytes);
}
