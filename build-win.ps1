param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$Generator = "",
    [string]$Platform = "",
    [int]$Parallel = 0,
    [switch]$Clean,
    [switch]$BuildTests,
    [switch]$BuildExamples,
    [switch]$DisableTools,
    [switch]$EnableZenoh,
    [switch]$EnableMqtt
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot "build-win"

function Say {
    param([string]$Message)
    Write-Host $Message
}

function Is-SingleConfigGenerator {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        return $false
    }

    return $Name -match "Ninja|Makefiles"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Say "Removing existing build-win directory..."
    Remove-Item -Recurse -Force $BuildDir
}

$ResolvedGenerator = $Generator
if ([string]::IsNullOrWhiteSpace($ResolvedGenerator) -and $env:CMAKE_GENERATOR) {
    $ResolvedGenerator = $env:CMAKE_GENERATOR
}

$ConfigureArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-DHAKO_PDU_ENDPOINT_BUILD_TESTS=$($BuildTests.IsPresent ? "ON" : "OFF")",
    "-DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=$($BuildExamples.IsPresent ? "ON" : "OFF")",
    "-DHAKO_PDU_ENDPOINT_BUILD_TOOLS=$($DisableTools.IsPresent ? "OFF" : "ON")",
    "-DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=$($EnableZenoh.IsPresent ? "ON" : "OFF")",
    "-DHAKO_PDU_ENDPOINT_ENABLE_MQTT=$($EnableMqtt.IsPresent ? "ON" : "OFF")"
)

if (-not [string]::IsNullOrWhiteSpace($ResolvedGenerator)) {
    $ConfigureArgs += @("-G", $ResolvedGenerator)
}

if (-not [string]::IsNullOrWhiteSpace($Platform)) {
    $ConfigureArgs += @("-A", $Platform)
}

if (Is-SingleConfigGenerator $ResolvedGenerator) {
    $ConfigureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
}

Say "Configuring Windows build in build-win ($Configuration)..."
& cmake @ConfigureArgs

$BuildArgs = @("--build", $BuildDir)
if (-not (Is-SingleConfigGenerator $ResolvedGenerator)) {
    $BuildArgs += @("--config", $Configuration)
}
if ($Parallel -gt 0) {
    $BuildArgs += @("--parallel", $Parallel)
}

Say "Building..."
& cmake @BuildArgs

Say "Done. Artifacts are under build-win."
