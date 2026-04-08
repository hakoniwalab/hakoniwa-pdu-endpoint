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

function Is-SingleConfigGenerator {
    param([string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name)) {
        return $false
    }

    return $Name -match "Ninja|Makefiles"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Say "Removing existing $BuildDirName directory..."
    Remove-Item -Recurse -Force $BuildDir
}

$DoBuildNative = $BuildNative.IsPresent
$DoBuildFfi = $BuildFfi.IsPresent

if (-not $DoBuildNative -and -not $DoBuildFfi) {
    $DoBuildNative = $true
    $DoBuildFfi = $true
}

if ($DoBuildNative) {
    $ResolvedGenerator = $Generator
    if ([string]::IsNullOrWhiteSpace($ResolvedGenerator) -and $env:CMAKE_GENERATOR) {
        $ResolvedGenerator = $env:CMAKE_GENERATOR
    }

    if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
        $ConfigureArgs = @(
            "-S", $ProjectRoot,
            "-B", $BuildDir,
            "-DBUILD_SHARED_LIBS=ON",
            "-DHAKO_PDU_ENDPOINT_BUILD_TESTS=OFF",
            "-DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES=OFF"
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
        if (Is-SingleConfigGenerator $ResolvedGenerator) {
            $ConfigureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
        }

        Say "Configuring native build..."
        & cmake @ConfigureArgs
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed (exit code: $LASTEXITCODE)."
        }
    }

    Say "Building native library..."
    $BuildArgs = @("--build", $BuildDir, "--target", "hakoniwa_pdu_endpoint")
    if (-not (Is-SingleConfigGenerator $ResolvedGenerator)) {
        $BuildArgs += @("--config", $Configuration)
    }
    if ($Parallel -gt 0) {
        $BuildArgs += @("--parallel", $Parallel)
    }
    & cmake @BuildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed (exit code: $LASTEXITCODE)."
    }
}

if ($DoBuildFfi) {
    $PythonCmd = Get-PythonCommand -Requested $PythonCommand
    $NativeLibDir = Join-Path $BuildDir "src\$Configuration"
    $SharedLib = Join-Path $NativeLibDir "hakoniwa_pdu_endpoint.dll"
    if (-not (Test-Path $SharedLib)) {
        throw "Shared library not found: $SharedLib. Build native library first."
    }

    $env:HAKO_PDU_ENDPOINT_SHARED_LIB = $SharedLib
    $env:HAKO_PDU_ENDPOINT_LIB_DIR = $NativeLibDir

    Say "Building Python cffi module..."
    & $PythonCmd.Exe @($PythonCmd.Args) (Join-Path $ProjectRoot "python/hakoniwa_pdu_endpoint/build_c_endpoint_ffi.py")
    if ($LASTEXITCODE -ne 0) {
        throw "Python cffi build failed (exit code: $LASTEXITCODE)."
    }
}

Say "Done."
