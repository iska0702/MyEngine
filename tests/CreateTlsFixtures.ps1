param([string]$Directory=(Join-Path $PSScriptRoot '../Diagnostics/TlsFixtures'))
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $Directory | Out-Null
$now=[DateTimeOffset]::UtcNow
$caKey=[Security.Cryptography.RSA]::Create(2048)
$caRequest=[Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=Idontknow isolated regression CA',$caKey,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
$caRequest.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($true,$false,0,$true))
$caRequest.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyCertSign,$true))
$ca=$caRequest.CreateSelfSigned($now.AddDays(-30),$now.AddDays(30))
[IO.File]::WriteAllBytes((Join-Path $Directory 'ca.der'),$ca.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
$der=$ca.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert)
$pem="-----BEGIN CERTIFICATE-----`n"+[Convert]::ToBase64String($der,[Base64FormattingOptions]::InsertLineBreaks)+"`n-----END CERTIFICATE-----`n"
[IO.File]::WriteAllText((Join-Path $Directory 'ca.pem'),$pem,[Text.Encoding]::ASCII)
foreach($expired in @($false,$true)){
    $key=[Security.Cryptography.RSA]::Create(2048)
    $request=[Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=localhost',$key,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $san=[Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new();$san.AddDnsName('localhost');$request.CertificateExtensions.Add($san.Build())
    $oids=[Security.Cryptography.OidCollection]::new();$oids.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids,$true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true))
    $serial=[byte[]]::new(16);[Security.Cryptography.RandomNumberGenerator]::Fill($serial)
    $until=if($expired){$now.AddDays(-1)}else{$now.AddDays(7)}
    $cert=$request.Create($ca,$now.AddDays(-10),$until,$serial)
    $withKey=[Security.Cryptography.X509Certificates.RSACertificateExtensions]::CopyWithPrivateKey($cert,$key)
    $name=if($expired){'expired.pfx'}else{'server.pfx'}
    [IO.File]::WriteAllBytes((Join-Path $Directory $name),$withKey.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx,'regression-only'))
    $withKey.Dispose();$cert.Dispose();$key.Dispose()
}
$ca.Dispose();$caKey.Dispose()
Write-Output 'Created ephemeral regression certificates; system certificate stores were not changed.'
