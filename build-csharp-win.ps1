param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [switch]$BuildExamples,
    [switch]$BuildTests
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName

$LibCsproj = Join-Path $ProjectRoot "csharp/hakoniwa_pdu_endpoint/Hakoniwa.PduEndpoint.csproj"
$TestCsproj = Join-Path $ProjectRoot "csharp/tests/SmokeTests/SmokeTests.csproj"
$ExampleProjects = @(
    (Join-Path $ProjectRoot "csharp/examples/MinimalExample/MinimalExample.csproj"),
    (Join-Path $ProjectRoot "csharp/examples/ManualPumpExample/ManualPumpExample.csproj"),
    (Join-Path $ProjectRoot "csharp/examples/RecvNextExample/RecvNextExample.csproj")
)

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

Say "Building C# binding library..."
& dotnet build $LibCsproj
if ($LASTEXITCODE -ne 0) {
    throw "dotnet build failed (binding library)."
}

if ($BuildTests.IsPresent) {
    Say "Building C# smoke tests..."
    & dotnet build $TestCsproj
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet build failed (smoke tests)."
    }
}

if ($BuildExamples.IsPresent) {
    Say "Building C# examples..."
    foreach ($ExampleCsproj in $ExampleProjects) {
        & dotnet build $ExampleCsproj
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet build failed (example: $ExampleCsproj)."
        }
    }
}

Say "Done."
