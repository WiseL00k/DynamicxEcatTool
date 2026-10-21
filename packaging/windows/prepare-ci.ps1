[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory)
. (Join-Path $PSScriptRoot 'common.ps1')
$deps = Get-Content (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null

# Fixed upstream commit; shared /MD Release dependency, independent of system vcpkg.
$yamlSource = Join-Path $WorkDirectory 'yaml-source'
$yamlInstall = Join-Path $WorkDirectory 'yaml-install'
if (-not (Test-Path (Join-Path $yamlInstall 'bin/yaml-cpp.dll'))) {
    Invoke-Checked git @('clone', '--no-checkout', 'https://github.com/jbeder/yaml-cpp.git', $yamlSource)
    Invoke-Checked git @('-C', $yamlSource, 'checkout', '--detach', $deps.yamlCpp.commit)
    Invoke-Checked cmake @('-S', $yamlSource, '-B', (Join-Path $WorkDirectory 'yaml-build'), '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$yamlInstall", '-DYAML_BUILD_SHARED_LIBS=ON',
        '-DYAML_CPP_BUILD_TESTS=OFF', '-DYAML_CPP_BUILD_TOOLS=OFF')
    Invoke-Checked cmake @('--build', (Join-Path $WorkDirectory 'yaml-build'), '--parallel', '4')
    Invoke-Checked cmake @('--install', (Join-Path $WorkDirectory 'yaml-build'))
}

$innoInstaller = Join-Path $WorkDirectory 'innosetup.exe'
Get-VerifiedDownload $deps.innoSetup.url $innoInstaller $deps.innoSetup.sha256
$innoDir = Join-Path $WorkDirectory 'inno'
$process = Start-Process -FilePath $innoInstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES',
    '/NORESTART','/SP-', "/DIR=`"$innoDir`"") -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Inno Setup installation failed: $($process.ExitCode)" }
Assert-File (Join-Path $innoDir 'ISCC.exe')

$redist = Join-Path $env:VCToolsRedistDir 'vc_redist.x64.exe'
Assert-File $redist

$npcapInstaller = Join-Path $WorkDirectory "npcap-$($deps.npcap.version).exe"
Get-VerifiedDownload $deps.npcap.url $npcapInstaller $deps.npcap.sha256
$npcapExtract = Join-Path $WorkDirectory 'npcap-extracted'
Invoke-Checked 7z @('x', '-y', "-o$npcapExtract", $npcapInstaller)
$testRuntime = Join-Path $WorkDirectory 'npcap-test-runtime'
New-Item -ItemType Directory -Path $testRuntime -Force | Out-Null
foreach ($name in @('wpcap.dll', 'Packet.dll')) {
    $candidates = @(Get-ChildItem $npcapExtract -Recurse -File -Filter $name |
        Where-Object { (Get-PeMachine $_.FullName) -eq 0x8664 })
    if ($candidates.Count -ne 1) { throw "Expected exactly one x64 $name in Npcap archive; found $($candidates.Count)." }
    Copy-Item -LiteralPath $candidates[0].FullName -Destination $testRuntime
}
@{
    YAML_ROOT = $yamlInstall
    ISCC_PATH = (Join-Path $innoDir 'ISCC.exe')
    VC_REDIST_PATH = $redist
    NPCAP_TEST_RUNTIME = $testRuntime
}.GetEnumerator() | ForEach-Object {
    "$($_.Key)=$($_.Value)" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
}
