#pragma once
#include "spla_defold.h"
#include <dmsdk/render/render.h>

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
}
