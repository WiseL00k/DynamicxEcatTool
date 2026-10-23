Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file not found: $Path" }
}

function Get-PeMachine([string]$Path) {
    Assert-File $Path
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file: $Path" }
        $reader.BaseStream.Position = 0x3C
        $offset = $reader.ReadInt32()
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $Path" }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}

function Assert-Amd64([string]$Path) {
    if ((Get-PeMachine $Path) -ne 0x8664) { throw "Expected amd64 binary: $Path" }
}

function Assert-Version([string]$Version) {
    if ($Version -notmatch '^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$' -or
        @($Version.Split('.') | Where-Object { [int]$_ -gt 65535 }).Count) {
        throw 'Version must be major.minor.patch, each in 0..65535 (without v).'
    }
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    Assert-File $Path
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Expected) {
        throw "SHA256 verification failed: $Path"
    }
}

function Get-VerifiedDownload([string]$Url, [string]$Destination, [string]$Sha256) {
    if (-not $Url.StartsWith('https://')) { throw 'Dependency downloads must use HTTPS.' }
    if (Test-Path -LiteralPath $Destination) {
        Assert-Sha256 $Destination $Sha256
        return
    }
    $partial = "$Destination.partial"
    Invoke-WebRequest -Uri $Url -OutFile $partial -UseBasicParsing -TimeoutSec 180
    Assert-Sha256 $partial $Sha256
    Move-Item -LiteralPath $partial -Destination $Destination
}
