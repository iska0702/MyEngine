param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$fixtureRoot = Join-Path $OutputDirectory 'test-content-fixtures'
New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot 'scripts'), (Join-Path $fixtureRoot 'Audio'), (Join-Path $OutputDirectory 'Contents') | Out-Null
[System.IO.File]::WriteAllText((Join-Path $fixtureRoot 'scripts/packed.lua'), 'PACKED_SCRIPT_OK = true', [System.Text.UTF8Encoding]::new($false))
$stream = [System.IO.File]::Create((Join-Path $fixtureRoot 'Audio/packed.wav'))
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([uint32]38)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([uint32]16)
    $writer.Write([uint16]1); $writer.Write([uint16]1); $writer.Write([uint32]44100)
    $writer.Write([uint32]88200); $writer.Write([uint16]2); $writer.Write([uint16]16)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([uint32]2); $writer.Write([int16]0)
} finally { $writer.Dispose(); $stream.Dispose() }
Push-Location $OutputDirectory
try { & (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/Content/pack_all_content.ps1') -SourceContent $fixtureRoot }
finally { Pop-Location }
