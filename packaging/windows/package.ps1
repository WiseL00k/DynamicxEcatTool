#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$IsccPath,
    [Parameter(Mandatory)][string]$VcRedistPath,
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '../..'),
    [string]$Version,
    [switch]$CheckOnly
)
. (Join-Path $PSScriptRoot 'common.ps1')
$savedPath = $env:PATH
$transcribing = $false
try {
    $SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
    $BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    $IsccPath = [IO.Path]::GetFullPath($IsccPath)
    $VcRedistPath = [IO.Path]::GetFullPath($VcRedistPath)
    if (-not $Version) { $Version = (Get-Content (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim() }
    Assert-Version $Version
    $deps = Get-Content (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
    $runtimePath = Join-Path $BuildDirectory 'local-runtime-Release.json'
    Assert-File $runtimePath
    $runtime = Get-Content -LiteralPath $runtimePath -Raw | ConvertFrom-Json
    if ([IO.Path]::GetFullPath($runtime.sourceDirectory).TrimEnd('\') -ine $SourceDirectory.TrimEnd('\')) {
        throw 'Build manifest belongs to a different source checkout.'
    }
    $exe = $runtime.executable
    $binaries = @($exe, $runtime.soemLibrary, $runtime.yamlLibrary)
    foreach ($binary in $binaries) { Assert-Amd64 $binary }
    $qtBin = $runtime.qtBinDirectory
    $deployTool = Join-Path $qtBin 'windeployqt.exe'
    $qmlDirectory = Join-Path $SourceDirectory 'qml'
    $assets = Join-Path $PSScriptRoot 'assets'
    foreach ($file in @($deployTool, (Join-Path $qtBin 'qtpaths.exe'), $IsccPath, $VcRedistPath,
        (Join-Path $qmlDirectory 'Main.qml'), (Join-Path $assets 'dynamicx_logo.ico'),
        (Join-Path $assets 'header.bmp'), (Join-Path $SourceDirectory 'config/esi/ECAT2CANFD.xml'))) {
        Assert-File $file
    }
    $redistInfo = (Get-Item -LiteralPath $VcRedistPath).VersionInfo
    if ($redistInfo.ProductName -notmatch 'Microsoft Visual C\+\+.*\(x64\)' -or
        [version]$redistInfo.FileVersion -lt [version]'14.40.0.0') {
        throw 'An official VC++ v14 x64 redistributable version 14.40 or newer is required.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $VcRedistPath
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw 'VC++ redistributable signature verification failed.'
    }
    if ($CheckOnly) { Write-Host "Packaging preflight passed for $Version"; return }
    if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh OutputDirectory; existing output is never overwritten.' }
    $stage = Join-Path $OutputDirectory 'app'
    $publish = Join-Path $OutputDirectory 'publish'
    New-Item -ItemType Directory -Path $stage, $publish | Out-Null
    Start-Transcript -Path (Join-Path $OutputDirectory 'package.log') | Out-Null
    $transcribing = $true
    foreach ($binary in $binaries) { Copy-Item -LiteralPath $binary -Destination $stage }
    Copy-Item -LiteralPath (Join-Path $SourceDirectory 'config/esi') -Destination (Join-Path $stage 'esi') -Recurse
    Copy-Item -LiteralPath (Join-Path $SourceDirectory 'config/sample_config') -Destination (Join-Path $stage 'sample_config') -Recurse
    $env:PATH = "$qtBin;$env:PATH"
    Invoke-Checked $deployTool @('--release', '--verbose', '1', '--no-compiler-runtime',
        '--qmldir', $qmlDirectory, '--qtpaths', (Join-Path $qtBin 'qtpaths.exe'),
        (Join-Path $stage 'DynamicxEcatTool.exe'))
    foreach ($file in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll',
        'platforms/qwindows.dll', 'imageformats/qsvg.dll', 'qml/QtQuick/Controls/qmldir', 'D3Dcompiler_47.dll')) {
        Assert-File (Join-Path $stage $file)
    }
    # Qt 6.8 defaults to D3D11. Supply the SDK's paired DXC libraries as well,
    # rather than silently accepting windeployqt's missing-DXC warning.
    if (-not (Test-Path (Join-Path $stage 'dxcompiler.dll')) -or -not (Test-Path (Join-Path $stage 'dxil.dll'))) {
        $sdkBin = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
        $dxc = Get-ChildItem -LiteralPath $sdkBin -Filter dxcompiler.dll -Recurse -File |
            Where-Object { $_.Directory.Name -eq 'x64' -and (Test-Path (Join-Path $_.DirectoryName 'dxil.dll')) } |
            Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $dxc) { throw 'The Windows SDK x64 dxcompiler.dll/dxil.dll pair is required.' }
        foreach ($name in @('dxcompiler.dll','dxil.dll')) {
            $library = Join-Path $dxc.DirectoryName $name
            Assert-Amd64 $library
            Copy-Item -LiteralPath $library -Destination $stage -Force
        }
    }
    foreach ($binary in $binaries) {
        Assert-Sha256 (Join-Path $stage ([IO.Path]::GetFileName($binary))) (Get-FileHash $binary).Hash
    }
    # The staging directory is an allowlisted copy, not the build directory.
    if (@(Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -match '^(npcap.*\.exe|wpcap\.dll|Packet\.dll)$' }).Count) {
        throw 'Npcap binaries must never be embedded in the installer.'
    }
    Invoke-Checked $IsccPath @('/Qp', "/DAppVersion=$Version", "/DAppSource=$stage",
        "/DReleaseOutput=$publish", "/DAssetSource=$assets", "/DVcRedistPath=$VcRedistPath",
        "/DNpcapVersion=$($deps.npcap.version)", "/DNpcapUrl=$($deps.npcap.url)",
        "/DNpcapSHA256=$($deps.npcap.sha256)", (Join-Path $PSScriptRoot 'installer.iss'))
    $name = "DynamicxEcatTool-v$Version-amd64.exe"
    $installer = Join-Path $publish $name
    Assert-File $installer
    $versionInfo = (Get-Item $installer).VersionInfo
    if ($versionInfo.ProductVersion.Trim() -ne $Version -or $versionInfo.FileVersion.Trim() -ne "$Version.0") {
        throw 'Compiled installer version does not match packaging/VERSION.'
    }
    $hash = (Get-FileHash $installer).Hash.ToLowerInvariant()
    "$hash  $name" | Set-Content -LiteralPath "$installer.sha256" -Encoding ASCII
    $commit = & git -C $SourceDirectory rev-parse HEAD
    if ($LASTEXITCODE) { throw 'Cannot identify source commit.' }
    $toolVersion = & $deployTool --version
    $payload = @(Get-ChildItem $stage -Recurse -File | ForEach-Object {
        [ordered]@{path = $_.FullName.Substring($stage.Length + 1).Replace('\','/'); sha256 = (Get-FileHash $_.FullName).Hash.ToLowerInvariant()}
    })
    [ordered]@{
        version = $Version; architecture = 'amd64'; commit = $commit; headCommit = $env:GITHUB_HEAD_SHA
        runId = $env:GITHUB_RUN_ID; createdUtc = [DateTime]::UtcNow.ToString('o')
        installer = $name; installerSha256 = $hash; qt = "$toolVersion"
        innoSetup = (Get-Item $IsccPath).VersionInfo.FileVersion; vcRedist = $redistInfo.FileVersion
        cmake = (& cmake --version | Select-Object -First 1); msvc = $env:VCToolsVersion
        dependencies = $deps; payload = $payload
    } | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $publish 'build-info.json') -Encoding UTF8
    if ($env:GITHUB_OUTPUT) {
        "version=$Version" | Out-File $env:GITHUB_OUTPUT -Append -Encoding utf8
        "publish=$publish" | Out-File $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
    Write-Host "Installer ready: $installer"
} finally {
    $env:PATH = $savedPath
    if ($transcribing) { Stop-Transcript | Out-Null }
}
