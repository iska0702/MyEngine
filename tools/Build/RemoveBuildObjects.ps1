$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..')).TrimEnd('\')
$removed = 0
foreach ($file in Get-ChildItem -LiteralPath $workspace -Recurse -File -Filter '*.obj') {
    $path = [IO.Path]::GetFullPath($file.FullName)
    if (!$path.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Object outside workspace' }
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
    # Wavefront meshes share this extension. Delete only recognized COFF objects.
    $stream = [IO.File]::OpenRead($path)
    try {
        $header = New-Object byte[] 4
        $count = $stream.Read($header,0,4)
    } finally { $stream.Dispose() }
    if ($count -lt 4) { continue }
    $machine = [BitConverter]::ToUInt16($header,0)
    $bigObject = $machine -eq 0 -and [BitConverter]::ToUInt16($header,2) -eq 65535
    if ($machine -in @(0x8664,0x14c,0xaa64,0x1c4) -or $bigObject) {
        Remove-Item -LiteralPath $path -Force
        $removed++
    }
}
Write-Output "Deleted $removed compiler object files; Wavefront meshes preserved."
