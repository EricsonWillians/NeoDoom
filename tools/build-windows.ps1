param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [string]$BuildDir = "build-windows",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Platform = "x64",
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),

    [switch]$Clean,
    [switch]$ConfigureOnly,
    [switch]$BuildOnly,
    [switch]$Install,
    [string]$InstallPrefix = "",
    [switch]$Package,

    [switch]$NoGltf,
    [switch]$NoPython,
    [switch]$NoVulkan,
    [switch]$OpenALVcpkg,
    [switch]$NoOpenALVcpkg,
    [switch]$NoLibSndFileVcpkg
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "tools/build-windows.ps1 is intended to run on Windows."
}

if ($OpenALVcpkg -and $NoOpenALVcpkg) {
    throw "-OpenALVcpkg and -NoOpenALVcpkg cannot be used together. OpenAL Soft is bundled by default."
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "OK: $Message" -ForegroundColor Green
}

function Require-Command {
    param(
        [string]$Name,
        [string]$Hint
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found. $Hint"
    }
}

function Resolve-UnderRepo {
    param([string]$Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }

    return [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $Path))
}

function Invoke-Logged {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    Write-Host "> $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Get-VcpkgGitLinkCommit {
    $line = & git -C $script:RepoRoot ls-tree HEAD vcpkg 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($line)) {
        return ""
    }

    $parts = $line -split "\s+"
    if ($parts.Length -ge 3) {
        return $parts[2]
    }

    return ""
}

function Ensure-Vcpkg {
    Write-Step "Preparing vcpkg"

    $vcpkgDir = Join-Path $script:RepoRoot "vcpkg"
    $toolchain = Join-Path $vcpkgDir "scripts\buildsystems\vcpkg.cmake"
    $bootstrap = Join-Path $vcpkgDir "bootstrap-vcpkg.bat"
    $vcpkgExe = Join-Path $vcpkgDir "vcpkg.exe"

    if (-not (Test-Path $toolchain)) {
        $commit = Get-VcpkgGitLinkCommit
        if (Test-Path $vcpkgDir) {
            Remove-Item -Recurse -Force $vcpkgDir
        }

        Invoke-Logged git @("clone", "https://github.com/microsoft/vcpkg.git", $vcpkgDir)
        if (-not [string]::IsNullOrWhiteSpace($commit)) {
            Invoke-Logged git @("-C", $vcpkgDir, "checkout", $commit)
        }
    }

    if (-not (Test-Path $vcpkgExe)) {
        if (-not (Test-Path $bootstrap)) {
            throw "vcpkg bootstrap script not found at $bootstrap"
        }
        Invoke-Logged $bootstrap @("-disableMetrics")
    }

    Write-Ok "vcpkg is ready"
    return $toolchain
}

