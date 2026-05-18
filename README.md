# SpriteLoop for Defold

SpriteLoop for Defold adds native playback for `.spla` animation packages. It
includes a `spriteloop` component, editor integration, Bob builders, Lua helpers,
and a small example project.

## Install

Add a tagged GitHub archive URL to your Defold project dependencies:

```text
https://github.com/<owner>/spriteloop-defold/archive/refs/tags/<version>.zip
```

Then fetch libraries from the Defold editor. The archive exposes the extension
folder at `spriteloop/`, which lets Defold discover `spriteloop/ext.manifest`.

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

## Supported Platforms

The repository currently includes prebuilt native libraries for:

```text
x86_64-win32
```

More Defold arc-platforms can be added by committing the matching library under
`spriteloop/lib/<arc-platform>/` and validating the example project for that
target.

## Validate

Validate the example project with Bob from the repository root. Use the platform
matching the library you want to check:

```sh
python3 utils/validate.py --bob path/to/bob.jar --platform x86_64-win32
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
