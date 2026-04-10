param(
    [ValidateSet("bootstrap", "use-existing")]
    [string]$Mode = "bootstrap",
    [string]$PythonCommand = "",
    [string]$Prefix = "$env:LOCALAPPDATA\Hakoniwa\hakoniwa-pdu-endpoint",
    [string]$Version = "v1.0.0",
    [string]$RuntimeArchiveName = "hakoniwa-pdu-endpoint-windows-x64-cp312.zip",
    [string]$RuntimeUrl = "",
    [switch]$RunSmokeTest
)

$ErrorActionPreference = "Stop"

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
    throw "Python launcher not found. Install Python or pass -PythonCommand."
}

function Install-PythonPackages {
    param($PythonCmd)

    Say "Installing Python packages..."
    & $PythonCmd.Exe @($PythonCmd.Args) -m pip install --upgrade pip setuptools wheel cffi
    & $PythonCmd.Exe @($PythonCmd.Args) -m pip install --upgrade hakoniwa-pdu hakoniwa-pdu-endpoint
}

function Install-RuntimeBundle {
    param([string]$RuntimeRoot, [string]$Url, [string]$ArchiveName)

    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
    $ArchivePath = Join-Path $RuntimeRoot $ArchiveName
    Say "Downloading runtime bundle..."
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $ArchivePath
    Say "Extracting runtime bundle..."
    Expand-Archive -Force -Path $ArchivePath -DestinationPath $RuntimeRoot
    Remove-Item -Force $ArchivePath
}

function Find-RuntimeArtifacts {
    param([string]$RuntimeRoot)

    $Dll = Get-ChildItem -Path $RuntimeRoot -Recurse -Filter "*.dll" -ErrorAction Stop |
        Where-Object { $_.Name -like "*hakoniwa*pdu*endpoint*" } |
        Select-Object -First 1
    $Ffi = Get-ChildItem -Path $RuntimeRoot -Recurse -Filter "_c_endpoint_ffi*.pyd" -ErrorAction Stop |
        Select-Object -First 1

    if (-not $Dll) {
        throw "Runtime bundle does not contain hakoniwa_pdu_endpoint dll."
    }
    if (-not $Ffi) {
        throw "Runtime bundle does not contain _c_endpoint_ffi*.pyd."
    }

    return @{
        Dll = $Dll
        Ffi = $Ffi
    }
}

function Install-RuntimeIntoPythonPackage {
    param($PythonCmd, [string]$RuntimeRoot)

    $PackageDir = & $PythonCmd.Exe @($PythonCmd.Args) -c "import pathlib, hakoniwa_pdu_endpoint; print(pathlib.Path(hakoniwa_pdu_endpoint.__file__).resolve().parent)"
    $PackageDir = $PackageDir.Trim()
    $Artifacts = Find-RuntimeArtifacts -RuntimeRoot $RuntimeRoot
    Copy-Item -Force $Artifacts.Ffi.FullName (Join-Path $PackageDir $Artifacts.Ffi.Name)
    Copy-Item -Force $Artifacts.Dll.FullName (Join-Path $PackageDir $Artifacts.Dll.Name)
    return @{
        PackageDir = $PackageDir
        Dll = $Artifacts.Dll
    }
}

function Run-SmokeTest {
    param($PythonCmd, [string]$PackageDir, [string]$DllPath)

    Say "Running smoke test..."
    $env:HAKO_PDU_ENDPOINT_SHARED_LIB = $DllPath
    $env:HAKO_PDU_ENDPOINT_LIB_DIR = $PackageDir
    & $PythonCmd.Exe @($PythonCmd.Args) -c "from hakoniwa_pdu_endpoint import c_endpoint; print('import ok')"
}

if ([string]::IsNullOrWhiteSpace($RuntimeUrl)) {
    $RuntimeUrl = "https://github.com/hakoniwalab/hakoniwa-pdu-endpoint/releases/download/$Version/$RuntimeArchiveName"
}

$PythonCmd = Get-PythonCommand -Requested $PythonCommand
$RuntimeRoot = Join-Path $Prefix "runtime"

Say "Mode=$Mode"
Say "Prefix=$Prefix"
Say "RuntimeRoot=$RuntimeRoot"
Say "RuntimeUrl=$RuntimeUrl"

switch ($Mode) {
    "bootstrap" {
        Install-PythonPackages -PythonCmd $PythonCmd
        Install-RuntimeBundle -RuntimeRoot $RuntimeRoot -Url $RuntimeUrl -ArchiveName $RuntimeArchiveName
    }
    "use-existing" {
        Say "Using existing Python/runtime installation"
    }
}

$RuntimeInstall = Install-RuntimeIntoPythonPackage -PythonCmd $PythonCmd -RuntimeRoot $RuntimeRoot

Say "Set these environment variables before using the Python binding if needed:"
Say "  HAKO_PDU_ENDPOINT_SHARED_LIB=$($RuntimeInstall.Dll.FullName)"
Say "  HAKO_PDU_ENDPOINT_LIB_DIR=$($RuntimeInstall.PackageDir)"

if ($RunSmokeTest -or $Mode -eq "bootstrap") {
    Run-SmokeTest -PythonCmd $PythonCmd -PackageDir $RuntimeInstall.PackageDir -DllPath $RuntimeInstall.Dll.FullName
}

Say "Done."
