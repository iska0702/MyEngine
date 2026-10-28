param([ValidateSet('EditorBuild','ReleaseBuild')][string]$Build='EditorBuild',
      [Parameter(Mandatory=$true)][string[]]$Sources,
      [ValidateSet('steam','standalone')][string]$Platform='steam')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
$target=if($Build -eq 'EditorBuild'){'editor'}else{'release'}
Write-Output 'Object caching is disabled; running a full build.'
& (Join-Path $root 'build.bat') $target $Platform
if($LASTEXITCODE -ne 0){throw 'Build failed'}
