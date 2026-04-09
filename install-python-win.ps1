param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [string]$Generator = "",
    [string]$Platform = "",
    [string]$ToolchainFile = "",
    [string]$VcpkgTriplet = "",
    [string]$PythonCommand = "",
    [string]$Prefix = "C:\hakoniwa",
    [int]$Parallel = 0,
    [switch]$Clean,
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectRoot $BuildDirName
$PythonBuildDir = Join-Path $ProjectRoot "build\python"
$InstallRoot = Join-Path $Prefix "share\hakoniwa-pdu-endpoint\python"
$PackageName = "hakoniwa_pdu_endpoint"
$PackageInstallDir = Join-Path $InstallRoot $PackageName

function Say {
    param([string]$Message)
    Write-Host $Message
}

function Get-PythonCommand {
    param([string]$Requested)

    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        return @{
            Exe = $Requested
            Args = @()
        }
    }
    if (Get-Command py -ErrorAction SilentlyContinue) {
        return @{
            Exe = "py"
            Args = @("-3")
        }
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return @{
            Exe = "python"
            Args = @()
        }
    }
    if (Get-Command python3 -ErrorAction SilentlyContinue) {
        return @{
            Exe = "python3"
            Args = @()
        }
    }
    throw "Python launcher not found. Install Python or pass -PythonCommand."
}

function Find-FfiArtifact {
    param([string]$PackageBuildDir)

    $Candidates = Get-ChildItem -Path $PackageBuildDir -Filter "_c_endpoint_ffi*" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".pyd", ".dll") } |
        Sort-Object Name
    if ($Candidates.Count -eq 0) {
        throw "cffi artifact not found under: $PackageBuildDir"
    }
    return $Candidates[0].FullName
}

if ($BuildFirst.IsPresent) {
    Say "Building native library and Python cffi module..."
    $BuildArgs = @{
        Configuration = $Configuration
        BuildDirName = $BuildDirName
    }
    if (-not [string]::IsNullOrWhiteSpace($Generator)) {
        $BuildArgs["Generator"] = $Generator
    }
    if (-not [string]::IsNullOrWhiteSpace($Platform)) {
        $BuildArgs["Platform"] = $Platform
    }
    if (-not [string]::IsNullOrWhiteSpace($ToolchainFile)) {
        $BuildArgs["ToolchainFile"] = $ToolchainFile
    }
    if (-not [string]::IsNullOrWhiteSpace($VcpkgTriplet)) {
        $BuildArgs["VcpkgTriplet"] = $VcpkgTriplet
    }
    if (-not [string]::IsNullOrWhiteSpace($PythonCommand)) {
        $BuildArgs["PythonCommand"] = $PythonCommand
    }
    if ($Parallel -gt 0) {
        $BuildArgs["Parallel"] = $Parallel
    }
    if ($Clean.IsPresent) {
        $BuildArgs["Clean"] = $true
    }
    $BuildArgs["BuildNative"] = $true
    $BuildArgs["BuildFfi"] = $true

    & (Join-Path $ProjectRoot "build-python-win.ps1") @BuildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "build-python-win.ps1 failed (exit code: $LASTEXITCODE)."
    }
}

$PythonCmd = Get-PythonCommand -Requested $PythonCommand
$NativeLibDir = Join-Path $BuildDir "src\$Configuration"
$SharedLib = Join-Path $NativeLibDir "hakoniwa_pdu_endpoint.dll"
$ImportLib = Join-Path $NativeLibDir "hakoniwa_pdu_endpoint.lib"
$PackageSourceDir = Join-Path $ProjectRoot "python\$PackageName"
$PackageBuildDir = Join-Path $PythonBuildDir $PackageName

if (-not (Test-Path $SharedLib)) {
    throw "Shared library not found: $SharedLib. Run build-python-win.ps1 or pass -BuildFirst."
}

if (-not (Test-Path $PackageSourceDir)) {
    throw "Python package directory not found: $PackageSourceDir"
}

$FfiArtifact = Find-FfiArtifact -PackageBuildDir $PackageBuildDir

Say "Installing Python runtime files to $PackageInstallDir"
New-Item -ItemType Directory -Force -Path $PackageInstallDir | Out-Null
Copy-Item -Path (Join-Path $PackageSourceDir "*") -Destination $PackageInstallDir -Recurse -Force
Copy-Item -Path $FfiArtifact -Destination (Join-Path $PackageInstallDir (Split-Path $FfiArtifact -Leaf)) -Force
Copy-Item -Path $SharedLib -Destination (Join-Path $PackageInstallDir "hakoniwa_pdu_endpoint.dll") -Force
if (Test-Path $ImportLib) {
    Copy-Item -Path $ImportLib -Destination (Join-Path $PackageInstallDir "hakoniwa_pdu_endpoint.lib") -Force
}

$SchemaDir = Join-Path $ProjectRoot "config\schema"
if (Test-Path $SchemaDir) {
    $SchemaInstallDir = Join-Path $PackageInstallDir "schema"
    New-Item -ItemType Directory -Force -Path $SchemaInstallDir | Out-Null
    Copy-Item -Path (Join-Path $SchemaDir "*") -Destination $SchemaInstallDir -Recurse -Force
}

Say "Done."
Say "Import check example:"
Say "  `$env:PYTHONPATH=`"$InstallRoot;`$env:PYTHONPATH`""
Say "Optional explicit runtime hints:"
Say "  `$env:HAKO_PDU_ENDPOINT_LIB_DIR=`"$PackageInstallDir`""
Say "  `$env:HAKO_PDU_ENDPOINT_SHARED_LIB=`"$PackageInstallDir\hakoniwa_pdu_endpoint.dll`""
