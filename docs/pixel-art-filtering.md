# Pixel-art filtering: deferred runtime support

Follow-up work to do after the new SpriteLoop app features are finished. This note records the current behavior and proposed integration; it does not change the extension.

## Current image loading

The extension reads embedded PNGs from a `.spla` package, decodes them into RGBA pixels with `stbi_load_from_memory`, packs them into an atlas at runtime, and uploads that atlas directly to a Defold GPU texture. This bypasses Defold's editor atlas/texture compilation pipeline.

Relevant code:

- `spriteloop/src/spla_defold_resources.cpp`: `build_image_resources()` decodes PNGs; `upload_image_resources()` packs and uploads the atlas. Both the texture upload parameters and the subsequent `SetTextureParams()` call explicitly select linear minification and magnification filtering.
- `spriteloop/src/spla_defold_instance.cpp`: creates instances and manages shared package/atlas resources.
- `spriteloop/src/comp_spriteloop.cpp`: submits atlas textures and materials for rendering.
- `spriteloop/spriteloop/materials/spriteloop.material`: the supplied material.

The app now offers independent project image filtering: smooth (linear) or sharp (nearest-neighbor). That preference controls editor rendering and exported image resizing. However, SPLA does not currently export the preference, so a sharp PNG asset does not tell the runtime how to sample it.

## Proposed follow-up

1. **App / SPLA format:** export the project's image filtering preference as optional package metadata. Decide the field location and naming together with the SDK update.
2. **SDK:** parse and expose the preference. Missing or unrecognized values should use linear filtering to preserve older packages' behavior.
3. **Defold extension:** use nearest minification and magnification for nearest-filtered packages, and retain linear filtering for smooth packages. Update both texture setup sites, and verify material samplers do not override the intended mode.
4. **Shared resources:** keep filtering consistent with package/atlas caching. If runtime overrides are introduced, account for the fact that instances can share a texture; changing one shared texture's filtering could affect other instances.

Defold supports filtering through material samplers and project defaults. Do not assume the game's defaults alone will carry the app's per-package preference. See [Defold texture filtering](https://defold.com/manuals/texture-filtering/) and [material sampler settings](https://defold.com/manuals/material/#sampler-settings).

PNG decoding and atlas packing do not inherently need to change for nearest sampling. Whole-pixel positioning is implemented independently in the app; see [Whole-pixel movement](../../spriteloop-app/docs/whole-pixel-movement.md). Runtime interpolation and instance-position snapping remain deferred. Selecting sharp filtering must not implicitly snap transforms or disable rotation and scale.

## Validation when implemented

- Load a tiny contrasting-pixel package and verify sharp rendering at enlarged scales, with both texture minification and magnification covered.
- Verify nearest and linear packages can coexist, including repeated instances that share package resources.
- Check the supplied material and a custom material with explicit sampler filters.
- Verify older packages retain their current appearance and smooth packages still render with linear filtering.
- Check atlas boundaries, transparency, animation playback, and texture recreation/reloading.

Lossless WebP export in the app is separate from this work: the extension loads embedded PNG assets, not the app's animated WebP exports.
