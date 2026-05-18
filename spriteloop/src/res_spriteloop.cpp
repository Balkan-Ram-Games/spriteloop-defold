#include "res_spriteloop.h"

#include "spla_defold.h"

#include <dmsdk/dlib/log.h>
#include <dmsdk/render/render.h>
#include <dmsdk/resource/resource.h>

#include <cstring>
#include <string>

// Defold resource type for compiled SpriteLoop component resources.
//
// The editor/Bob pipeline writes SpriteLoopDesc DDF data to .spriteloopc files. This resource
// type loads the DDF, preloads/acquires the configured material, and makes both available to
// comp_spriteloop.cpp during component creation.
namespace spla_defold {

namespace {

constexpr const char* default_material_path = "/spriteloop/spriteloop/materials/spriteloop.materialc";
constexpr const char* default_material_source_path = "/spriteloop/spriteloop/materials/spriteloop.material";

// Returns true when value ends with suffix.
bool ends_with(const std::string& value, const char* suffix)
{
    const std::size_t suffix_length = std::strlen(suffix);
    return value.size() >= suffix_length &&
           value.compare(value.size() - suffix_length, suffix_length, suffix) == 0;
}

// Returns the compiled material path used at runtime.
// desc may be null; empty material fields fall back to the adapter material.
std::string compiled_material_path_or_default(dmGameSystemDDF::SpriteLoopDesc* desc)
{
    std::string path =
        desc != nullptr && desc->m_Material != nullptr && desc->m_Material[0] != '\0'
            ? desc->m_Material
            : default_material_path;
    if (ends_with(path, ".material")) {
        path += "c";
    }
    return path;
}

// Returns the source material path used for Bob preload hints.
// desc may be null; compiled paths are converted back to .material paths.
std::string source_material_path_or_default(dmGameSystemDDF::SpriteLoopDesc* desc)
{
    std::string path =
        desc != nullptr && desc->m_Material != nullptr && desc->m_Material[0] != '\0'
            ? desc->m_Material
            : default_material_source_path;
    if (ends_with(path, ".materialc")) {
        path.resize(path.size() - 1);
    }
    return path;
}

// Releases material and DDF data owned by a SpriteLoopResource.
// factory is the Defold resource factory used to release material dependencies.
void release_resources(dmResource::HFactory factory, SpriteLoopResource* resource)
{
    if (resource == nullptr) {
        return;
    }
    if (resource->material != nullptr) {
        dmResource::Release(factory, resource->material);
        resource->material = nullptr;
    }
    if (resource->ddf != nullptr) {
        dmDDF::FreeMessage(resource->ddf);
        resource->ddf = nullptr;
    }
}

// Acquires dependent runtime resources for an already loaded SpriteLoopResource.
// The material must be world-space because component rendering emits world-space vertices.
dmResource::Result acquire_resources(dmResource::HFactory factory, SpriteLoopResource* resource)
{
    const std::string material_path = compiled_material_path_or_default(resource->ddf);
    dmResource::Result result =
        dmResource::Get(factory, material_path.c_str(), reinterpret_cast<void**>(&resource->material));
    if (result != dmResource::RESULT_OK) {
        dmLogError("Could not load SpriteLoop material '%s'", material_path.c_str());
        return result;
    }

    if (dmRender::GetMaterialVertexSpace(resource->material->m_Material) !=
        dmRenderDDF::MaterialDesc::VERTEX_SPACE_WORLD) {
        dmLogError("SpriteLoop material '%s' must use world vertex space", material_path.c_str());
        return dmResource::RESULT_NOT_SUPPORTED;
    }

    return dmResource::RESULT_OK;
}

// Bob/engine preload callback.
// params contains the serialized DDF buffer; the parsed DDF is stored in m_PreloadData and the
// source material is announced as a preload hint.
dmResource::Result resource_preload(const dmResource::ResourcePreloadParams* params)
{
    dmGameSystemDDF::SpriteLoopDesc* desc = nullptr;
    dmDDF::Result result = dmDDF::LoadMessage(params->m_Buffer, params->m_BufferSize, &desc);
    if (result != dmDDF::RESULT_OK) {
        return dmResource::RESULT_FORMAT_ERROR;
    }

    const std::string material_path = source_material_path_or_default(desc);
    dmResource::PreloadHint(params->m_HintInfo, material_path.c_str());
    if (desc->m_Package != nullptr && desc->m_Package[0] != '\0') {
        dmResource::PreloadHint(params->m_HintInfo, desc->m_Package);
    }
    *params->m_PreloadData = desc;
    return dmResource::RESULT_OK;
}

// Engine create callback for .spriteloopc resources.
// params supplies the preloaded DDF and resource factory; the created SpriteLoopResource is
// attached to params->m_Resource for component creation.
dmResource::Result resource_create(const dmResource::ResourceCreateParams* params)
{
    SpriteLoopResource* resource = new SpriteLoopResource;
    resource->ddf = static_cast<dmGameSystemDDF::SpriteLoopDesc*>(params->m_PreloadData);
    resource->path = params->m_Filename != nullptr ? params->m_Filename : "";
    dmResource::Result result = acquire_resources(params->m_Factory, resource);
    if (result != dmResource::RESULT_OK) {
        dmLogError("Could not load SpriteLoop component resource '%s'", params->m_Filename);
        release_resources(params->m_Factory, resource);
        delete resource;
        return result;
    }

    dmResource::SetResource(params->m_Resource, resource);
    return dmResource::RESULT_OK;
}

// Engine destroy callback for .spriteloopc resources.
// Releases the acquired material and parsed DDF.
dmResource::Result resource_destroy(const dmResource::ResourceDestroyParams* params)
{
    SpriteLoopResource* resource =
        static_cast<SpriteLoopResource*>(dmResource::GetResource(params->m_Resource));
    if (resource != nullptr) {
        release_resources(params->m_Factory, resource);
        delete resource;
    }
    return dmResource::RESULT_OK;
}

// Engine hot-reload callback for .spriteloopc resources.
// Replaces the DDF and reacquires the configured material when the source resource changes.
dmResource::Result resource_recreate(const dmResource::ResourceRecreateParams* params)
{
    dmGameSystemDDF::SpriteLoopDesc* desc = nullptr;
    dmDDF::Result result = dmDDF::LoadMessage(params->m_Buffer, params->m_BufferSize, &desc);
    if (result != dmDDF::RESULT_OK) {
        dmLogError("Could not reload SpriteLoop component resource '%s'", params->m_Filename);
        return dmResource::RESULT_FORMAT_ERROR;
    }

    SpriteLoopResource* resource =
        static_cast<SpriteLoopResource*>(dmResource::GetResource(params->m_Resource));
    release_resources(params->m_Factory, resource);
    resource->ddf = desc;
    dmResource::Result acquire_result = acquire_resources(params->m_Factory, resource);
    if (acquire_result != dmResource::RESULT_OK) {
        dmDDF::FreeMessage(resource->ddf);
        resource->ddf = nullptr;
    }
    return acquire_result;
}

// Registers callbacks for the spriteloopc resource type.
ResourceResult resource_type_register(HResourceTypeContext context,
                                      HResourceType type)
{
    return static_cast<ResourceResult>(
        dmResource::SetupType(context, type, 0, resource_preload, resource_create, 0, resource_destroy,
                              resource_recreate));
}

} // namespace

} // namespace spla_defold

DM_DECLARE_RESOURCE_TYPE(ResourceTypeSpriteLoopExt, SPLA_DEFOLD_COMPONENT_TYPE,
                         spla_defold::resource_type_register, 0);
