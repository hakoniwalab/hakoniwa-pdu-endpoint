param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDirName = "build-win",
    [string]$Generator = "",
    [string]$Platform = "",
    [string]$ToolchainFile = "",
    [string]$VcpkgTriplet = "",
    [string]$PythonCommand = "",
    [int]$Parallel = 0,
    [switch]$Clean,
    [switch]$BuildFirst
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

$PythonTests = @(
    (Join-Path $ProjectRoot "python/test/test_c_endpoint_smoke.py"),
    (Join-Path $ProjectRoot "python/test/test_endpoint_container_smoke.py")
)

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

$PythonCmd = Get-PythonCommand -Requested $PythonCommand

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

Say "Running Windows Python smoke tests..."
foreach ($TestScript in $PythonTests) {
    & $PythonCmd.Exe @($PythonCmd.Args) $TestScript
    if ($LASTEXITCODE -ne 0) {
        throw "Python smoke test failed: $TestScript"
    }
}

Say "Done."
