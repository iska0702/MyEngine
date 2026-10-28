param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Executable
$start.WorkingDirectory = Split-Path $Executable -Parent
$start.Arguments = '--port 0'
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [System.Diagnostics.Process]::Start($start)
try {
    # Partial pipe input must not prevent the console reader from shutting down.
    $process.StandardInput.Write('sta')
    $process.StandardInput.Flush()
    Start-Sleep -Milliseconds 100
    $process.StandardInput.WriteLine('tus')
    $process.StandardInput.WriteLine('quit')
    $process.StandardInput.Close()
    if (!$process.WaitForExit(5000)) { throw 'Server did not stop within five seconds' }
    $output = $process.StandardOutput.ReadToEnd()
    $errors = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0 -or !$output.Contains('Server Status') -or !$output.Contains('Server stopped.')) {
        throw "Server lifecycle failed: $output $errors"
    }
    Write-Output 'PASS: partial stdin, command queue, EOF, status and graceful shutdown'
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