function Find-BiasedDoomExe {
    $candidates = @(
        (Join-Path $script:BuildPath "$Configuration\biaseddoom.exe"),
        (Join-Path $script:BuildPath "biaseddoom.exe"),
        (Join-Path $script:BuildPath "bin\biaseddoom.exe"),
        (Join-Path $script:BuildPath "Release\biaseddoom.exe"),
        (Join-Path $script:BuildPath "Debug\biaseddoom.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    return ""
}

function Find-AudioProbe {
    $candidates = @(
        (Join-Path $script:BuildPath "$Configuration\biaseddoom-audio-probe.exe"),
        (Join-Path $script:BuildPath "biaseddoom-audio-probe.exe"),
        (Join-Path $script:BuildPath "Release\biaseddoom-audio-probe.exe"),
        (Join-Path $script:BuildPath "Debug\biaseddoom-audio-probe.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }

    return ""
}

function Test-BundledAudio {
    if ($NoOpenALVcpkg -or $NoLibSndFileVcpkg) {
        Write-Host "Skipping bundled-audio probe because a bundled dependency was disabled." -ForegroundColor Yellow
        return
    }

    $probe = Find-AudioProbe
    if ([string]::IsNullOrWhiteSpace($probe)) {
        throw "Audio regression probe was not found under $script:BuildPath"
    }

    $oldDrivers = $env:ALSOFT_DRIVERS
    try {
        $env:ALSOFT_DRIVERS = "null"
        Invoke-Logged $probe @(
            (Join-Path $script:RepoRoot "wadsrc\static\sounds\dsquake.ogg"),
            (Join-Path $script:RepoRoot "wadsrc\static\sounds\dssecret.flac")
        )
    }
    finally {
        if ($null -eq $oldDrivers) {
            Remove-Item Env:\ALSOFT_DRIVERS -ErrorAction SilentlyContinue
        }
        else {
            $env:ALSOFT_DRIVERS = $oldDrivers
        }
    }

    Write-Ok "Bundled OpenAL, OGG, and FLAC regression probe passed"
}

function Copy-OptionalFiles {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path $SourceDir)) {
        return
    }

    foreach ($pattern in @("*.pk3", "*.dll")) {
        Get-ChildItem -Path $SourceDir -Filter $pattern -File -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-Item $_.FullName -Destination $DestinationDir -Force }
    }

    foreach ($directory in @("soundfonts", "fm_banks", "python")) {
        $source = Join-Path $SourceDir $directory
        if (Test-Path $source) {
            Copy-Item $source -Destination $DestinationDir -Recurse -Force
        }
    }
}

function Write-WindowsPackageReadme {
    param([string]$DestinationDir)

    @"
BiasedDoom for Windows x64

Run biaseddoom.exe to start the engine. You still need a supported IWAD such as DOOM2.WAD.

Example:
  biaseddoom.exe -iwad C:\Games\Doom\DOOM2.WAD

Keep the PK3 files, DLLs, soundfonts, fm_banks, and python folders beside biaseddoom.exe.
Python mods are trusted code and stay disabled until you pass -python or set py_enabled true.
OpenAL Soft and the compressed-audio codecs are built in; do not copy OpenAL or sndfile DLLs from another source port into this folder.

For a complete startup/audio report that exits after checking, run:
  biaseddoom.exe -stdout -audiodiagnostics -norun

The snd_status and snd_listdrivers console commands add backend and device details to that log.
The automatic log is written to %LOCALAPPDATA%\biaseddoom\biaseddoom-audio.log, with a fallback beside biaseddoom.exe.
Disconnected endpoints are reopened automatically; if Windows temporarily has no usable output, BiasedDoom keeps retrying with bounded backoff.
See TROUBLESHOOTING.md for IWAD, audio, mod, and build diagnostics.
"@ | Set-Content -Path (Join-Path $DestinationDir "README-Windows.txt") -Encoding ascii
}

function New-WindowsPackage {
    param([string]$Executable)

    Write-Step "Creating Windows package"

    $artifactRoot = Join-Path $script:RepoRoot "artifacts"
    $packageName = "BiasedDoom-Windows-x64-$Configuration"
    $stageDir = Join-Path $artifactRoot $packageName
    $zipPath = Join-Path $artifactRoot "$packageName.zip"
    $checksumPath = "$zipPath.sha256"

    if (Test-Path $stageDir) {
        Remove-Item -Recurse -Force $stageDir
    }
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
    New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null

    Copy-Item $Executable -Destination $stageDir -Force
    $exeDir = Split-Path -Parent $Executable
    Copy-OptionalFiles -SourceDir $exeDir -DestinationDir $stageDir
    Copy-OptionalFiles -SourceDir $script:BuildPath -DestinationDir $stageDir
    Copy-Item (Join-Path $script:RepoRoot "TROUBLESHOOTING.md") -Destination $stageDir -Force
    Write-WindowsPackageReadme -DestinationDir $stageDir

    if (-not (Test-Path (Join-Path $stageDir "biaseddoom.exe"))) {
        throw "Package staging failed: biaseddoom.exe is missing."
    }

    if (-not (Get-ChildItem -Path $stageDir -Filter "*.pk3" -File -ErrorAction SilentlyContinue)) {
        throw "Package staging failed: no PK3 resource files were found."
    }

    if (-not (Test-Path (Join-Path $stageDir "zmusic.dll"))) {
        throw "Package staging failed: zmusic.dll is missing."
    }
    if (-not (Test-Path (Join-Path $stageDir "TROUBLESHOOTING.md"))) {
        throw "Package staging failed: TROUBLESHOOTING.md is missing."
    }

    if (-not $NoOpenALVcpkg -and (Get-ChildItem -Path $stageDir -Filter "openal*.dll" -File -ErrorAction SilentlyContinue)) {
        throw "Package staging failed: bundled OpenAL build unexpectedly contains a loose OpenAL DLL."
    }
    if (-not $NoLibSndFileVcpkg -and (Get-ChildItem -Path $stageDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)(sndfile|mpg123).*\.dll$' })) {
        throw "Package staging failed: bundled codec build unexpectedly contains a loose decoder DLL."
    }

    if (-not $NoPython -and -not (Test-Path (Join-Path $stageDir "python\Lib\encodings\__init__.py"))) {
        throw "Package staging failed: the embedded Python standard library is missing."
    }
    if (-not $NoPython -and -not (Test-Path (Join-Path $stageDir "python\LICENSE.txt"))) {
        throw "Package staging failed: the CPython license is missing."
    }

    if (Test-Path $zipPath) {
        Remove-Item -Force $zipPath
    }

    Compress-Archive -Path $stageDir -DestinationPath $zipPath -Force
    $hash = Get-FileHash -Algorithm SHA256 -Path $zipPath
    "$($hash.Hash.ToLowerInvariant())  $(Split-Path -Leaf $zipPath)" |
        Set-Content -Path $checksumPath -Encoding ascii

    Write-Ok "Package: $zipPath"
    Write-Ok "Checksum: $checksumPath"
}

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSCommandPath) ".."))
$script:BuildPath = Resolve-UnderRepo $BuildDir

