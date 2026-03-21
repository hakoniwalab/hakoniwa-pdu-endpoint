param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [string]$TestExePath = "",
    [string]$GTestFilter = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName

function Say {
    param([string]$Message)
    Write-Host $Message
}

if ([string]::IsNullOrWhiteSpace($TestExePath)) {
    $TestExePath = Join-Path $BuildDir "test/$Configuration/endpoint_test.exe"
}

if (-not (Test-Path $TestExePath)) {
    Say "Test binary not found: $TestExePath"
    Say "Building endpoint_test..."
    & cmake --build $BuildDir --config $Configuration --target endpoint_test
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed (exit code: $LASTEXITCODE)."
    }
}

$Args = @("--gtest_color=no")
if (-not [string]::IsNullOrWhiteSpace($GTestFilter)) {
    $Args += "--gtest_filter=$GTestFilter"
}

Say "Running tests..."
& $TestExePath @Args
if ($LASTEXITCODE -ne 0) {
    throw "Test execution failed (exit code: $LASTEXITCODE)."
}
