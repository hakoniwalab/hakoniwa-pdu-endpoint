param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [string]$Generator = "",
    [string]$Platform = "",
    [string]$ToolchainFile = "",
    [string]$VcpkgTriplet = "",
    [string]$HakoniwaCoreRoot = "",
    [int]$Parallel = 0,
    [switch]$Clean,
    [switch]$BuildTests,
    [switch]$BuildExamples,
    [switch]$DisableTools,
    [switch]$BuildShared,
    [switch]$EnableZenoh,
    [switch]$EnableMqtt,
    [switch]$EnableHakoniwaCore
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName

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

function Get-OnOffValue {
    param(
        [bool]$Enabled
    )

    if ($Enabled) {
        return "ON"
    }
    return "OFF"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Say "Removing existing $BuildDirName directory..."
    Remove-Item -Recurse -Force $BuildDir
}

$ResolvedGenerator = $Generator
if ([string]::IsNullOrWhiteSpace($ResolvedGenerator) -and $env:CMAKE_GENERATOR) {
    $ResolvedGenerator = $env:CMAKE_GENERATOR
}

$ConfigureArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-DHAKO_PDU_ENDPOINT_BUILD_TESTS=$(Get-OnOffValue -Enabled $BuildTests.IsPresent)",
    "-DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=$(Get-OnOffValue -Enabled $BuildExamples.IsPresent)",
    "-DHAKO_PDU_ENDPOINT_BUILD_TOOLS=$(Get-OnOffValue -Enabled (-not $DisableTools.IsPresent))",
    "-DBUILD_SHARED_LIBS=$(Get-OnOffValue -Enabled $BuildShared.IsPresent)",
    "-DHAKO_PDU_ENDPOINT_ENABLE_ZENOH=$(Get-OnOffValue -Enabled $EnableZenoh.IsPresent)",
    "-DHAKO_PDU_ENDPOINT_ENABLE_MQTT=$(Get-OnOffValue -Enabled $EnableMqtt.IsPresent)",
    "-DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE=$(Get-OnOffValue -Enabled $EnableHakoniwaCore.IsPresent)"
)

if (-not [string]::IsNullOrWhiteSpace($ResolvedGenerator)) {
    $ConfigureArgs += @("-G", $ResolvedGenerator)
}

if (-not [string]::IsNullOrWhiteSpace($Platform)) {
    $ConfigureArgs += @("-A", $Platform)
}

if (-not [string]::IsNullOrWhiteSpace($ToolchainFile)) {
    $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"
}

if (-not [string]::IsNullOrWhiteSpace($VcpkgTriplet)) {
    $ConfigureArgs += "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet"
}

if (-not [string]::IsNullOrWhiteSpace($HakoniwaCoreRoot)) {
    $ConfigureArgs += "-DHAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT=$HakoniwaCoreRoot"
}

if (Is-SingleConfigGenerator $ResolvedGenerator) {
    $ConfigureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
}

Say "Configuring Windows build in $BuildDirName ($Configuration)..."
& cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed (exit code: $LASTEXITCODE)."
}

$BuildArgs = @("--build", $BuildDir)
if (-not (Is-SingleConfigGenerator $ResolvedGenerator)) {
    $BuildArgs += @("--config", $Configuration)
}
if ($Parallel -gt 0) {
    $BuildArgs += @("--parallel", $Parallel)
}

Say "Building..."
& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed (exit code: $LASTEXITCODE)."
}

Say "Done. Artifacts are under $BuildDirName."
