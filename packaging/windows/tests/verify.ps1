[CmdletBinding()]
param([Parameter(Mandatory)][string]$IsccPath, [Parameter(Mandatory)][string]$WorkDirectory)
. (Join-Path $PSScriptRoot '../common.ps1')
$WorkDirectory = [IO.Path]::GetFullPath($WorkDirectory)
New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null

function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $caught = $false
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $caught = $true
    }
    if (-not $caught) { throw "Expected failure matching $Pattern" }
}

Get-ChildItem (Join-Path $PSScriptRoot '..') -Filter '*.ps1' -Recurse | ForEach-Object {
    $tokens = $null; $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
}
Assert-Version '0.4.0'
foreach ($bad in @('v0.4.0', '0.4', '../0.4.0', '65536.0.0', '01.0.0')) {
    Assert-Throws { Assert-Version $bad } 'Version must'
}
Assert-Throws { Assert-File (Join-Path $WorkDirectory 'absent.dll') } 'Required file not found'
$shell = (Get-Process -Id $PID).Path
Assert-Throws { Invoke-Checked $shell @('-NoProfile', '-Command', 'exit 7') } 'exit code 7'
$sample = Join-Path $WorkDirectory 'hash-test.txt'
'original' | Set-Content $sample -Encoding ASCII
$hash = (Get-FileHash $sample).Hash
Assert-Sha256 $sample $hash
'changed' | Set-Content $sample -Encoding ASCII
Assert-Throws { Assert-Sha256 $sample $hash } 'SHA256 verification failed'
Assert-Throws { Get-VerifiedDownload 'http://example.invalid/file' $sample $hash } 'HTTPS'
# Cached files must still be verified, never reused on name alone.
Assert-Throws { Get-VerifiedDownload 'https://example.invalid/file' $sample $hash } 'SHA256 verification failed'

Invoke-Checked $IsccPath @('/Q', "/DTestOutput=$WorkDirectory", (Join-Path $PSScriptRoot 'npcap-state-tests.iss'))
$report = Join-Path $WorkDirectory ('state-result-' + [guid]::NewGuid().ToString('N') + '.txt')
$testExe = Join-Path $WorkDirectory 'npcap-state-tests.exe'
$p = Start-Process -FilePath $testExe -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
    "/Report=`"$report`"", "/LOG=`"$WorkDirectory/state-tests.log`"") -WindowStyle Hidden -PassThru
if (-not $p.WaitForExit(60000)) { $p.Kill(); throw 'Npcap state harness timed out.' }
Assert-File $report
if ((Get-Content $report -Raw) -ne 'PASS 8') { throw (Get-Content $report -Raw) }
Write-Host 'PASS: script validation and 8 optional embedded Npcap installer scenarios (no driver installed).'
