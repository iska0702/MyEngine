#include "Storage/ContentFile.h"
#include "Storage/SaveSystem.h"
#include "Core/Project.h"
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static Bytes Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {(std::istreambuf_iterator<char>(file)),{}};}
static void Write(const std::filesystem::path& path,const Bytes& bytes){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());Check(bool(file),"Fixture write failed");}
static uint32_t U32(const Bytes& bytes,size_t offset){uint32_t value=0;Check(offset+4<=bytes.size(),"Fixture bounds");std::memcpy(&value,bytes.data()+offset,4);return value;}
int main(int argc,char** argv){try{
    Check(argc==2,"Fixture directory required");const std::filesystem::path root=argv[1];
    auto fixture=Read(root/"oracle.bin");Check(fixture.size()>52,"Missing independent GCM fixture");
    const size_t length=U32(fixture,48);Check(fixture.size()==52+length+length+16,"Invalid oracle fixture");
    Bytes cipher,plain;
    Check(PakCrypto::EncryptBlock(fixture.data()+16,32,fixture.data(),16,fixture.data()+52,length,cipher,"oracle"),"CNG encrypt failed");
    Check(cipher==Bytes(fixture.begin()+52+length,fixture.end()),"CNG differs from .NET AES-GCM oracle");
    Check(PakCrypto::DecryptBlock(fixture.data()+16,32,fixture.data(),16,cipher.data(),cipher.size(),plain,"oracle")&&plain==Bytes(fixture.begin()+52,fixture.begin()+52+length),"GCM roundtrip failed");
    auto key=PakCrypto::GetMasterKey();uint8_t iv[16]{};Check(PakCrypto::RandomBytes(iv,16),"OS random failed");
    Bytes message(4096,0x63);Check(PakCrypto::EncryptBlock(key.data(),32,iv,16,message.data(),message.size(),cipher,"fixture"),"Encrypt failed");
    auto reject=[&](const Bytes& input,const Bytes& useKey,const uint8_t* nonce,const std::string& context){plain={1,2,3};Check(!PakCrypto::DecryptBlock(useKey.data(),32,nonce,16,input.data(),input.size(),plain,context)&&plain.empty(),"Invalid record published plaintext");};
    auto bad=cipher;bad[0]^=1;reject(bad,key,iv,"fixture");bad=cipher;bad.back()^=1;reject(bad,key,iv,"fixture");
    auto wrongKey=key;wrongKey[0]^=1;reject(cipher,wrongKey,iv,"fixture");reject(cipher,key,iv,"other/file");
    for(unsigned i=0;i<16;++i){iv[i]^=1;reject(cipher,key,iv,"fixture");iv[i]^=1;}
    bad=cipher;bad.pop_back();reject(bad,key,iv,"fixture");
    Check(PakCrypto::EncryptBlock(key.data(),32,iv,16,nullptr,0,cipher,"empty")&&PakCrypto::DecryptBlock(key.data(),32,iv,16,cipher.data(),cipher.size(),plain,"empty")&&plain.empty(),"Empty record failed");
    const auto contents=root/"Contents";
    Check(EngineStorage::ReadContentArchive(u"Content/A.txt",contents)==Read(root/"Content/A.txt"),"Authenticated archive read failed");
    auto volume=Read(contents/"content01.pak"),original=volume;Check(U32(volume,0)==0x334B5045,"Old archive format emitted");
    volume[28]^=1;Write(contents/"content01.pak",volume);Check(EngineStorage::ReadContentArchive(u"Content/A.txt",contents).empty(),"Tampered index accepted");
    volume=original;volume.back()^=1;Write(contents/"content01.pak",volume);Check(EngineStorage::ReadContentArchive(u"Content/A.txt",contents).empty(),"Tampered payload accepted");Write(contents/"content01.pak",original);
    auto other=Read(contents/"content02.pak"),originalOther=other;
    const size_t startA=28+U32(original,20),startB=28+U32(other,20);
    Check(original.size()-startA==other.size()-startB,"Swap fixture lengths differ");
    std::copy(original.begin()+startA,original.end(),other.begin()+startB);Write(contents/"content02.pak",other);
    Check(EngineStorage::ReadContentArchive(u"Content/B.txt",contents).empty(),"Payload swapped between file names accepted");Write(contents/"content02.pak",originalOther);
    EngineProject::active=true;EngineProject::current.file=root/"Test.idkp";EngineProject::current.editorScene="Saved/Regression.save";
    auto& saves=EngineSave::SaveManager::Get();EngineSave::SavePackage loaded;
    Check(saves.SaveGame("MODEL SecurityFixture\n","[Window][Fixture]\n"),"Authenticated save failed");
    Check(saves.LoadGame(loaded)&&loaded.sceneData.find("SecurityFixture")!=std::string::npos,"Authenticated save read failed");
    const auto save=root/"Saved/Regression.save";auto saved=Read(save);Check(U32(saved,4)==2,"Old save format emitted");
    for(size_t offset:{size_t(4),size_t(8),size_t(24),size_t(28),size_t(32),size_t(36),saved.size()-1}){
        auto corrupted=saved;corrupted[offset]^=0x40;Write(save,corrupted);loaded.sceneData="unchanged";
        Check(!saves.LoadGame(loaded)&&loaded.sceneData=="unchanged","Tampered save changed scene state");
    }
    auto extra=saved;extra.push_back(1);Write(save,extra);Check(!saves.LoadGame(loaded),"Trailing save bytes accepted");Write(save,saved);
    EngineProject::current.editorScene="Saved/Legacy.save";
    Check(saves.LoadGame(loaded)&&loaded.sceneData.find("LegacyFixture")!=std::string::npos,"Legacy editor scene migration read failed");
    Check(saves.SaveGame(loaded.sceneData,loaded.imguiSettings)&&U32(Read(root/"Saved/Legacy.save"),4)==2,"Legacy save was not upgraded");
    std::cout<<"PASS: independent AES-256-GCM oracle, wrong key/tag/nonce/context/truncation rejection, no plaintext on failure, authenticated PAK index/payload, cross-file swap rejection, SAVE v2 metadata/payload checks, editor legacy migration\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
