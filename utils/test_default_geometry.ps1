# Run from a Visual Studio x64 Developer PowerShell.
# Compile the real adapter/core with minimal Defold type and graphics stubs.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot
$testRoot = Join-Path $repoRoot '.artifacts/default-geometry-test'
$stubs = @{
    'gameobject/gameobject.h' = 'namespace dmGameObject { using HInstance = void*; }'
    'resource/resource.h' = ''
    'graphics/graphics.h' = 'namespace dmGraphics { using HTexture = unsigned long long; using HContext = void*; inline HContext GetInstalledContext() { return nullptr; } }'
    'dlib/vmath.h' = 'namespace dmVMath { struct Point3 { Point3(float,float,float) {} }; struct Vector3 { Vector3(float,float,float) {} }; struct Quat { Quat(float,float,float,float) {} }; }'
    'dlib/log.h' = 'inline void dmLogWarning(const char*, ...) {}'
}
foreach ($entry in $stubs.GetEnumerator()) {
    $stubPath = Join-Path $testRoot ('dmsdk/' + $entry.Key)
    New-Item -ItemType Directory -Force -Path (Split-Path $stubPath) | Out-Null
    Set-Content -LiteralPath $stubPath -Value $entry.Value
}
Push-Location $repoRoot
try {
    & cl /nologo /EHsc /std:c++17 /MT /I $testRoot /I spriteloop/include `
        utils/default_geometry_test.cpp spriteloop/src/spla_defold_instance.cpp `
        spriteloop/lib/x86_64-win32/spla_core.lib `
        "/Fe:$testRoot/test.exe" "/Fo:$testRoot/"
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed' }
    & "$testRoot/test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Geometry sharing checks failed' }
} finally {
    Pop-Location
}
