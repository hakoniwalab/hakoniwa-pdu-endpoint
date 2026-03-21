param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [string]$CsprojPath = "csharp/tests/SmokeTests/SmokeTests.csproj"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName
$SharedDll = Join-Path $BuildDir "src/$Configuration/hakoniwa_pdu_endpoint.dll"
$Csproj = Join-Path $ProjectRoot $CsprojPath

function Say {
    param([string]$Message)
    Write-Host $Message
}

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Say "Configuring shared native library build..."
    & cmake -S $ProjectRoot -B $BuildDir -DBUILD_SHARED_LIBS=ON
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed (exit code: $LASTEXITCODE)."
    }
}

Say "Building shared native library..."
& cmake --build $BuildDir --config $Configuration --target hakoniwa_pdu_endpoint
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed (exit code: $LASTEXITCODE)."
}

Say "Building C# smoke tests..."
& dotnet build $Csproj
if ($LASTEXITCODE -ne 0) {
    throw "dotnet build failed (exit code: $LASTEXITCODE)."
}

$env:PATH = "$(Split-Path $SharedDll);$env:PATH"

Say "Running C# smoke tests..."
& dotnet run --project $Csproj --no-build
if ($LASTEXITCODE -ne 0) {
    throw "C# smoke tests failed (exit code: $LASTEXITCODE)."
}
