#include "spla_defold_masks.h"
#include "spla_defold_mask_groups.h"
#include "res_spriteloop.h"
#include "spriteloop/spla_mask.hpp"
#include <dmsdk/sdk.h>
#include <dmsdk/gamesys/resources/res_material.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace spla_defold {
namespace {
// Offscreen vertices use z as a pass selector. They never enter the world pass.
struct Vertex { float x, y, pass, u, v, r, g, b, a; };
struct Pass { unsigned art = 0, coverage = 0, composite = 0; };
struct Batch { unsigned start, count, pass; dmGraphics::HTexture texture; };
// Immutable local-space frame data; world transform/tint remain per component.
struct CachedGeometry {
    dmGraphics::HVertexBuffer buffer=0;
    std::vector<Batch> batches;
    std::vector<Pass> passes;
    SplaDefoldBakedFrame quad;
    float left=0,bottom=0,right=1,top=1;
    size_t bytes=0;
    uint64_t touched=0;
    ~CachedGeometry() {if(buffer) dmGraphics::DeleteVertexBuffer(buffer);}
};
// Cache counters measure texture payload, not driver allocation overhead.
struct CacheStats {
    uint64_t requests=0, hits=0, render_hits=0, misses=0, compositions=0;
    uint64_t draws=0, clears=0, binds=0, evictions=0, fallbacks=0;
    uint64_t allocations=0, deletions=0, live_bytes=0, peak_bytes=0;
    uint64_t retained_bytes=0, live_entries=0, clock=0;
} cache_stats;
size_t result_budget=32*1024*1024;
using ResultKey=std::tuple<uintptr_t,bool,uintptr_t>;
struct Result {
    dmResource::HFactory factory=nullptr;
    dmGameSystem::MaterialResource* material=nullptr;
    dmGraphics::HRenderTarget target=0;
    dmGraphics::HTexture texture=0;
    SplaDefoldBakedFrame quad;
    float left=0,bottom=0,right=1,top=1;
    size_t bytes=0;
    uint64_t touched=0;
    ~Result() {
        if(target) {
            dmGraphics::DeleteRenderTarget(dmGraphics::GetInstalledContext(),target);
            cache_stats.live_bytes-=bytes;--cache_stats.live_entries;
            ++cache_stats.deletions;
        }
        if(material) dmResource::Release(factory,material);
    }
};
struct Scratch {
    dmGraphics::HRenderTarget group=0,coverage=0;
    unsigned width=0,height=0;
    bool nearest=false;
    size_t bytes=0;
    void clear() {
        auto context=dmGraphics::GetInstalledContext();
        if(group) dmGraphics::DeleteRenderTarget(context,group);
        if(coverage) dmGraphics::DeleteRenderTarget(context,coverage);
        group=coverage=0;width=height=0;bytes=0;
    }
} scratch;
struct Images {
    spriteloop::SplaPackage const* package = nullptr;
    std::vector<SplaDefoldImageResource> decoded;
    std::map<std::pair<float, bool>, dmGraphics::HTexture> alpha;
    dmGraphics::HTexture artwork = 0;
    std::map<std::pair<uintptr_t,bool>,std::shared_ptr<CachedGeometry>> geometry;
    size_t geometry_bytes=0;
    uint64_t geometry_clock=0;
    // Bound retained GPU payload per image/package owner. Evicted live buffers
    // remain owned by their current consumers until those consumers move on.
    static constexpr size_t geometry_budget=8*1024*1024;
    // Owner lifetime scopes frame pointers to one actual package generation.
    // Only component States retain Images; the cache cannot keep a package alive.
    std::map<ResultKey,std::shared_ptr<Result>> results;
    ~Images() {
        for(const auto& entry:results) cache_stats.retained_bytes-=entry.second->bytes;
        destroy_atlas_texture(dmGraphics::GetInstalledContext(), artwork);
        for (auto& entry : alpha) destroy_atlas_texture(dmGraphics::GetInstalledContext(), entry.second);
    }
};
struct State {
    SplaDefoldComponent* component = nullptr;
    dmResource::HFactory factory = nullptr;
    dmGameSystem::MaterialResource* material = nullptr;
    dmGraphics::HVertexDeclaration declaration = 0;
    dmGraphics::HVertexBuffer buffer = 0; // borrowed from prepared
    std::shared_ptr<CachedGeometry> prepared;
    std::shared_ptr<Result> completed;
    bool cacheable=false;
    dmGraphics::HTexture result = 0;
    dmGraphics::HRenderTarget result_target = 0, group_target = 0, coverage_target = 0;
    dmGraphics::HProgram program = dmGraphics::INVALID_PROGRAM_HANDLE;
    unsigned width = 0, height = 0;
    const SplaDefoldBakedFrame* baked_frame = nullptr;
    std::uint32_t skin_revision = 0;
    bool dirty = true;
    bool bounds_error_logged = false;
    bool nearest = false;
    std::shared_ptr<Images> images;
    std::vector<Vertex> vertices;
    std::vector<Batch> batches;
    std::vector<Pass> passes;
    SplaDefoldBakedFrame quad;
    float left = 0, bottom = 0, right = 1, top = 1;
    ~State() {
        const auto context = dmGraphics::GetInstalledContext();
        if (result_target) dmGraphics::DeleteRenderTarget(context, result_target);
        if (group_target) dmGraphics::DeleteRenderTarget(context, group_target);
        if (coverage_target) dmGraphics::DeleteRenderTarget(context, coverage_target);
        if (program != dmGraphics::INVALID_PROGRAM_HANDLE) dmGraphics::DeleteProgram(context, program);
        if (declaration) dmGraphics::DeleteVertexDeclaration(declaration);

        if (material) dmResource::Release(factory, material);
    }
};
std::unordered_map<const SplaDefoldComponent*, std::unique_ptr<State>> states;
std::unordered_map<const void*, std::weak_ptr<Images>> image_cache;
ResultKey result_key(const State& state) {
    return ResultKey(reinterpret_cast<uintptr_t>(state.baked_frame),state.nearest,
                     reinterpret_cast<uintptr_t>(state.material));
}
void trim_results(size_t incoming=0) {
    while(cache_stats.retained_bytes+incoming>result_budget) {
        std::shared_ptr<Images> owner;
        ResultKey key;
        uint64_t oldest=UINT64_MAX;
        for(auto& item:image_cache) if(auto images=item.second.lock())
            for(const auto& entry:images->results) if(entry.second->touched<oldest) {
                oldest=entry.second->touched;owner=images;key=entry.first;
            }
        if(!owner) break;
        cache_stats.retained_bytes-=owner->results.at(key)->bytes;
        owner->results.erase(key);++cache_stats.evictions;
    }
}
bool reuse_result(State& state) {
    if(!state.cacheable || !result_budget) return false;
    auto found=state.images->results.find(result_key(state));
    if(found==state.images->results.end()) return false;
    state.completed=found->second;
    auto& result=*state.completed;result.touched=++cache_stats.clock;
    state.result=result.texture;state.quad=result.quad;
    state.left=result.left;state.bottom=result.bottom;
    state.right=result.right;state.top=result.top;state.dirty=false;
    state.bounds_error_logged=false;
    return true;
}
unsigned next_pass = 1;
bool masks_prepared = false;
bool masks_prepare_success = true;
unsigned pass_id() {
    // Exactly representable by a float. IDs only need to be unique in a frame.
    if (next_pass >= 16000000) next_pass = 1;
    return next_pass++;
}

const SplaDefoldBakedFrame* frame_for(const SplaDefoldInstance& instance) {
    const auto* animation = instance.player->current_animation();
    const int index = instance.player->current_frame_index();
    if (!animation || index < 0) return nullptr;
    for (const auto& baked : instance_baked_animations(instance))
        if (baked.id == animation->id && static_cast<std::size_t>(index) < baked.frames.size())
            return &baked.frames[index];
    return nullptr;
}

dmGraphics::HTexture artwork_texture(State& state) {
    auto& images = *state.images;
    if (images.artwork) return images.artwork;
    std::string error;
    if (images.decoded.empty() && !build_image_resources(*images.package, images.decoded, error)) {
        dmLogError("SpriteLoop mask: %s", error.c_str());
        return 0;
    }
    auto resources = images.decoded;
    // Premultiply before texture filtering. Multiplying a filtered straight-alpha
    // sample in the shader lets invisible RGB bleed into deformed mesh edges.
    for (auto& resource : resources) {
        auto& pixels = resource.rgba_pixels;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
            for (int c = 0; c < 3; ++c)
                pixels[i + c] = static_cast<std::uint8_t>((pixels[i + c] * pixels[i + 3] + 127) / 255);
    }
    int width = 0, height = 0;
    std::size_t bytes = 0;
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), *images.package,
                               resources, images.artwork, width, height, bytes, error, true)) {
        dmLogError("SpriteLoop mask: %s", error.c_str());
        return 0;
    }
    return images.artwork;
}

