// Local adapter regression test. Link with spla_defold_instance.cpp and spla_core.
// Texture operations are stubbed; no Defold graphics context is needed.
#include "spla_defold.h"
#include <cassert>
#include <iostream>

namespace spla_defold {
bool build_image_resources(const spriteloop::SplaPackage&,
                          std::vector<SplaDefoldImageResource>&, std::string&) { return true; }
bool upload_image_resources(dmGraphics::HContext, const spriteloop::SplaPackage&,
                            std::vector<SplaDefoldImageResource>&, dmGraphics::HTexture&,
                            int&, int&, std::size_t&, std::string&) { return true; }
void destroy_atlas_texture(dmGraphics::HContext, dmGraphics::HTexture&) {}
}

int main()
{
    using namespace spla_defold;
    SplaDefoldSharedPackageResource resource;
    resource.package.animations.resize(1);
    resource.baked_animations.resize(1);
    resource.package.skins.resize(2);
    resource.package.skins[0].id = "default";
    resource.package.skins[1].id = "custom";
    resource.package.skins[1].parts.resize(1);
    auto* first = create_instance_from_shared_resource(&resource);
    auto* second = create_instance_from_shared_resource(&resource);
    assert(first && second);
    assert(first->player.get() != second->player.get());
    assert(&instance_baked_animations(*first) == &resource.baked_animations);
    assert(&instance_baked_animations(*second) == &resource.baked_animations);
    assert(first->baked_animations.capacity() == 0);
    assert(set_instance_skin(*first, "default"));
    assert(&instance_baked_animations(*first) == &resource.baked_animations);
    assert(first->baked_animations.capacity() == 0);

    const auto revision = first->skin_revision;
    first->skin_state.variant_overrides_by_part = {0};
    assert(rebuild_instance_skin(*first));
    assert(!first->uses_shared_baked_animations);
    assert(&instance_baked_animations(*first) == &first->baked_animations);
    assert(&instance_baked_animations(*second) == &resource.baked_animations);
    assert(first->skin_revision == revision + 1);
    first->skin_state.variant_overrides_by_part = {-1};
    assert(rebuild_instance_skin(*first));
    assert(&instance_baked_animations(*first) == &resource.baked_animations);
    assert(first->baked_animations.capacity() == 0);

    assert(set_instance_skin(*first, "custom"));
    assert(!first->uses_shared_baked_animations);
    assert(set_instance_skin(*first, "default"));
    clear_instance_variants(*first);
    assert(first->uses_shared_baked_animations);
    set_instance_tint(*first, 0.5f, 0.25f, 0.75f);
    assert(second->tint_r == 1.0f);
    assert(&instance_baked_animations(*first) == &resource.baked_animations);

    destroy_instance(first, nullptr);
    assert(&instance_baked_animations(*second) == &resource.baked_animations);
    destroy_instance(second, nullptr);
    assert(registered_instances().empty());
    SplaDefoldInstance standalone;
    assert(rebuild_instance_skin(standalone));
    assert(!standalone.uses_shared_baked_animations);
    assert(&instance_baked_animations(standalone) == &standalone.baked_animations);
    std::cout << "Default geometry sharing checks passed\n";
}
