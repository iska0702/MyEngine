#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#elif defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#endif

#include <vector>
#include <cstdint>
#include <string>
#include <cstring>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <climits>
#if defined(_WIN32)
#include "GeneratedContentKey.h"
#endif

class PakCrypto {
public:
    // Read-only migration key for pre-authentication SAVE v1 files.
    static std::vector<uint8_t> GetLegacyKey() {
        std::vector<uint8_t> key(32);
        const uint64_t w0 = 0x656E69676E45794DULL; // "Idontknow"
        const uint64_t w1 = 0x6553363532534541ULL; // "AES256Se"
        const uint64_t w2 = 0x6B63615065727563ULL; // "curePack"
        const uint64_t w3 = 0x2423402136323032ULL; // "2026!@#$"
        memcpy(key.data() + 0, &w0, 8);
        memcpy(key.data() + 8, &w1, 8);
        memcpy(key.data() + 16, &w2, 8);
        memcpy(key.data() + 24, &w3, 8);
        return key;
    }


    static std::vector<uint8_t> GetMasterKey() {
#if defined(_WIN32)
        return {IdkBuildKey::bytes, IdkBuildKey::bytes + 32};
#else
        return {};
#endif
    }
    static bool RandomBytes(void* output,size_t bytes) {
#if defined(_WIN32)
        return output&&bytes<=ULONG_MAX&&BCryptGenRandom(nullptr,static_cast<PUCHAR>(output),static_cast<ULONG>(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
#else
        return false;
#endif
    }
    static bool Gcm(bool encrypt,const uint8_t* key,size_t keySize,const uint8_t* iv,size_t ivSize,
                    const uint8_t* input,size_t inputSize,std::vector<uint8_t>& output,const std::string& context) {
        output.clear();
        if(!key||keySize!=32||!iv||ivSize!=16||(!input&&inputSize)||(!encrypt&&inputSize<16)||inputSize>512ull*1024*1024)return false;
#if defined(_WIN32)
        BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_KEY_HANDLE handle=nullptr;
        if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_AES_ALGORITHM,nullptr,0)<0)return false;
        bool ok=false;
        if(BCryptSetProperty(alg,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0&&
           BCryptGenerateSymmetricKey(alg,&handle,nullptr,0,const_cast<PUCHAR>(key),32,0)>=0) {
            std::vector<uint8_t> aad(iv,iv+16);aad.insert(aad.end(),context.begin(),context.end());
            uint8_t tag[16]{};size_t bytes=inputSize;
            if(!encrypt){std::memcpy(tag,input,16);input+=16;bytes-=16;}
            std::vector<uint8_t> buffer((std::max)(size_t(1),bytes));
            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;BCRYPT_INIT_AUTH_MODE_INFO(auth);
            auth.pbNonce=const_cast<PUCHAR>(iv);auth.cbNonce=12;auth.pbAuthData=aad.data();auth.cbAuthData=static_cast<ULONG>(aad.size());auth.pbTag=tag;auth.cbTag=16;
            ULONG written=0;
            auto status=encrypt?BCryptEncrypt(handle,const_cast<PUCHAR>(input),static_cast<ULONG>(bytes),&auth,nullptr,0,buffer.data(),static_cast<ULONG>(buffer.size()),&written,0):
                                BCryptDecrypt(handle,const_cast<PUCHAR>(input),static_cast<ULONG>(bytes),&auth,nullptr,0,buffer.data(),static_cast<ULONG>(buffer.size()),&written,0);
            ok=status>=0&&written==bytes;
            if(ok){if(encrypt)output.assign(tag,tag+16);output.insert(output.end(),buffer.begin(),buffer.begin()+written);}
            SecureZeroMemory(buffer.data(),buffer.size());
        }
        if(handle)BCryptDestroyKey(handle);BCryptCloseAlgorithmProvider(alg,0);return ok;
#else
        return false; // Never substitute an unauthenticated cipher on unsupported platforms.
#endif
    }
    static bool EncryptBlock(const uint8_t* key,size_t keySize,const uint8_t* iv,size_t ivSize,const uint8_t* input,size_t size,std::vector<uint8_t>& output,const std::string& context="index") {
        return Gcm(true,key,keySize,iv,ivSize,input,size,output,context);
    }
    static bool DecryptBlock(const uint8_t* key,size_t keySize,const uint8_t* iv,size_t ivSize,const uint8_t* input,size_t size,std::vector<uint8_t>& output,const std::string& context="index") {
        return Gcm(false,key,keySize,iv,ivSize,input,size,output,context);
    }
    static std::string FileContext(const std::u16string& name) {
        const auto utf8=std::filesystem::path(name).u8string();return "file/"+std::string(utf8.begin(),utf8.end());
    }
    static std::vector<uint8_t> GetSigningKey() {
        std::vector<uint8_t> result(32);
#if defined(_WIN32)
        BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;auto key=GetMasterKey();
        const char label[]="Idontknow/file-signing/v1";
        bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0;
        if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,key.data(),32,0)>=0;
        if(ok)ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(label)),sizeof(label)-1,0)>=0;
        if(ok)ok=BCryptFinishHash(hash,result.data(),32,0)>=0;
        if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
        SecureZeroMemory(key.data(),key.size());if(!ok)return {};