Write-Step "Checking tools"
Require-Command "git" "Install Git for Windows."
Require-Command "cmake" "Install CMake 3.16 or newer."
Write-Ok "Git and CMake found"

$toolchainFile = Ensure-Vcpkg

if ($Clean -and (Test-Path $script:BuildPath)) {
    Write-Step "Cleaning build directory"
    Remove-Item -Recurse -Force $script:BuildPath
}

if (-not $BuildOnly) {
    Write-Step "Configuring BiasedDoom"

    $gltf = if ($NoGltf) { "OFF" } else { "ON" }
	$python = if ($NoPython) { "OFF" } else { "ON" }
    $vulkan = if ($NoVulkan) { "OFF" } else { "ON" }
    $cmakeArgs = @(
        "-S", $script:RepoRoot,
        "-B", $script:BuildPath,
        "-G", $Generator,
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
        "-DBIASEDDOOM_ENABLE_GLTF=$gltf",
        "-DBIASEDDOOM_BUILD_GLTF=$gltf",
		"-DBIASEDDOOM_ENABLE_PYTHON=$python",
        "-DBIASEDDOOM_REQUIRE_PYTHON=$python",
        "-DBIASEDDOOM_BUILD_AUDIO_TESTS=ON",
        "-DHAVE_VULKAN=$vulkan",
        "-DOPENAL_SOFT_VCPKG=$(if ($NoOpenALVcpkg) { 'OFF' } else { 'ON' })",
        "-DDYN_OPENAL=$(if ($NoOpenALVcpkg) { 'ON' } else { 'OFF' })",
        "-DVCPKG_LIBSNDFILE=$(if ($NoLibSndFileVcpkg) { 'OFF' } else { 'ON' })",
        "-DDYN_SNDFILE=$(if ($NoLibSndFileVcpkg) { 'ON' } else { 'OFF' })",
        "-DPK3_QUIET_ZIPDIR=ON"
    )

    if ($Generator -like "Visual Studio*") {
        $cmakeArgs += @("-A", $Platform)
    } else {
        $cmakeArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
    }

    if ($Install -and -not [string]::IsNullOrWhiteSpace($InstallPrefix)) {
        $cmakeArgs += "-DCMAKE_INSTALL_PREFIX=$(Resolve-UnderRepo $InstallPrefix)"
    }

    Invoke-Logged cmake $cmakeArgs
}

if ($ConfigureOnly) {
    Write-Ok "Configure-only run complete"
    exit 0
}

Write-Step "Building BiasedDoom"
Invoke-Logged cmake @("--build", $script:BuildPath, "--config", $Configuration, "--parallel", "$Jobs")

$exe = Find-BiasedDoomExe
if ([string]::IsNullOrWhiteSpace($exe)) {
    throw "Build finished, but biaseddoom.exe was not found under $script:BuildPath"
}
Write-Ok "Executable: $exe"
Test-BundledAudio

if ($Install) {
    Write-Step "Installing BiasedDoom"
    $installArgs = @("--install", $script:BuildPath, "--config", $Configuration)
    if (-not [string]::IsNullOrWhiteSpace($InstallPrefix)) {
        $installArgs += @("--prefix", (Resolve-UnderRepo $InstallPrefix))
    }
    Invoke-Logged cmake $installArgs
}

if ($Package) {
    New-WindowsPackage -Executable $exe
}

Write-Ok "Windows build complete"
