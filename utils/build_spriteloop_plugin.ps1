param(
    [string]$Bob = $env:BOB,
    [string]$DefoldSdk = $env:DEFOLDSDK,
    [string]$BuildServer = "https://build.defold.com",
    [string]$Variant = "headless",
    [string[]]$Platforms = @("x86_64-win32")
)

if (-not $Bob) {
    if ($env:DYNAMO_HOME) {
        $Bob = Join-Path $env:DYNAMO_HOME "share/java/bob.jar"
    }
}

if (-not $Bob -or -not (Test-Path $Bob)) {
    throw "Bob not found. Set `$env:BOB to the path of bob.jar or set `$env:DYNAMO_HOME."
}

$Bob = (Resolve-Path $Bob).Path

if (-not $DefoldSdk) {
    $version = java -jar $Bob --version
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to read Bob version from $Bob."
    }
    $DefoldSdk = ($version -split "\s+")[-1]
}

Push-Location (Join-Path $PSScriptRoot "..")
try {
    $target = "spriteloop/plugins/share"
    $currentJar = Join-Path $target "pluginSpla.jar"
    $backupDir = Join-Path ([IO.Path]::GetTempPath()) ("spriteloop-plugin-backup-" + [Guid]::NewGuid().ToString("N"))
    $backupJar = Join-Path $backupDir "pluginSpla.jar"

    New-Item -ItemType Directory -Force $backupDir | Out-Null
    if (Test-Path $backupJar) {
        Remove-Item $backupJar -Force -ErrorAction Stop
    }
    if (Test-Path $currentJar) {
        Copy-Item $currentJar $backupJar -Force -ErrorAction Stop
        Remove-Item $currentJar -Force -ErrorAction Stop
    }

    foreach ($platform in $Platforms) {
        java -jar $Bob --platform=$platform clean build --build-artifacts=plugins --variant $Variant --build-server=$BuildServer --defoldsdk=$DefoldSdk
        if ($LASTEXITCODE -ne 0) {
            throw "Bob plugin build failed for $platform."
        }
    }

    New-Item -ItemType Directory -Force $target | Out-Null
    $buildRoot = "build"
    if (-not (Test-Path $buildRoot)) {
        throw "Bob did not create $buildRoot."
    }

    Get-ChildItem -Path $buildRoot -Recurse -File |
        Where-Object { $_.Name -like "*.jar" } |
        Select-Object FullName, Length

    $pluginJars = @(Get-ChildItem -Path $buildRoot -Recurse -Filter "pluginSpla.jar" |
        Where-Object {
            $_.FullName -like "*$([IO.Path]::DirectorySeparatorChar)spriteloop$([IO.Path]::DirectorySeparatorChar)*" -or
            $_.FullName -like "*$([IO.Path]::AltDirectorySeparatorChar)spriteloop$([IO.Path]::AltDirectorySeparatorChar)*"
        })
    if ($pluginJars.Count -eq 0) {
        throw "Bob did not produce a SpriteLoop plugin jar under $buildRoot."
    }
    if ($pluginJars.Count -gt 1) {
        $paths = ($pluginJars | ForEach-Object { $_.FullName }) -join [Environment]::NewLine
        throw "Bob produced multiple SpriteLoop plugin jars:$([Environment]::NewLine)$paths"
    }

    Copy-Item $pluginJars[0].FullName (Join-Path $target "pluginSpla.jar") -Force -ErrorAction Stop

    if (-not (Test-Path $currentJar)) {
        if (Test-Path $backupJar) {
            Copy-Item $backupJar $currentJar -Force -ErrorAction Stop
        }
        throw "Bob did not produce $currentJar."
    }

    if (Test-Path $backupJar) {
        Remove-Item $backupJar -Force -ErrorAction Stop
    }
    if (Test-Path $backupDir) {
        Remove-Item $backupDir -Recurse -Force -ErrorAction Stop
    }
}
finally {
    if ((Test-Path $backupJar) -and -not (Test-Path $currentJar)) {
        Copy-Item $backupJar $currentJar -Force -ErrorAction Stop
    }
    if (Test-Path $backupDir) {
        Remove-Item $backupDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
