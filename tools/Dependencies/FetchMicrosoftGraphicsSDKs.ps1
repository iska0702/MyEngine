$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$microsoft = Join-Path $root 'ThirdParty/Microsoft'
$version = '1.0.4314.35'
$target = Join-Path $microsoft "WebView2-$version"
if (!(Test-Path (Join-Path $target 'build/native/include/WebView2.h'))) {
    if (Test-Path -LiteralPath $target) { throw "Incomplete WebView2 installation: $target" }
    $archive = Join-Path ([IO.Path]::GetTempPath()) ("idk-webview-" + [guid]::NewGuid().ToString('N') + '.zip')
    try {
        Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile $archive
        if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4') { throw 'WebView2 package hash mismatch' }
        Expand-Archive -LiteralPath $archive -DestinationPath $target
        if ((Get-AuthenticodeSignature (Join-Path $target 'build/native/x64/WebView2Loader.dll')).Status -ne 'Valid') { throw 'WebView2 DLL signature is not valid' }
    } finally { if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force } }
}
$headers = Join-Path $microsoft 'DirectX-Headers-1.620.1'
if (!(Test-Path (Join-Path $headers 'include/directx/d3dx12.h'))) {
    if (Test-Path -LiteralPath $headers) { throw "Incomplete DirectX-Headers installation: $headers" }
    & git clone --depth 1 --branch v1.620.1 https://github.com/microsoft/DirectX-Headers.git $headers
    if ($LASTEXITCODE -ne 0) { throw 'DirectX-Headers download failed' }
    $commit = (& git -C $headers rev-parse HEAD).Trim()
    if ($commit -ne 'a2b1aa9f89c8cf887ae22c9ad7ccf600e5bbd62c') { throw "Unexpected DirectX-Headers commit: $commit" }
}
Write-Output 'Microsoft WebView2 1.0.4314.35 and DirectX-Headers 1.620.1 are installed.'
