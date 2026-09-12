[CmdletBinding()]
param(
    [string]$QtRoot = 'D:\Qt\6.8.3\msvc2022_64',
    [string]$YamlPackageDirectory = 'D:\Program Files\vcpkg\installed\x64-windows\share\yaml-cpp',
    [string]$VsEnvironment = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    [string]$Ninja = 'D:\Qt\Tools\Ninja\ninja.exe',
    [string]$BuildDirectory,
    [switch]$RunTests,
    [switch]$BuildOnly,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repoDirectory 'build/codex-dc-release'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program exited with code $LASTEXITCODE."
    }
}

foreach ($required in @($VsEnvironment, $Ninja, (Join-Path $QtRoot 'bin/windeployqt.exe'),
                        (Join-Path $YamlPackageDirectory 'yaml-cpp-config.cmake'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required tool or package is missing: $required. Override the corresponding script parameter."
    }
}

$cache = Join-Path $BuildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $sourceLine = Get-Content -LiteralPath $cache | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' }
    if ($sourceLine) {
        $cachedSource = [IO.Path]::GetFullPath($sourceLine.Substring($sourceLine.IndexOf('=') + 1))
        if (-not $cachedSource.Equals($repoDirectory, [StringComparison]::OrdinalIgnoreCase)) {
            throw "This build directory belongs to $cachedSource. Choose a new build directory for $repoDirectory."
        }
    }
}

# Import the selected x64 compiler environment without changing machine settings.
# Quote this trusted, validated batch-file path; reject cmd metacharacters supplied as a path.
$env:VSLANG = '1033'
& $env:ComSpec /d /c 'chcp 65001 >nul'
if ($LASTEXITCODE -ne 0) { throw 'Could not select UTF-8 for MSVC/CMake dependency detection.' }
if ($VsEnvironment -match '["\r\n&|<>^%!]') { throw 'Unsupported characters in VsEnvironment path.' }
$compilerEnvironment = & $env:ComSpec /d /c "call `"$VsEnvironment`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the MSVC x64 environment.' }
foreach ($entry in $compilerEnvironment) {
    $separator = $entry.IndexOf('=')
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable($entry.Substring(0, $separator), $entry.Substring($separator + 1), 'Process')
    }
}

$configurationStarted = [DateTime]::UtcNow
$testing = if ($RunTests) { 'ON' } else { 'OFF' }
Invoke-Checked $cmake @('-S', $repoDirectory, '-B', $BuildDirectory, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$Ninja", '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-Dyaml-cpp_DIR=$YamlPackageDirectory", "-DBUILD_TESTING=$testing")
Invoke-Checked $cmake @('--build', $BuildDirectory, '--config', 'Release', '--parallel', '4')

$captureDirectories = @((Join-Path $env:SystemRoot 'System32'), (Join-Path $env:SystemRoot 'System32/Npcap'))
$captureDirectory = $captureDirectories | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_ 'wpcap.dll')) -and (Test-Path -LiteralPath (Join-Path $_ 'Packet.dll'))
} | Select-Object -First 1
if (($RunTests -or -not $BuildOnly) -and -not $captureDirectory) {
    throw 'Npcap runtime was not found (wpcap.dll and Packet.dll). Install the x64 Npcap runtime before testing or launching.'
}
if ($RunTests) {
    Invoke-Checked $ctest @('--test-dir', $BuildDirectory, '-C', 'Release', '--output-on-failure', '--no-tests=error')
}
if ($BuildOnly) { return }

$manifestPath = Join-Path $BuildDirectory 'local-runtime-Release.json'
$runtime = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$executable = [IO.Path]::GetFullPath($runtime.executable)
$runtimeDirectory = Split-Path $executable
if (-not $runtime.sourceDirectory.Equals($repoDirectory.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The runtime manifest does not belong to this source checkout.'
}
if (-not $executable.StartsWith($BuildDirectory.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "The executable is outside the selected build directory: $executable"
}

foreach ($library in @($runtime.soemLibrary, $runtime.yamlLibrary)) {
    if ([IO.Path]::GetExtension($library) -ieq '.dll') {
        $destination = Join-Path $runtimeDirectory ([IO.Path]::GetFileName($library))
        if (-not ([IO.Path]::GetFullPath($library)).Equals([IO.Path]::GetFullPath($destination), [StringComparison]::OrdinalIgnoreCase)) {
            Copy-Item -LiteralPath $library -Destination $destination -Force
        }
        if ((Get-FileHash -LiteralPath $library).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            throw "Deployed DLL does not match its build/package source: $destination"
        }
    }
}
Invoke-Checked (Join-Path $runtime.qtBinDirectory 'windeployqt.exe') @('--release', '--compiler-runtime', '--verbose', '0',
    '--qmldir', (Join-Path $repoDirectory 'qml'), $executable)

# Keep DLL and Qt plugin resolution tied to this build, not another application install.
$env:PATH = "$runtimeDirectory;$captureDirectory;$env:SystemRoot\System32;$env:PATH"
$env:QT_PLUGIN_PATH = $runtimeDirectory
$env:QML_IMPORT_PATH = Join-Path $runtimeDirectory 'qml'
$env:QML2_IMPORT_PATH = $env:QML_IMPORT_PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $runtimeDirectory 'platforms'
$evidence = [ordered]@{
    sourceDirectory = $repoDirectory
    buildDirectory = $BuildDirectory
    buildCheckUtc = $configurationStarted.ToString('o')
    executable = $executable
    executableModifiedUtc = (Get-Item -LiteralPath $executable).LastWriteTimeUtc.ToString('o')
    executableSha256 = (Get-FileHash -LiteralPath $executable).Hash
    captureDirectory = $captureDirectory
    launched = $false
}
if (-not $NoLaunch) {
    $application = Start-Process -FilePath $executable -WorkingDirectory $runtimeDirectory -PassThru
    Start-Sleep -Seconds 2
    $application.Refresh()
    if ($application.HasExited) { throw "Application exited during startup (code $($application.ExitCode))." }
    $actualPath = $application.MainModule.FileName
    if (-not $actualPath.Equals($executable, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected executable path: $actualPath"
    }
    $modules = @($application.Modules | Where-Object { $_.ModuleName -in @('soem_interface.dll', 'yaml-cpp.dll') })
    foreach ($module in $modules) {
        $expectedPath = Join-Path $runtimeDirectory $module.ModuleName
        if (-not $module.FileName.Equals($expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected loaded DLL: $($module.FileName)"
        }
    }
    if (-not ($modules | Where-Object { $_.ModuleName -eq 'soem_interface.dll' })) {
        throw 'The running process did not load the expected SOEM interface library.'
    }
    if ([IO.Path]::GetExtension($runtime.yamlLibrary) -ieq '.dll' -and
        -not ($modules | Where-Object { $_.ModuleName -eq 'yaml-cpp.dll' })) {
        throw 'The running process did not load the expected yaml-cpp library.'
    }
    $evidence.launched = $true
    $evidence.processId = $application.Id
    $evidence.loadedLibraries = @($modules | ForEach-Object { $_.FileName })
}
$evidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $BuildDirectory 'runtime-verification.json') -Encoding UTF8
[pscustomobject]$evidence
