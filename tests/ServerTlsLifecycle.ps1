param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture=Join-Path $root 'Diagnostics/TlsFixtures/server.pfx'
if(!(Test-Path -LiteralPath $fixture)){throw 'Run tools/Diagnostics/CheckNetworkSecurity.ps1 first to create test certificates'}
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=(Resolve-Path -LiteralPath $Executable).Path
$start.WorkingDirectory=Split-Path $start.FileName -Parent
$start.Arguments='--port 0 --tls-pfx "'+$fixture+'" --tls-password-env IDK_TLS_PFX_PASSWORD'
$start.Environment['IDK_TLS_PFX_PASSWORD']='regression-only'
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($start)
try {
    $outputTask=$process.StandardOutput.ReadToEndAsync()
    $errorTask=$process.StandardError.ReadToEndAsync()
    $process.StandardInput.WriteLine('status')
    $process.StandardInput.WriteLine('quit')
    $process.StandardInput.Close()
    if(!$process.WaitForExit(10000)){throw 'TLS server did not stop within ten seconds'}
    $output=$outputTask.GetAwaiter().GetResult();$errors=$errorTask.GetAwaiter().GetResult()
    if($process.ExitCode-ne 0-or $output-notmatch 'QUIC\s*/\s*TLS 1\.3'-or !$output.Contains('Server Status')-or !$output.Contains('Server stopped.')){
        throw "TLS server lifecycle failed: $output $errors"
    }
    Write-Output 'PASS: packaged MsQuic runtime, CLI PFX/environment password, TLS 1.3 listener, status and graceful shutdown'
} finally {
    if(!$process.HasExited){$process.Kill();$process.WaitForExit()}
    $process.Dispose()
}
