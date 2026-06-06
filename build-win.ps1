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

function Resolve-VcpkgToolchainFile {
    param(
        [string]$ProjectRoot
    )

    $Candidates = @()
    $ParentDir = Split-Path -Parent $ProjectRoot
    if (-not [string]::IsNullOrWhiteSpace($ParentDir)) {
        $Candidates += (Join-Path $ParentDir "vcpkg/scripts/buildsystems/vcpkg.cmake")
    }

    $Candidates += "C:\project\vcpkg\scripts\buildsystems\vcpkg.cmake"

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $Candidates += (Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake")
    }

    foreach ($Candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($Candidate) -and (Test-Path $Candidate)) {
            return $Candidate
        }
    }

    return ""
}

if ($Clean -and (Test-Path $BuildDir)) {
    Say "Removing existing $BuildDirName directory..."
    Remove-Item -Recurse -Force $BuildDir
}

$ResolvedGenerator = $Generator
if ([string]::IsNullOrWhiteSpace($ResolvedGenerator) -and $env:CMAKE_GENERATOR) {
    $ResolvedGenerator = $env:CMAKE_GENERATOR
}

$ResolvedToolchainFile = $ToolchainFile
if ([string]::IsNullOrWhiteSpace($ResolvedToolchainFile)) {
    $ResolvedToolchainFile = Resolve-VcpkgToolchainFile -ProjectRoot $ProjectRoot
}

$ResolvedVcpkgTriplet = $VcpkgTriplet
if ([string]::IsNullOrWhiteSpace($ResolvedVcpkgTriplet) -and -not [string]::IsNullOrWhiteSpace($ResolvedToolchainFile)) {
    $ResolvedVcpkgTriplet = "x64-windows"
}

$ResolvedPlatform = $Platform
if ([string]::IsNullOrWhiteSpace($ResolvedPlatform) -and -not [string]::IsNullOrWhiteSpace($ResolvedToolchainFile)) {
    $ResolvedPlatform = "x64"
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

if (-not [string]::IsNullOrWhiteSpace($ResolvedPlatform)) {
    $ConfigureArgs += @("-A", $ResolvedPlatform)
}

if (-not [string]::IsNullOrWhiteSpace($ResolvedToolchainFile)) {
    $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$ResolvedToolchainFile"
}

if (-not [string]::IsNullOrWhiteSpace($ResolvedVcpkgTriplet)) {
    $ConfigureArgs += "-DVCPKG_TARGET_TRIPLET=$ResolvedVcpkgTriplet"
}

if (-not [string]::IsNullOrWhiteSpace($HakoniwaCoreRoot)) {
    $ConfigureArgs += "-DHAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT=$HakoniwaCoreRoot"
}

if (Is-SingleConfigGenerator $ResolvedGenerator) {
    $ConfigureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
}

if (-not [string]::IsNullOrWhiteSpace($ResolvedToolchainFile)) {
    Say "Using vcpkg toolchain: $ResolvedToolchainFile"
    if (-not [string]::IsNullOrWhiteSpace($ResolvedVcpkgTriplet)) {
        Say "Using vcpkg triplet: $ResolvedVcpkgTriplet"
    }
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
