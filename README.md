# SpriteLoop for Defold

SpriteLoop for Defold adds native playback for `.spla` animation packages. It
includes a `spriteloop` component, editor integration, Bob builders, Lua helpers,
and a small example project.

<img src="media/component.png" alt="SpriteLoop component in Defold" width="560">

## Install

Add a tagged GitHub archive URL to your Defold project dependencies:

```text
https://github.com/Balkan-Ram-Games/spriteloop-defold/archive/refs/tags/v0.7.0-alpha.zip
```

Then fetch libraries from the Defold editor. The archive exposes the extension
folder at `spriteloop/`, which lets Defold discover `spriteloop/ext.manifest`.

After the first install, restart the Defold editor before opening collections or
game objects that already contain embedded SpriteLoop components. Defold can
fetch the extension files without errors, but already-loaded editor resources may
not recognize the newly registered custom component type until the editor starts
again.

## Use

Create a SpriteLoop component and point its package field at a `.spla` file in
your project. Scripts can control the component through:

```lua
local spriteloop = require "spriteloop.spriteloop"

function init(self)
    spriteloop.play_anim("#body", "idle", { loop = true })
end
```

The included example project shows an embedded SpriteLoop component, movement
script, collision object, collection proxy load/unload flow, and cache debug UI.

## Skins and Part Variants

A SpriteLoop component can select a default skin in the Defold editor. Runtime
scripts can change the active skin and override individual part variants through
the component-oriented Lua API:

```lua
local spriteloop = require "spriteloop.spriteloop"

function init(self)
    local component = "#body"

    -- Applies the skin's part variants and visibility settings.
    spriteloop.set_skin(component, "blue_robot")

    -- Overrides one part after applying the skin.
    spriteloop.set_variant(component, "head", "head_helmet")

    spriteloop.play_anim(component, "idle", { loop = true })
end
```

Skin and part APIs use the IDs exported in the `.spla` package:

```lua
local changed = spriteloop.set_skin(url, skin_id)
local changed = spriteloop.set_variant(url, part_id, variant_id)
local changed = spriteloop.clear_variant(url, part_id)
spriteloop.clear_variants(url)
```

- `set_skin()` returns `true` when the skin ID exists.
- `set_variant()` returns `true` when the part exists and the variant belongs
  to that part.
- `clear_variant()` returns `true` when the part ID exists.
- `clear_variants()` removes every manual part override.

Manual part variants take precedence over the active skin. Clearing an override
restores the variant selected by the active skin, or the part's base image when
the skin does not override it. Changing skins does not clear manual part
overrides.

Use `get_info()` to discover the skins and variants available in the loaded
package:

```lua
local info = spriteloop.get_info("#body")

print("active skin index", info.skin_index) -- zero-based, or -1

for _, skin in ipairs(info.skins) do
    print(skin.id, skin.name, skin.override_count)
end

for _, variant in ipairs(info.variants) do
    print(variant.id, variant.name, variant.part_id, variant.z_offset)
end
```

`info.skins` and `info.variants` are Lua arrays. `skin_index` is the native
zero-based package index and is `-1` when no skin is active.

The same skin and part functions are also available from the low-level
`spriteloop.spla` module for handles returned by `spla.load()`:

```lua
local spla = require "spriteloop.spla"
local handle = spla.load("/character.spla")

spla.set_skin(handle, "blue_robot")
spla.set_variant(handle, "head", "head_helmet")
```

## Supported Native Extension Libraries

The repository currently includes prebuilt native extension libraries for these
Defold arc-platform folders:

```text
x86_64-win32
x86_64-linux
x86_64-osx
arm64-osx
wasm-web
```

More Defold arc-platforms can be added by committing the matching library under
`spriteloop/lib/<arc-platform>/` and validating the example project for that
target.

Defold/Bob uses `x86_64-macos` as the macOS build platform, but native extension
libraries still live under `x86_64-osx` and `arm64-osx`. This is expected.

Defold's current WebAssembly HTML5 arc-platform is `wasm-web`; the older
asm.js-style `js-web` platform is intentionally not shipped by this extension.

## Validate

Validate the example project with Bob from the repository root. Use the platform
matching the library you want to check:

```sh
python3 utils/validate.py --bob path/to/bob.jar --platform x86_64-win32
```

For macOS validation, use Bob's macOS platform name:

```sh
python3 utils/validate.py --bob path/to/bob.jar --platform x86_64-macos
```

For HTML5/WebAssembly validation, use Defold's WebAssembly platform name:

```sh
python3 utils/validate.py --bob path/to/bob.jar --platform wasm-web
```

On Windows, the same command works from PowerShell:

```powershell
python utils\validate.py --bob path\to\bob.jar --platform x86_64-win32
```

Pass `--java path/to/java` if Java is not on `PATH`. The script checks that the
committed extension files and platform library are present, then runs Bob with
`clean build`. Bob output is hidden on success; pass `--verbose` to print the
full build log.

If `spriteloop/pluginsrc/` or `spriteloop/commonsrc/spriteloop_ddf.proto`
changes, rebuild the editor/Bob plugin jar before validating:

```sh
python3 utils/build_spriteloop_plugin.py --bob path/to/bob.jar --platform x86_64-win32
```

## Layout

```text
game.project              # Example and validation project
spriteloop/               # Defold native extension
  ext.manifest
  spla.lua
  spriteloop.lua
  api/
  commonsrc/
  editor/
  include/
  lib/
  plugins/
  pluginsrc/
  src/
example/                  # Example content
input/                    # Example input bindings
utils/                    # Maintenance scripts
```
