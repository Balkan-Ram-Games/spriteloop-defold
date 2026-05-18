#include "spla_defold.h"

#include <dmsdk/dlib/log.h>
#include <dmsdk/resource/resource.h>

// Resource type for raw .spla packages.
//
// SpriteLoop packages are zip files consumed by the native component. Registering a normal
// resource type lets Bob-bundled .spla files be acquired with dmResource::Get at runtime.
namespace spla_defold {

namespace {

dmResource::Result resource_create(const dmResource::ResourceCreateParams* params)
{
    if (params->m_Buffer == nullptr || params->m_BufferSize == 0) {
        dmLogError("SpriteLoop package resource '%s' is empty", params->m_Filename);
        return dmResource::RESULT_FORMAT_ERROR;
    }

    SplaPackageBytesResource* resource = new SplaPackageBytesResource;
    resource->path = params->m_Filename != nullptr ? params->m_Filename : "";
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(params->m_Buffer);
    resource->bytes.assign(bytes, bytes + params->m_BufferSize);
    dmResource::SetResource(params->m_Resource, resource);
    return dmResource::RESULT_OK;
}

dmResource::Result resource_destroy(const dmResource::ResourceDestroyParams* params)
{
    SplaPackageBytesResource* resource =
        static_cast<SplaPackageBytesResource*>(dmResource::GetResource(params->m_Resource));
    delete resource;
    return dmResource::RESULT_OK;
}

dmResource::Result resource_recreate(const dmResource::ResourceRecreateParams* params)
{
    if (params->m_Buffer == nullptr || params->m_BufferSize == 0) {
        dmLogError("SpriteLoop package resource '%s' is empty", params->m_Filename);
        return dmResource::RESULT_FORMAT_ERROR;
    }

    SplaPackageBytesResource* resource =
        static_cast<SplaPackageBytesResource*>(dmResource::GetResource(params->m_Resource));
    if (resource == nullptr) {
        return dmResource::RESULT_RESOURCE_NOT_FOUND;
    }

    resource->path = params->m_Filename != nullptr ? params->m_Filename : "";
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(params->m_Buffer);
    resource->bytes.assign(bytes, bytes + params->m_BufferSize);
    return dmResource::RESULT_OK;
}

ResourceResult resource_type_register(HResourceTypeContext context,
                                      HResourceType type)
{
    return static_cast<ResourceResult>(
        dmResource::SetupType(context, type, 0, 0, resource_create, 0, resource_destroy,
                              resource_recreate));
}

} // namespace

} // namespace spla_defold

DM_DECLARE_RESOURCE_TYPE(ResourceTypeSplaPackageExt, "spla",
                         spla_defold::resource_type_register, 0);
