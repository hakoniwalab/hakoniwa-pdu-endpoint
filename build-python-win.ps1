param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [switch]$BuildNative,
    [switch]$BuildFfi
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName

function Say {
    param([string]$Message)
    Write-Host $Message
}

$DoBuildNative = $BuildNative.IsPresent
$DoBuildFfi = $BuildFfi.IsPresent

if (-not $DoBuildNative -and -not $DoBuildFfi) {
    $DoBuildNative = $true
    $DoBuildFfi = $true
}

if ($DoBuildNative) {
    if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
        Say "Configuring native build..."
        & cmake -S $ProjectRoot -B $BuildDir
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed (exit code: $LASTEXITCODE)."
        }
    }

    Say "Building native library..."
    & cmake --build $BuildDir --config $Configuration --target hakoniwa_pdu_endpoint
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed (exit code: $LASTEXITCODE)."
    }
}

if ($DoBuildFfi) {
    Say "Building Python cffi module..."
    & python3 (Join-Path $ProjectRoot "python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Python cffi build failed (exit code: $LASTEXITCODE)."
    }
}

Say "Done."