dmGraphics::HTexture alpha_texture(State& state, float padding) {
    auto& images = *state.images;
    const auto key = std::make_pair(padding, state.nearest);
    auto found = images.alpha.find(key);
    if (found != images.alpha.end()) return found->second;
    std::string error;
    if (images.decoded.empty() && !build_image_resources(*images.package, images.decoded, error)) {
        dmLogError("SpriteLoop mask: %s", error.c_str());
        return 0;
    }
    auto resources = images.decoded;
    for (auto& resource : resources) {
        spriteloop::SplaRgbaImage image;
        image.width = resource.width;
        image.height = resource.height;
        image.pixels = std::move(resource.rgba_pixels);
        auto inset = spriteloop::inset_mask_alpha(image, padding, state.nearest);
        if (!inset) { dmLogError("SpriteLoop mask: %s", inset.error().message.c_str()); return 0; }
        resource.rgba_pixels = std::move(inset.value().pixels);
    }
    dmGraphics::HTexture texture = 0;
    int width = 0, height = 0;
    std::size_t bytes = 0;
    // Identical image sizes, order and packing options preserve the artwork UVs,
    // including rotation and transparent gutters outside mesh UVs.
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), *images.package,
                               resources, texture, width, height, bytes, error)) {
        dmLogError("SpriteLoop mask: %s", error.c_str());
        return 0;
    }
    images.alpha.emplace(key, texture);
    return texture;
}

