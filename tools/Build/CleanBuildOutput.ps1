param([Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..')).TrimEnd('\')
$target = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\')
if ($target -notin @((Join-Path $workspace 'EditorBuild'), (Join-Path $workspace '.ProjectBuild'))) {
    throw "Only this workspace's editor and project staging build can be cleaned."
}
if (!(Test-Path -LiteralPath $target -PathType Container)) { return }
$archive = Join-Path $workspace ('Diagnostics\Archive\' + (Split-Path $target -Leaf))
New-Item -ItemType Directory -Path $archive -Force | Out-Null
& (Join-Path (Split-Path $PSScriptRoot -Parent) 'Build/RemoveBuildObjects.ps1')
foreach ($file in Get-ChildItem -LiteralPath $target -File) {
    if ($file.Extension -in @('.log','.csv','.bmp','.png','.exp','.lib') -or
              $file.Name -match '^(benchmark.*|graphics_.*_world|build_log|startup_check|log)\.txt$') {
        $destination = Join-Path $archive $file.Name
        if (Test-Path -LiteralPath $destination) {
            $destination = Join-Path $archive ($file.BaseName + '-' + [Guid]::NewGuid().ToString('N') + $file.Extension)
        }
        Move-Item -LiteralPath $file.FullName -Destination $destination
    }
}
Write-Output "Build diagnostics archived in $archive"
