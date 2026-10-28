# Shared Windows CNG AES-256-GCM implementation, compatible with Windows PowerShell 5.1.
$ErrorActionPreference='Stop'
if(!('IdkFileCrypto' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
public static class IdkFileCrypto {
    [StructLayout(LayoutKind.Sequential)] struct Auth {
        public int size,version;public IntPtr nonce;public int nonceSize;
        public IntPtr aad;public int aadSize;public IntPtr tag;public int tagSize;
        public IntPtr mac;public int macSize,aadCount;public ulong dataCount;public int flags;
    }
    [DllImport("bcrypt.dll",CharSet=CharSet.Unicode)] static extern int BCryptOpenAlgorithmProvider(out IntPtr a,string id,string impl,int flags);
    [DllImport("bcrypt.dll",CharSet=CharSet.Unicode)] static extern int BCryptSetProperty(IntPtr a,string name,byte[] value,int size,int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptGenerateSymmetricKey(IntPtr a,out IntPtr key,IntPtr obj,int objSize,byte[] secret,int size,int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptEncrypt(IntPtr key,byte[] input,int length,ref Auth auth,IntPtr iv,int ivSize,byte[] output,int size,out int written,int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptDecrypt(IntPtr key,byte[] input,int length,ref Auth auth,IntPtr iv,int ivSize,byte[] output,int size,out int written,int flags);
    [DllImport("bcrypt.dll")] static extern int BCryptDestroyKey(IntPtr key);
    [DllImport("bcrypt.dll")] static extern int BCryptCloseAlgorithmProvider(IntPtr a,int flags);
    static void Check(int status){if(status<0)throw new CryptographicException("CNG AES-GCM failed: 0x"+status.ToString("X8"));}
    public static byte[] Random(int count){var bytes=new byte[count];using(var rng=RandomNumberGenerator.Create())rng.GetBytes(bytes);return bytes;}
    public static byte[] Derive(byte[] key,string label){using(var h=new HMACSHA256(key))return h.ComputeHash(Encoding.UTF8.GetBytes(label));}
    public static byte[] Crypt(bool encrypt,byte[] key,byte[] iv,byte[] input,string context){
        if(key.Length!=32||iv.Length!=16||(!encrypt&&input.Length<16))throw new CryptographicException("Invalid AES-GCM record");
        byte[] body,tag=new byte[16];
        if(encrypt)body=input;else{body=new byte[input.Length-16];Buffer.BlockCopy(input,0,tag,0,16);Buffer.BlockCopy(input,16,body,0,body.Length);}
        var label=Encoding.UTF8.GetBytes(context);var aad=new byte[16+label.Length];Buffer.BlockCopy(iv,0,aad,0,16);Buffer.BlockCopy(label,0,aad,16,label.Length);
        var result=new byte[Math.Max(1,body.Length)];IntPtr alg=IntPtr.Zero,k=IntPtr.Zero;
        var noncePin=GCHandle.Alloc(iv,GCHandleType.Pinned);var aadPin=GCHandle.Alloc(aad,GCHandleType.Pinned);var tagPin=GCHandle.Alloc(tag,GCHandleType.Pinned);
        try {
            Check(BCryptOpenAlgorithmProvider(out alg,"AES",null,0));var mode=Encoding.Unicode.GetBytes("ChainingModeGCM\0");Check(BCryptSetProperty(alg,"ChainingMode",mode,mode.Length,0));
            Check(BCryptGenerateSymmetricKey(alg,out k,IntPtr.Zero,0,key,key.Length,0));
            var auth=new Auth{size=Marshal.SizeOf(typeof(Auth)),version=1,nonce=noncePin.AddrOfPinnedObject(),nonceSize=12,aad=aadPin.AddrOfPinnedObject(),aadSize=aad.Length,tag=tagPin.AddrOfPinnedObject(),tagSize=16};int written;
            Check(encrypt?BCryptEncrypt(k,body,body.Length,ref auth,IntPtr.Zero,0,result,result.Length,out written,0):BCryptDecrypt(k,body,body.Length,ref auth,IntPtr.Zero,0,result,result.Length,out written,0));
            if(written!=body.Length)throw new CryptographicException("Invalid AES-GCM output length");
            var output=new byte[written+(encrypt?16:0)];if(encrypt)Buffer.BlockCopy(tag,0,output,0,16);Buffer.BlockCopy(result,0,output,encrypt?16:0,written);return output;
        } finally {Array.Clear(result,0,result.Length);if(k!=IntPtr.Zero)BCryptDestroyKey(k);if(alg!=IntPtr.Zero)BCryptCloseAlgorithmProvider(alg,0);noncePin.Free();aadPin.Free();tagPin.Free();}
    }
}
'@
}
function Get-ContentEncryptionKey {
    $root=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '..'))
    $directory=Join-Path $root '.keys';$file=Join-Path $directory 'content-key.dpapi'
    Add-Type -AssemblyName System.Security
    if(!(Test-Path -LiteralPath $file)) {
        if(Test-Path -LiteralPath (Join-Path $root "src/Storage/GeneratedContentKey.h")){throw "Protected key store missing; restore .keys instead of silently rotating the key"}
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $key=[IdkFileCrypto]::Random(32)
        $protected=[Security.Cryptography.ProtectedData]::Protect($key,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
        $stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.Write($protected,0,$protected.Length)}finally{$stream.Dispose()}
    } else {
        $key=[Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($file),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    }
    if($key.Length-ne 32){throw 'Invalid local content key; refusing to replace it'}
    $header=Join-Path $root 'src/Storage/GeneratedContentKey.h'
    $source="#pragma once`n// Generated local build key. Never commit or distribute this source file.`nnamespace IdkBuildKey { inline constexpr unsigned char bytes[32] = {"+([string]::Join(',',@($key|ForEach-Object {'0x{0:X2}'-f $_})))+"}; }`n"
    if(!(Test-Path -LiteralPath $header)-or [IO.File]::ReadAllText($header)-cne $source){[IO.File]::WriteAllText($header,$source,[Text.UTF8Encoding]::new($false))}
    return ,$key
}
function Encrypt-Data([byte[]]$plainBytes,[byte[]]$key,[string]$Context='index') {
    $iv=[IdkFileCrypto]::Random(16)
    return @{IV=$iv;Cipher=[IdkFileCrypto]::Crypt($true,$key,$iv,$plainBytes,$Context)}
}
function Decrypt-Data([byte[]]$cipherBytes,[byte[]]$iv,[byte[]]$key,[string]$Context='index') {
    return ,[IdkFileCrypto]::Crypt($false,$key,$iv,$cipherBytes,$Context)
}