void append(State& state, const std::vector<SplaDefoldBakedVertex>& vertices,
            const std::vector<std::uint32_t>& indices, std::size_t start,
            std::size_t count, unsigned id, dmGraphics::HTexture texture) {
    const unsigned begin = static_cast<unsigned>(state.vertices.size());
    for (std::size_t i = start; i < start + count; ++i) {
        const auto& v = vertices[indices[i]];
        state.vertices.push_back({v.x, v.y, static_cast<float>(id), v.u, v.v, v.r, v.g, v.b, v.a});
    }
    if (count) state.batches.push_back({begin, static_cast<unsigned>(count), id, texture});
}

// Read the compiled shader description rather than relying on private Material
// internals. Bob supplies the platform shader variants referenced by the material.
bool load_program(State& state, const char* path) {
    void* bytes = nullptr;
    uint32_t size = 0;
    if (dmResource::GetRaw(state.factory, path, &bytes, &size) != dmResource::RESULT_OK) return false;
    dmRenderDDF::MaterialDesc* material = nullptr;
    const auto parsed = dmDDF::LoadMessage(bytes, size, &material);
    free(bytes);
    if (parsed != dmDDF::RESULT_OK) return false;
    const std::string program_path = material->m_Program;
    dmDDF::FreeMessage(material);
    bytes = nullptr;
    if (dmResource::GetRaw(state.factory, program_path.c_str(), &bytes, &size) != dmResource::RESULT_OK) return false;
    dmGraphics::ShaderDesc* shader = nullptr;
    const auto shader_parsed = dmDDF::LoadMessage(bytes, size, &shader);
    free(bytes);
    if (shader_parsed != dmDDF::RESULT_OK) return false;
    char error[1024] = {};
    state.program = dmGraphics::NewProgram(dmGraphics::GetInstalledContext(), shader, error, sizeof(error));
    dmDDF::FreeMessage(shader);
    if (state.program == dmGraphics::INVALID_PROGRAM_HANDLE) dmLogError("SpriteLoop mask shader: %s", error);
    return state.program != dmGraphics::INVALID_PROGRAM_HANDLE;
}

std::unique_ptr<State> create_state(SplaDefoldComponent* component, dmResource::HFactory factory) {
    std::unique_ptr<State> state(new State);
    state->component = component;
    state->factory = factory;
    void* material_bytes = nullptr;
    uint32_t material_size = 0;
    if (dmResource::GetRaw(factory, component->resource->ddf->m_Material,
                           &material_bytes, &material_size) == dmResource::RESULT_OK) {
        dmRenderDDF::MaterialDesc* desc = nullptr;
        if (dmDDF::LoadMessage(material_bytes, material_size, &desc) == dmDDF::RESULT_OK) {
            if (desc->m_Samplers.m_Count) {
                state->nearest = desc->m_Samplers[0].m_FilterMag == dmRenderDDF::MaterialDesc::FILTER_MODE_MAG_NEAREST;
            }
            dmDDF::FreeMessage(desc);
        }
        free(material_bytes);
    }
    const char* material = component->resource->ddf->m_MaskMaterial;
    if (dmResource::Get(factory, material, reinterpret_cast<void**>(&state->material)) != dmResource::RESULT_OK) {
        dmLogError("Could not load SpriteLoop mask materials");
        return nullptr;
    }
    if (!load_program(*state, material)) return nullptr;
    auto context = dmGraphics::GetInstalledContext();
    auto streams = dmGraphics::NewVertexStreamDeclaration(context);
    dmGraphics::AddVertexStream(streams, "position", 3, dmGraphics::TYPE_FLOAT, false);
    dmGraphics::AddVertexStream(streams, "texcoord0", 2, dmGraphics::TYPE_FLOAT, false);
    dmGraphics::AddVertexStream(streams, "color", 4, dmGraphics::TYPE_FLOAT, false);
    state->declaration = dmGraphics::NewVertexDeclaration(context, streams);
    dmGraphics::DeleteVertexStreamDeclaration(streams);

    const auto& instance = *component->instance;
    const void* key = instance.shared_resource ? static_cast<const void*>(instance.shared_resource) : &instance;
    state->images = image_cache[key].lock();
    if (!state->images) {
        state->images = std::make_shared<Images>();
        state->images->package = &instance_package(instance);
        image_cache[key] = state->images;
    }
    return state;
}

}

