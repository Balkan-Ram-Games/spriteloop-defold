# New mesh presets: compatibility audit

Checked on 2026-10-04 against the current SpriteLoop app, SDK source, and the Windows SDK library bundled with this extension (`SDK_VERSION`: `0.3.0`).

## Finding

No new preset-specific runtime implementation or SPLA schema change was identified. The app evaluates presets while exporting and writes the resulting mesh-local vertex positions for every animation frame. Defold consumes those baked positions rather than recreating Limb, Bend, Wave, or Elastic behavior.

Painted influence weights, overlapping presets, local regions, wave motion, and preset options affect the evaluated positions in the app. Their authoring settings are not exported as runtime preset definitions. Consequently, adding another position-deforming preset does not itself require an extension change, provided it retains the existing mesh export contract.

## Existing contract and implementation

- App `src/editor/splaExport.ts`: exports rest positions, UVs, and triangle indices in each part's `mesh`; calls `deformPartMesh()` for each frame and exports the evaluated `vertices` for parts with presets or other mesh controls.
- App `src/editor/meshDeformation.ts` and `meshPresets.ts`: evaluate sampled controls and combine preset deformation before export.
- SDK `src/spla_loader.cpp`: reads frame-local positions and validates that their count matches the owning mesh.
- SDK `src/spla_baked.cpp`: uses frame-local positions with static UVs and triangle indices, converts authoring coordinates for rendering, and computes bounds across baked animation geometry.
- Extension `spriteloop/src/spla_defold_instance.cpp`: builds cached animation geometry through the SDK.
- Extension `spriteloop/src/comp_spriteloop.cpp`: submits the baked vertices and indices; it is not restricted to four-vertex sprites and does not interpret preset names.

Topology and UVs remain static within a part. Changes that introduce animated topology or animated UVs would require revisiting this contract. Runtime manipulation of preset controls is also outside the current baked-animation model.

## Verification performed

- 62 existing app mesh/preset/export tests passed.
- Six additional temporary export fixtures passed: Limb with painted forearm weights, Bend, free-tip Wave, Elastic, Elastic with Pull disabled, and layered Limb/Wave/Elastic with painted influence weights.
- Each fixture exported three frames. Exported positions matched the app's evaluated deformation, with rest topology retained and no preset definitions leaked into the manifest.
- A temporary C++ check linked directly against `spriteloop/lib/x86_64-win32/spla_core.lib`, using the extension's bundled headers. All six exported packages loaded and baked successfully; positions, UVs, triangle counts, and inclusion within calculated bounds matched expectations.

The temporary checks did not modify runtime code. This establishes compatibility of export, loading, and geometry baking on the bundled Windows SDK; it is not a visual test in a running Defold game or validation of every platform library.

## Before the next extension release

- Add representative preset packages to repeatable SDK/extension regression coverage, especially layered painted presets and time-driven Wave motion.
- Visually compare app output and Defold playback for skins/variants, flips, rotation/skew, atlas edges, and large deformations near camera boundaries.
- Exercise dense meshes and longer animations on target devices. Package size and cached geometry grow with vertex counts and frame counts; preset count alone does not imply new runtime solver work.
- Validate the supported platform libraries and keep bundled SDK headers/libraries synchronized when updating them.

Pixel-art filtering is a separate outstanding integration task: see [pixel-art-filtering.md](pixel-art-filtering.md).

## Mesh Depth update, 2026-10-07

SDK_VERSION is now 0.4.0. SPLA frame parts may include triangleOrder, a full permutation of static triangle ordinals. The SDK loader validates it and baked frames apply it before coordinate conversion and variant clipping. The extension already submits the resulting indices. The updated header and Windows library pass the Windows Defold build. This supersedes the earlier static-order compatibility statement for animated Depth. Non-Windows libraries require rebuilding against SDK 0.4.0 before release because the frame-part C++ ABI changed.
