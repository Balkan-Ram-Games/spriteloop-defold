# SpriteLoop Defold Extension

SpriteLoop for Defold adds native playback for `.spla` SpriteLoop packages. The extension provides a Defold component, editor integration, Bob builders, Lua control helpers, and a small example project.

This repository is the public Defold extension package. The SpriteLoop SDK implementation remains private; this repo contains only the public SDK headers and prebuilt SDK libraries required by Defold native extension builds.

## Repository Layout

```text
game.project              # Example/validation Defold project
spriteloop/               # Defold native extension folder
  ext.manifest
  spla.lua
  spriteloop.lua
  include/spriteloop/     # Public SDK header snapshot
  lib/<arc-platform>/     # Prebuilt SDK library snapshot
  plugins/share/          # Prebuilt editor/Bob plugin jar
  src/                    # Defold glue code
  editor/                 # Defold editor integration
  pluginsrc/              # Bob plugin source
example/                  # Example project content
input/                    # Example input bindings
utils/                    # Local build/package helpers
```

## Using The Extension

For a Defold project, use the packaged `spriteloop/` extension folder or a release zip from this repository as a dependency. The extension folder must be present in the Defold project tree so the native extension builder can discover `spriteloop/ext.manifest`.

Lua scripts should use the public component API:

```lua
local spriteloop = require "spriteloop.spriteloop"

function init(self)
    spriteloop.play_anim("#body", "idle", { loop = true })
end
```

SpriteLoop component data references `.spla` packages, for example:

```text
embedded_components {
  id: "body"
  type: "spriteloop"
  data: "package: \"/example/assets/robot_idle.spla\"\n"
  ""
}
```

## Local Validation

Package the Defold extension from the repository root:

```powershell
python utils\package_defold_extension.py --output .artifacts\spriteloop-defold-extension.zip
```

Build the example project with Bob. If you already ran the package step, remove `.artifacts` first or build before packaging so Bob does not also discover the staged extension copy:

```powershell
Remove-Item .artifacts -Recurse -Force
```

Build with Bob:

```powershell
java -jar path\to\bob.jar --platform=x86_64-win32 --variant debug --build-server=https://build.defold.com clean build
```

If `spriteloop/pluginsrc/` or `spriteloop/commonsrc/spriteloop_ddf.proto` changes, rebuild the editor/Bob plugin jar before validating:

```powershell
.\utils\build_spriteloop_plugin.ps1 -Bob path\to\bob.jar -Platforms x86_64-win32
```

## SDK Snapshot

`spriteloop/SDK_VERSION` records the private SDK version used for the committed header and library snapshot. The current snapshot starts with the Windows desktop library:

```text
spriteloop/include/spriteloop/*.hpp
spriteloop/lib/x86_64-win32/spla_core.lib
```

To refresh the SDK snapshot from the private `spriteloop-sdk` repository after building `spla_core` there:

```powershell
Copy-Item ..\spriteloop-sdk\include\spriteloop\*.hpp .\spriteloop\include\spriteloop\ -Force
Copy-Item ..\spriteloop-sdk\build\windows\x64\release\spla_core.lib .\spriteloop\lib\x86_64-win32\ -Force
Set-Content .\spriteloop\SDK_VERSION "0.1.0"
```

Add more `spriteloop/lib/<arc-platform>/` directories only after the matching SDK libraries are built and validated.

## Notes

The extension links against prebuilt SpriteLoop SDK binaries instead of including private SDK source. The public repository may expose SDK headers and compiled libraries, but it should not contain the private SDK implementation, SDK tests, SDK tools, or root SDK build files.