#else
        return {};
#endif
        return result;
    }

    static bool DecryptAES256CBC(const uint8_t* keyData, size_t keyLen,
                                 const uint8_t* ivData, size_t ivLen,
                                 const uint8_t* cipherData, size_t cipherLen,
                                 std::vector<uint8_t>& outPlain) {
        outPlain.clear();
        if (!keyData || keyLen != 32 || !ivData || ivLen != 16 || !cipherData || cipherLen == 0 || cipherLen % 16 != 0 || cipherLen > 60ull*1024*1024) {
            return false;
        }

#if defined(_WIN32)
        BCRYPT_ALG_HANDLE hAlg = NULL;
        BCRYPT_KEY_HANDLE hKey = NULL;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
        if (!NT_SUCCESS(status)) return false;

        status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        if (!NT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        DWORD cbKeyObject = 0;
        DWORD cbData = 0;
        status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbKeyObject, sizeof(DWORD), &cbData, 0);
        if (!NT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        std::vector<uint8_t> keyObject(cbKeyObject);
        status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject.data(), cbKeyObject, (PUCHAR)keyData, (ULONG)keyLen, 0);
        if (!NT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        std::vector<uint8_t> ivCopy(ivData, ivData + ivLen);

        ULONG cbResult = 0;
        status = BCryptDecrypt(hKey, (PUCHAR)cipherData, (ULONG)cipherLen, NULL, ivCopy.data(), (ULONG)ivCopy.size(), NULL, 0, &cbResult, BCRYPT_BLOCK_PADDING);
        if (!NT_SUCCESS(status) || cbResult == 0) {
            BCryptDestroyKey(hKey);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return false;
        }

        outPlain.resize(cbResult);
        status = BCryptDecrypt(hKey, (PUCHAR)cipherData, (ULONG)cipherLen, NULL, ivCopy.data(), (ULONG)ivCopy.size(), outPlain.data(), cbResult, &cbResult, BCRYPT_BLOCK_PADDING);
        if(NT_SUCCESS(status))outPlain.resize(cbResult);else{SecureZeroMemory(outPlain.data(),outPlain.size());outPlain.clear();}

        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return NT_SUCCESS(status);

#elif defined(__APPLE__)
        outPlain.resize(cipherLen);
        size_t numBytesDecrypted = 0;
        CCCryptorStatus status = CCCrypt(
            kCCDecrypt,
            kCCAlgorithmAES,
            kCCOptionPKCS7Padding,
            keyData,
            keyLen,
            ivData,
            cipherData,
            cipherLen,
            outPlain.data(),
            outPlain.size(),
            &numBytesDecrypted
        );

        if (status == kCCSuccess) {
            outPlain.resize(numBytesDecrypted);
            return true;
        }
        return false;
#else
        return false;
#endif
    }

};