bool package_has_masks(const spriteloop::SplaPackage& package) {
    return std::any_of(package.parts.begin(), package.parts.end(),
        [](const spriteloop::SplaPart& part) { return part.mask_source_part_index >= 0; });
}

bool submit_component_masks(SplaDefoldComponent* component, dmResource::HFactory factory,
                            dmRender::HRenderContext context) {
    auto& owned = states[component];
    if (owned && owned->images->package != &instance_package(*component->instance)) owned.reset();
    if (!owned) owned = create_state(component, factory);
    if (!owned) return false;
    auto& state = *owned;
    const auto* frame = frame_for(*component->instance);
    if (!frame) return false;
    if (state.baked_frame == frame && state.skin_revision == component->instance->skin_revision) return true;
    ++cache_stats.requests;
    state.completed.reset();
    state.result=0;
    state.baked_frame=frame;
    state.skin_revision=component->instance->skin_revision;
    state.cacheable=component->instance->uses_shared_baked_animations;
    if(reuse_result(state)) {++cache_stats.hits;return true;}
    ++cache_stats.misses;
    state.baked_frame=nullptr; // Failed preparation must remain retryable.
    const bool cacheable=component->instance->uses_shared_baked_animations;
    auto& images=*state.images;
    const auto geometry_key=std::make_pair(reinterpret_cast<uintptr_t>(frame),state.nearest);
    const auto cached=images.geometry.find(geometry_key);
    if(cacheable && cached!=images.geometry.end()) {
        const auto& g=*cached->second;
        cached->second->touched=++images.geometry_clock;
        state.prepared=cached->second;
        state.buffer=g.buffer;state.batches=g.batches;state.passes=g.passes;
        state.quad=g.quad;state.left=g.left;state.bottom=g.bottom;state.right=g.right;state.top=g.top;
        state.baked_frame=frame;state.skin_revision=component->instance->skin_revision;state.dirty=true;
        return true;
    }

    const auto artwork = artwork_texture(state);
    if (!artwork) return false;
    state.dirty = true;
    state.vertices.clear(); state.batches.clear(); state.passes.clear();
    const auto& bounds = instance_bounds(*component->instance);
    state.left = std::floor(bounds.min_x) - 1;
    state.bottom = std::floor(bounds.min_y) - 1;
    state.right = std::ceil(bounds.max_x) + 1;
    state.top = std::ceil(bounds.max_y) + 1;
    state.quad.vertices = {
        {state.left, state.bottom, 0, 0}, {state.right, state.bottom, 1, 0},
        {state.right, state.top, 1, 1}, {state.left, state.top, 0, 1}};
    state.quad.indices = {0, 1, 2, 0, 2, 3};
    for (const auto& group : mask_groups(*frame)) {
        Pass pass;
        pass.art = pass_id();
        append(state, frame->vertices, frame->indices, group.index_start, group.index_count, pass.art,
               artwork);
        if (group.source >= 0) {
            pass.coverage = pass_id(); pass.composite = pass_id();
            const auto source = std::find_if(frame->mask_sources.begin(), frame->mask_sources.end(),
                [&group](const spriteloop::SplaBakedMaskSource& value) { return value.part_index == group.source; });
            if (source != frame->mask_sources.end() && !source->indices.empty()) {
                const auto texture = alpha_texture(state, group.padding);
                if (!texture) return false;
                append(state, source->vertices, source->indices, 0, source->indices.size(), pass.coverage, texture);
            }
            append(state, state.quad.vertices, state.quad.indices, 0, 6, pass.composite, 0);
        }
        state.passes.push_back(pass);
    }
    const size_t bytes=state.vertices.size()*sizeof(Vertex);
    const bool immutable=cacheable && bytes<=Images::geometry_budget;
    // Customized and oversized frames retain a private dynamic buffer. Never
    // overwrite a buffer still referenced by the cache or another component.
    auto prepared=!immutable && state.prepared && state.prepared.use_count()==1
        ? state.prepared : std::make_shared<CachedGeometry>();
    prepared->bytes=bytes;
    if(prepared->buffer) {
        dmGraphics::SetVertexBufferData(prepared->buffer,static_cast<unsigned>(bytes),
            state.vertices.data(),dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
    } else {
        prepared->buffer=dmGraphics::NewVertexBuffer(dmGraphics::GetInstalledContext(),
            static_cast<unsigned>(bytes),state.vertices.data(),immutable
                ? dmGraphics::BUFFER_USAGE_STATIC_DRAW : dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
    }
    if(!prepared->buffer) return false;
    prepared->batches=state.batches;prepared->passes=state.passes;prepared->quad=state.quad;
    prepared->left=state.left;prepared->bottom=state.bottom;prepared->right=state.right;prepared->top=state.top;
    prepared->touched=++images.geometry_clock;
    state.prepared=prepared;state.buffer=prepared->buffer;
    if(cacheable && prepared->bytes<=Images::geometry_budget) {
        while(images.geometry_bytes+prepared->bytes>Images::geometry_budget && !images.geometry.empty()) {
            auto victim=images.geometry.begin();
            for(auto it=images.geometry.begin();it!=images.geometry.end();++it)
                if(it->second->touched<victim->second->touched) victim=it;
            images.geometry_bytes-=victim->second->bytes;images.geometry.erase(victim);
        }
        images.geometry.emplace(geometry_key,prepared);images.geometry_bytes+=prepared->bytes;
    }
    state.baked_frame = frame;
    state.skin_revision = component->instance->skin_revision;
    return true;
}

void destroy_component_masks(SplaDefoldComponent* component) {
    states.erase(component);
    if(states.empty()) scratch.clear();
    for (auto it = image_cache.begin(); it != image_cache.end();) {
        if (it->second.expired()) it = image_cache.erase(it); else ++it;
    }
}
void destroy_all_component_masks() { states.clear(); image_cache.clear(); scratch.clear(); }
const SplaDefoldBakedFrame* component_mask_frame(const SplaDefoldComponent* component) {
    auto it = states.find(component);
    return it == states.end() || !it->second ? nullptr : &it->second->quad;
}
dmGraphics::HTexture component_mask_texture(const SplaDefoldComponent* component) {
    auto it = states.find(component);
    return it == states.end() || !it->second ? 0 : it->second->result;
}

namespace {
dmGraphics::HRenderTarget new_target(unsigned width, unsigned height, bool nearest) {
    dmGraphics::RenderTargetCreationParams params = {};
    auto& creation = params.m_ColorBufferCreationParams[0];
    creation.m_Type = dmGraphics::TEXTURE_TYPE_2D;
    creation.m_Width = creation.m_OriginalWidth = static_cast<uint16_t>(width);
    creation.m_Height = creation.m_OriginalHeight = static_cast<uint16_t>(height);
    creation.m_Depth = creation.m_OriginalDepth = 1;
    creation.m_LayerCount = creation.m_MipMapCount = 1;
    creation.m_UsageHintBits = dmGraphics::TEXTURE_USAGE_FLAG_SAMPLE;
    auto& texture = params.m_ColorBufferParams[0];
    texture.m_Format = dmGraphics::TEXTURE_FORMAT_RGBA;
    texture.m_Width = static_cast<uint16_t>(width);
    texture.m_Height = static_cast<uint16_t>(height);
    texture.m_Depth = texture.m_LayerCount = 1;
    texture.m_MinFilter = texture.m_MagFilter = nearest ? dmGraphics::TEXTURE_FILTER_NEAREST : dmGraphics::TEXTURE_FILTER_LINEAR;
    texture.m_UWrap = texture.m_VWrap = dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE;
    params.m_ColorBufferLoadOps[0] = dmGraphics::ATTACHMENT_OP_LOAD;
    params.m_ColorBufferStoreOps[0] = dmGraphics::ATTACHMENT_OP_STORE;
    return dmGraphics::NewRenderTarget(dmGraphics::GetInstalledContext(), dmGraphics::BUFFER_TYPE_COLOR0_BIT, params);
}

// Composition may run before render-script commands (explicit preparation), or
// during a screen draw (the built-in renderer fallback). There is no public SDK
// getter for the active target or its binding parameters. Custom render scripts
// must prepare before commands execute; dispatch then never changes their target.
struct GraphicsState {
    dmGraphics::HContext context = dmGraphics::GetInstalledContext();
    dmGraphics::PipelineState pipeline = dmGraphics::GetPipelineState(context);
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    GraphicsState() { dmGraphics::GetViewport(context, &x, &y, &width, &height); }
    ~GraphicsState() {
        // Intentional screen boundary, not arbitrary caller-target restoration.
        // Custom scene targets are bound later by queued render commands.
        ++cache_stats.binds;
        dmGraphics::SetRenderTarget(context, 0, {});
        dmGraphics::SetViewport(context, x, y, width, height);
        const auto set = [this](dmGraphics::State state, bool enabled) {
            if (enabled) dmGraphics::EnableState(context, state); else dmGraphics::DisableState(context, state);
        };
        set(dmGraphics::STATE_DEPTH_TEST, pipeline.m_DepthTestEnabled);
        set(dmGraphics::STATE_STENCIL_TEST, pipeline.m_StencilEnabled);
        set(dmGraphics::STATE_CULL_FACE, pipeline.m_CullFaceEnabled);
        set(dmGraphics::STATE_SCISSOR_TEST, pipeline.m_ScissorTestEnabled);
        set(dmGraphics::STATE_BLEND, pipeline.m_BlendEnabled);
        dmGraphics::SetDepthMask(context, pipeline.m_WriteDepth);
        const unsigned mask = pipeline.m_WriteColorMask;
        dmGraphics::SetColorMask(context, mask & 1, mask & 2, mask & 4, mask & 8);
        dmGraphics::SetBlendFuncSeparate(context,
            static_cast<dmGraphics::BlendFactor>(pipeline.m_BlendSrcFactor),
            static_cast<dmGraphics::BlendFactor>(pipeline.m_BlendDstFactor),
            static_cast<dmGraphics::BlendFactor>(pipeline.m_BlendSrcFactorAlpha),
            static_cast<dmGraphics::BlendFactor>(pipeline.m_BlendDstFactorAlpha));
        dmGraphics::SetBlendEquationSeparate(context,
            static_cast<dmGraphics::BlendEquation>(pipeline.m_BlendEquationColor),
            static_cast<dmGraphics::BlendEquation>(pipeline.m_BlendEquationAlpha));
    }
};
}

bool render_component_masks(const SplaDefoldComponent* component) {
    auto it = states.find(component);
    if (it == states.end() || !it->second) return false;
    auto& state = *it->second;
    if (!state.dirty) return state.result != 0;
    // A failed explicit prepass must not retry inside an offscreen scene draw.
    if (masks_prepared) return false;
    // Submissions precede the prepass: another actor may have produced this key.
    if(reuse_result(state)) {++cache_stats.render_hits;return true;}
    GraphicsState saved;
    auto context = saved.context;
    const auto width = static_cast<unsigned>(std::max(1.0f, state.right - state.left));
    const auto height = static_cast<unsigned>(std::max(1.0f, state.top - state.bottom));
    const auto limit = std::min(65535u, dmGraphics::GetMaxTextureSize(context));
    if (width > limit || height > limit) {
        if (!state.bounds_error_logged) {
            dmLogError("SpriteLoop mask bounds exceed texture dimensions (requested %ux%u, limit %u)",
                       width, height, limit);
            state.bounds_error_logged = true;
        }
        return false;
    }
    state.bounds_error_logged = false;
    const size_t bytes=size_t(width)*height*4;
    const bool share=state.cacheable && bytes<=result_budget && result_budget>0;
    std::shared_ptr<Result> pending;
    dmGraphics::HRenderTarget result_target=0,group_target=0,coverage_target=0;
    if(share) {
        // No per-actor scratch targets on the shared path. Misses execute serially.
        if(state.result_target) dmGraphics::DeleteRenderTarget(context,state.result_target);
        if(state.group_target) dmGraphics::DeleteRenderTarget(context,state.group_target);
        if(state.coverage_target) dmGraphics::DeleteRenderTarget(context,state.coverage_target);
        state.result_target=state.group_target=state.coverage_target=0;
        state.width=state.height=0;
        if(width!=scratch.width || height!=scratch.height || state.nearest!=scratch.nearest) {
            scratch.clear();
            scratch.group=new_target(width,height,state.nearest);
            scratch.coverage=new_target(width,height,state.nearest);
            if(!scratch.group || !scratch.coverage) {scratch.clear();return false;}
            scratch.width=width;scratch.height=height;scratch.nearest=state.nearest;
            scratch.bytes=bytes*2;
        }
        trim_results(bytes);
        pending=std::make_shared<Result>();
        pending->target=new_target(width,height,state.nearest);
        if(!pending->target) return false;
        // Keep the material identity alive even if its last actor goes away
        // while another material's actors keep this package cache alive.
        pending->factory=state.factory;pending->material=state.material;
        dmResource::IncRef(state.factory,state.material);
        pending->bytes=bytes;++cache_stats.allocations;++cache_stats.live_entries;
        cache_stats.live_bytes+=bytes;
        cache_stats.peak_bytes=std::max(cache_stats.peak_bytes,cache_stats.live_bytes);
        pending->texture=dmGraphics::GetRenderTargetTexture(context,pending->target,dmGraphics::BUFFER_TYPE_COLOR0_BIT);
        pending->quad=state.quad;
        pending->left=state.left;pending->bottom=state.bottom;
        pending->right=state.right;pending->top=state.top;
        result_target=pending->target;group_target=scratch.group;coverage_target=scratch.coverage;
    } else {
        ++cache_stats.fallbacks;
        if(width!=state.width || height!=state.height || !state.result_target ||
           !state.group_target || !state.coverage_target) {
            if(state.result_target) dmGraphics::DeleteRenderTarget(context,state.result_target);
            if(state.group_target) dmGraphics::DeleteRenderTarget(context,state.group_target);
            if(state.coverage_target) dmGraphics::DeleteRenderTarget(context,state.coverage_target);
            state.result_target=new_target(width,height,state.nearest);
            state.group_target=new_target(width,height,state.nearest);
            state.coverage_target=new_target(width,height,state.nearest);
            state.width=state.height=0;state.result=0;
            if(!state.result_target || !state.group_target || !state.coverage_target) return false;
            state.width=width;state.height=height;
        }
        result_target=state.result_target;group_target=state.group_target;coverage_target=state.coverage_target;
    }
    dmGraphics::SetViewport(context, 0, 0, width, height);
    dmGraphics::DisableState(context, dmGraphics::STATE_DEPTH_TEST);
    dmGraphics::DisableState(context, dmGraphics::STATE_STENCIL_TEST);
    dmGraphics::DisableState(context, dmGraphics::STATE_CULL_FACE);
    dmGraphics::DisableState(context, dmGraphics::STATE_SCISSOR_TEST);
    dmGraphics::EnableState(context, dmGraphics::STATE_BLEND);
    dmGraphics::SetDepthMask(context, false);
    dmGraphics::SetColorMask(context, true, true, true, true);
    dmGraphics::SetBlendFunc(context, dmGraphics::BLEND_FACTOR_ONE, dmGraphics::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    dmGraphics::SetBlendEquationSeparate(context, dmGraphics::BLEND_EQUATION_ADD, dmGraphics::BLEND_EQUATION_ADD);
    dmGraphics::EnableProgram(context, state.program);
    dmGraphics::EnableVertexBuffer(context, state.buffer, 0);
    dmGraphics::EnableVertexDeclaration(context, state.declaration, 0, 0, state.program);
    const auto projection = dmVMath::Matrix4::orthographic(state.left, state.right, state.bottom, state.top, -1, 1);
    dmGraphics::SetConstantM4(context, &projection, 1, dmGraphics::FindUniformLocation(state.program, "view_proj"));
    const auto clear = [context](dmGraphics::HRenderTarget target) {
        ++cache_stats.clears;++cache_stats.binds;
        dmGraphics::SetRenderTarget(context, target, {});
        dmGraphics::Clear(context, dmGraphics::BUFFER_TYPE_COLOR0_BIT, 0, 0, 0, 0, 1, 0);
    };
    const auto draw = [&](unsigned id, float mode, dmGraphics::HTexture artwork = 0, dmGraphics::HTexture coverage = 0) {
        const dmVMath::Vector4 selector(static_cast<float>(id), mode, 0, 0);
        dmGraphics::SetConstantV4(context, &selector, 1, dmGraphics::FindUniformLocation(state.program, "mask_pass"));
        for (const auto& batch : state.batches) {
            if (batch.pass != id) continue;
            const auto texture = artwork ? artwork : batch.texture;
            const auto alpha = coverage ? coverage : texture;
            // ESSL 100 cannot query textureSize in a shader. Supply the bound
            // texture's dimensions for artwork, coverage and composite draws.
            const dmVMath::Vector4 texture_size(
                static_cast<float>(dmGraphics::GetTextureWidth(context, texture)),
                static_cast<float>(dmGraphics::GetTextureHeight(context, texture)), 0, 0);
            dmGraphics::SetConstantV4(context, &texture_size, 1,
                dmGraphics::FindUniformLocation(state.program, "texture_size"));
            dmGraphics::EnableTexture(context, 0, 0, texture);
            dmGraphics::EnableTexture(context, 1, 0, alpha);
            const auto filter = state.nearest ? dmGraphics::TEXTURE_FILTER_NEAREST : dmGraphics::TEXTURE_FILTER_LINEAR;
            // Only artwork uses mipmaps. Coverage and compositing stay at level zero.
            const auto min_filter = !state.nearest && mode < 0.5f
                ? dmGraphics::TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR : filter;
            for (const auto handle : {texture, alpha})
                dmGraphics::SetTextureParams(context, handle, min_filter, filter,
                    dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE, dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE, 1.0f);
            dmGraphics::SetSampler(context, dmGraphics::FindUniformLocation(state.program, "texture_sampler"), 0);
            dmGraphics::SetSampler(context, dmGraphics::FindUniformLocation(state.program, "coverage_sampler"), 1);
            ++cache_stats.draws;
            dmGraphics::Draw(context, dmGraphics::PRIMITIVE_TRIANGLES, batch.start, batch.count, 1);
            dmGraphics::DisableTexture(context, 1, alpha);
            dmGraphics::DisableTexture(context, 0, texture);
        }
    };
    clear(result_target);
    for (const auto& pass : state.passes) {
        if (!pass.coverage) {
            ++cache_stats.binds;
            dmGraphics::SetRenderTarget(context, result_target, {});
            draw(pass.art, 0);
        } else {
            clear(group_target); draw(pass.art, 0);
            clear(coverage_target); draw(pass.coverage, 1);
            ++cache_stats.binds;
            dmGraphics::SetRenderTarget(context, result_target, {});
            draw(pass.composite, 2,
                dmGraphics::GetRenderTargetTexture(context, group_target, dmGraphics::BUFFER_TYPE_COLOR0_BIT),
                dmGraphics::GetRenderTargetTexture(context, coverage_target, dmGraphics::BUFFER_TYPE_COLOR0_BIT));
        }
    }
    dmGraphics::DisableVertexDeclaration(context, state.declaration);
    dmGraphics::DisableVertexBuffer(context, state.buffer);
    dmGraphics::DisableProgram(context);
    ++cache_stats.compositions;
    state.result=dmGraphics::GetRenderTargetTexture(context,result_target,dmGraphics::BUFFER_TYPE_COLOR0_BIT);
    if(pending) {
        pending->touched=++cache_stats.clock;
        state.images->results.emplace(result_key(state),pending);
        cache_stats.retained_bytes+=pending->bytes;
        state.completed=pending;
    }
    state.dirty = false;
    return true;
}

void begin_mask_frame() { masks_prepared = false; }

bool prepare_component_masks() {
    if (masks_prepared) return masks_prepare_success;
    bool success = true;
    for (const auto& entry : states)
        if (entry.second && !render_component_masks(entry.first)) success = false;
    masks_prepared = true;
    masks_prepare_success = success;
    return success;
}


bool set_mask_composition_cache_budget(uint64_t bytes)
{
    if (bytes > 128 * 1024 * 1024) return false;
    result_budget = static_cast<size_t>(bytes);
    trim_results();
    return true;
}

MaskCompositionCacheStats mask_composition_cache_stats()
{
    MaskCompositionCacheStats stats;
    stats.budget = result_budget;
    stats.retained_bytes = cache_stats.retained_bytes;
    stats.live_bytes = cache_stats.live_bytes;
    stats.peak_live_bytes = cache_stats.peak_bytes;
    stats.live_entries = cache_stats.live_entries;
    stats.scratch_bytes = scratch.bytes;
    for (const auto& item : image_cache) {
        if (auto images = item.second.lock()) {
            ++stats.owners;
            stats.entries += images->results.size();
        }
    }
    for (const auto& item : states) {
        if (!item.second) continue;
        const auto& state = *item.second;
        stats.fallback_bytes += uint64_t(state.width) * state.height * 4 *
            ((state.result_target ? 1 : 0) + (state.group_target ? 1 : 0) + (state.coverage_target ? 1 : 0));
    }
    stats.pinned_unretained_bytes = cache_stats.live_bytes - cache_stats.retained_bytes;
    stats.requests = cache_stats.requests;
    stats.hits = cache_stats.hits;
    stats.render_hits = cache_stats.render_hits;
    stats.misses = cache_stats.misses;
    stats.compositions = cache_stats.compositions;
    stats.draws = cache_stats.draws;
    stats.clears = cache_stats.clears;
    stats.binds = cache_stats.binds;
    stats.evictions = cache_stats.evictions;
    stats.fallbacks = cache_stats.fallbacks;
    stats.allocations = cache_stats.allocations;
    stats.deletions = cache_stats.deletions;
    return stats;
}

} // namespace spla_defold
