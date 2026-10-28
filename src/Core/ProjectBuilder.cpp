#include "ProjectBuilder.h"
#if defined(_WIN32) && defined(ENGINE_EDITOR)
#include "Project.h"
#include "EngineLog.h"
#include "Storage/PakWriter.h"
#include "Storage/ContentFile.h"
#include "Storage/StreamingTexture.h"
#include "Storage/ArchiveAuthentication.h"
#include <windows.h>
#include <bcrypt.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>
#include <set>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>
#include <array>
#include <ctime>
#include <cctype>
#pragma comment(lib,"bcrypt.lib")
namespace EngineProject {
namespace {
using Bytes=std::vector<uint8_t>;
using Asset=EngineStorage::BuildAsset;
using Checkpoint=std::function<void()>;
struct Cancelled:std::exception {const char* what()const noexcept override{return "Build cancelled";}};
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
fs::path ExeDirectory(){wchar_t path[32768]{};auto size=GetModuleFileNameW(nullptr,path,32768);if(!size||size>=32768)throw std::runtime_error("Cannot locate engine installation");return fs::path(path).parent_path();}
void RequirePlatform(const std::string& platform){if(platform!="standalone"&&platform!="steam"&&platform!="epic")throw std::runtime_error("Unsupported build platform");}
std::string Hex(const uint8_t* data,size_t size){static const char digits[]="0123456789abcdef";std::string result;result.reserve(size*2);for(size_t i=0;i<size;++i){result+=digits[data[i]>>4];result+=digits[data[i]&15];}return result;}
std::string NewId(){uint8_t bytes[16];if(!PakCrypto::RandomBytes(bytes,16))throw std::runtime_error("Cannot generate build ID");return Hex(bytes,16);}
Bytes Text(const std::string& text){return {text.begin(),text.end()};}
void Write(const fs::path& path,const Bytes& data){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.flush();if(!out)throw std::runtime_error("Cannot write build file: "+Utf8(path));}
void RejectLink(const fs::path& path){auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked files or directories cannot be build inputs: "+Utf8(path));}
fs::path Child(const fs::path& root,const std::string& relative){Project boundary;boundary.file=root/"Boundary.idkp";return boundary.Resolve(relative);}
void RemoveOwnedStage(const fs::path& path,const fs::path& parent){if(fs::absolute(path).lexically_normal().parent_path()!=fs::absolute(parent).lexically_normal())throw std::runtime_error("Invalid build staging path");RejectLink(path);fs::remove_all(path);}
struct Sha256 {
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    Sha256(){if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 provider unavailable");if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);alg=nullptr;throw std::runtime_error("SHA-256 initialization failed");}}
    ~Sha256(){if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);}
    void Add(const void* data,size_t size){if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<void*>(data)),static_cast<ULONG>(size),0)<0)throw std::runtime_error("SHA-256 update failed");}
    std::string Finish(){uint8_t bytes[32];if(BCryptFinishHash(hash,bytes,32,0)<0)throw std::runtime_error("SHA-256 finish failed");return Hex(bytes,32);}
};
std::string CopyHash(const fs::path& source,const fs::path& destination,const Checkpoint& checkpoint={}){
    RejectLink(source);std::ifstream input(source,std::ios::binary);if(!input)throw std::runtime_error("Missing build runtime file: "+Utf8(source));
    std::ofstream output;if(!destination.empty()){fs::create_directories(destination.parent_path());output.open(destination,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Cannot copy runtime file");}
    Sha256 hash;char buffer[65536];
    while(input){if(checkpoint)checkpoint();input.read(buffer,sizeof(buffer));auto count=input.gcount();if(count){hash.Add(buffer,static_cast<size_t>(count));if(!destination.empty())output.write(buffer,count);}}
    if(!input.eof()||(!destination.empty()&&!output))throw std::runtime_error("Runtime file copy failed");
    if(!destination.empty()){output.flush();if(!output)throw std::runtime_error("Cannot flush runtime file");}
    return hash.Finish();
}
Bytes Hmac(const Bytes& data){auto key=PakCrypto::GetSigningKey();if(key.size()!=32)throw std::runtime_error("Signing key unavailable");BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;Bytes result(32);
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0;
    if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,key.data(),32,0)>=0;if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),0)>=0;if(ok)ok=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);SecureZeroMemory(key.data(),key.size());if(!ok)throw std::runtime_error("Cannot authenticate build metadata");return result;
}
std::vector<fs::path> Files(const fs::path& directory){RejectLink(directory);std::vector<fs::path> result;for(auto& entry:fs::recursive_directory_iterator(directory)){RejectLink(entry.path());if(entry.is_regular_file())result.push_back(entry.path());}std::sort(result.begin(),result.end());return result;}
std::vector<std::u16string> ArchiveNames(const fs::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};auto length=file.tellg();if(length<48)throw std::runtime_error("Runtime shader archive is truncated");file.seekg(0);
    uint32_t magic=0,size=0,plainSize=0;uint8_t iv[16]{};file.read(reinterpret_cast<char*>(&magic),4);file.read(reinterpret_cast<char*>(iv),16);file.read(reinterpret_cast<char*>(&size),4);file.read(reinterpret_cast<char*>(&plainSize),4);
    if(magic!=0x334B5045||size<20||size>20*1024*1024||size!=uint64_t(plainSize)+16||uint64_t(size)+28>uint64_t(length))throw std::runtime_error("Runtime shader archive has invalid bounds");
    Bytes cipher(size),plain;file.read(reinterpret_cast<char*>(cipher.data()),size);auto key=PakCrypto::GetMasterKey();
    if(!file||!PakCrypto::DecryptBlock(key.data(),key.size(),iv,16,cipher.data(),cipher.size(),plain)||plain.size()!=plainSize)throw std::runtime_error("Runtime shader archive failed authentication");
    uint32_t count=0;std::memcpy(&count,plain.data(),4);if(count>65536||plain.size()!=4+uint64_t(count)*276)throw std::runtime_error("Invalid runtime shader index");
    std::vector<std::u16string> result;for(uint32_t i=0;i<count;++i){char16_t name[129]{};std::memcpy(name,plain.data()+4+size_t(i)*276,256);std::u16string keyName(name);while(!keyName.empty()&&keyName.back()==u' ')keyName.pop_back();std::replace(keyName.begin(),keyName.end(),u'\\',u'/');result.push_back(std::move(keyName));}return result;
}
struct Runtime {fs::path root;std::map<std::string,std::string> files;};
Runtime LoadRuntime(const std::string& platform){
    Runtime runtime;runtime.root=ProjectBuilder::SupportRoot()/platform;
    const auto record=EngineStorage::ReadBuildInput(runtime.root/"runtime.idkr");if(record.size()<40)throw std::runtime_error("Runtime template is incomplete");
    uint32_t magic=0,size=0;std::memcpy(&magic,record.data(),4);std::memcpy(&size,record.data()+20,4);
    if(magic!=0x52544B49||size!=record.size()-24)throw std::runtime_error("Invalid runtime template metadata");
    auto key=PakCrypto::GetMasterKey();Bytes plain;if(!PakCrypto::DecryptBlock(key.data(),key.size(),record.data()+4,16,record.data()+24,size,plain,"runtime-template/"+platform))throw std::runtime_error("Runtime template key or authentication mismatch; install matching engine build support");
    std::istringstream input(std::string(plain.begin(),plain.end()));std::string header,preset;input>>header>>std::quoted(preset);if(header!="IDK_RUNTIME_1"||preset!=platform)throw std::runtime_error("Runtime platform mismatch");
    std::string path,hash;while(input>>std::quoted(path)>>std::quoted(hash)){Child(runtime.root,path);if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos||!runtime.files.emplace(path,hash).second||runtime.files.size()>65536)throw std::runtime_error("Invalid runtime file manifest");}
    if(!input.eof()||!runtime.files.count("Runtime.exe")||!runtime.files.count("ShaderLibrary/content01.pak")||!runtime.files.count("Tools/CookStreamingTexture.exe"))throw std::runtime_error("Runtime template is incomplete");
    return runtime;
}
std::wstring Quote(const std::wstring& arg){std::wstring output=L"\"";unsigned slashes=0;for(auto c:arg){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){output.append(slashes*2+1,L'\\');output+=c;}else{output.append(slashes,L'\\');output+=c;}slashes=0;}output.append(slashes*2,L'\\');output+=L'\"';return output;}
void CookTexture(const fs::path& tool,const fs::path& input,const fs::path& output,const fs::path& dependencies,const Checkpoint& checkpoint,const std::function<void(const std::string&)>& log){
    fs::create_directories(output.parent_path());const auto logPath=output.parent_path()/"cooker.log";SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle capture;capture.value=CreateFileW(logPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(capture.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create texture cooker log");
    Handle nullInput;nullInput.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;start.hStdOutput=start.hStdError=capture.value;start.hStdInput=nullInput.value;
    PROCESS_INFORMATION process{};auto args=Quote(tool.wstring())+L" "+Quote(input.wstring())+L" "+Quote(output.wstring())+L" "+Quote(dependencies.wstring());
    if(!CreateProcessW(tool.c_str(),args.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,output.parent_path().c_str(),&start,&process))throw std::runtime_error("Cannot start installed texture cooker");
    Handle thread,child;thread.value=process.hThread;child.value=process.hProcess;const auto deadline=GetTickCount64()+120000;
    try{while(WaitForSingleObject(child.value,100)==WAIT_TIMEOUT){checkpoint();if(GetTickCount64()>deadline)throw std::runtime_error("Texture cooking exceeded two minutes");}}
    catch(...){TerminateProcess(child.value,ERROR_CANCELLED);WaitForSingleObject(child.value,5000);throw;}
    DWORD code=0;if(!GetExitCodeProcess(child.value,&code))throw std::runtime_error("Cannot read texture cooker status");
    CloseHandle(capture.value);capture.value=INVALID_HANDLE_VALUE;
    if(code!=0)throw std::runtime_error("Texture cooking failed for "+Utf8(input)+" (code "+std::to_string(code)+")");
    auto text=EngineStorage::ReadBuildInput(logPath);if(!text.empty())log(std::string(text.begin(),text.end()));checkpoint();
}
struct CookedTexture {EngineStorage::StreamingPakEntry entry{};fs::path source;};
uint64_t Align(uint64_t value){return (value+4095)&~uint64_t(4095);}
void WriteStreaming(const fs::path& path,std::vector<CookedTexture>& textures,const Checkpoint& checkpoint){
    using namespace EngineStorage;if(textures.size()>65536)throw std::runtime_error("Too many streaming textures");
    StreamingPakHeader header;header.magic=0x334B5053;header.version=2;header.numEntries=static_cast<uint32_t>(textures.size());header.headerSize=static_cast<uint32_t>(Align(48+textures.size()*160));
    uint64_t offset=header.headerSize;Bytes table;table.reserve(textures.size()*160);
    for(auto& texture:textures){texture.entry.offset=offset;const auto* entry=reinterpret_cast<const uint8_t*>(&texture.entry);table.insert(table.end(),entry,entry+160);offset=Align(offset+texture.entry.compressedBytes+32);}
    auto key=PakCrypto::GetMasterKey();uint8_t iv[16]{};Bytes cipher;auto context="stream-index/2/"+std::to_string(header.numEntries)+"/"+std::to_string(header.headerSize);
    if(!PakCrypto::RandomBytes(iv,16)||!PakCrypto::EncryptBlock(key.data(),key.size(),iv,16,table.data(),table.size(),cipher,context))throw std::runtime_error("Cannot encrypt streaming index");
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<char*>(&header),16);file.write(reinterpret_cast<char*>(iv),16);file.write(reinterpret_cast<char*>(cipher.data()),cipher.size());
    char zeros[4096]{};auto pad=[&](uint64_t target){auto position=file.tellp();if(position<0||uint64_t(position)>target)throw std::runtime_error("Invalid streaming offset");while(uint64_t(file.tellp())<target){auto bytes=(std::min)(uint64_t(sizeof(zeros)),target-uint64_t(file.tellp()));file.write(zeros,bytes);if(!file)throw std::runtime_error("Cannot pad streaming archive");}};
    pad(header.headerSize);
    for(auto& texture:textures){checkpoint();auto cooked=EngineStorage::ReadBuildInput(texture.source);if(cooked.size()!=32ull+texture.entry.compressedBytes)throw std::runtime_error("Cooked texture changed during build");
        if(!PakCrypto::RandomBytes(iv,16)||!PakCrypto::EncryptBlock(key.data(),key.size(),iv,16,cooked.data()+32,cooked.size()-32,cipher,"stream/"+std::string(texture.entry.name)))throw std::runtime_error("Cannot encrypt streaming texture");
        file.write(reinterpret_cast<char*>(iv),16);file.write(reinterpret_cast<char*>(cipher.data()),cipher.size());pad(Align(texture.entry.offset+texture.entry.compressedBytes+32));}
    file.flush();if(!file)throw std::runtime_error("Cannot finish streaming archive");
}
#pragma pack(push,1)
struct LicensePayload {uint64_t hwid=0;char bound[32]{};char product[64]{};char owner[64]{};uint64_t issued=0,expires=0;uint32_t flags=1;};
#pragma pack(pop)
Asset License(){static_assert(sizeof(LicensePayload)==188);LicensePayload payload;strcpy_s(payload.bound,"UNIVERSAL_DISTRIBUTION");strcpy_s(payload.product,"Idontknow Commercial Release");strcpy_s(payload.owner,"Licensed Game User");payload.issued=static_cast<uint64_t>(std::time(nullptr));
    Bytes bytes(20);std::memcpy(bytes.data(),"MYENG_LIC_V1",12);uint32_t size=188;std::memcpy(bytes.data()+16,&size,4);const auto* data=reinterpret_cast<const uint8_t*>(&payload);Bytes raw(data,data+188),signature=Hmac(raw);bytes.insert(bytes.end(),raw.begin(),raw.end());bytes.insert(bytes.end(),signature.begin(),signature.end());return {u"security/license.lic",{},std::move(bytes)};
}
Asset DllManifest(const fs::path& thirdParty,const Checkpoint& checkpoint){std::map<std::string,std::string> hashes;for(auto& file:Files(thirdParty)){auto extension=Utf8(file.extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});if(extension==".dll")hashes[Utf8(file.lexically_relative(thirdParty))]=CopyHash(file,{},checkpoint);}
    std::string body;for(auto& [path,hash]:hashes){auto name=path;std::replace(name.begin(),name.end(),'\\','/');body+=name+":"+hash+"\r\n";}auto signature=Hmac(Text(body));return {u"security/integrity.manifest",{},Text("HMAC-SHA256:"+Hex(signature.data(),signature.size())+"\r\n"+body)};
}
}
struct ProjectBuilder::State {
    mutable std::mutex mutex;BuildSnapshot snapshot;std::atomic<bool> cancel{false};std::thread worker;
};
ProjectBuilder::ProjectBuilder():state(std::make_unique<State>()){}
ProjectBuilder::~ProjectBuilder(){Cancel();if(state->worker.joinable())state->worker.join();}
ProjectBuilder& ProjectBuilder::Get(){static ProjectBuilder builder;return builder;}
fs::path ProjectBuilder::SupportRoot(){return ExeDirectory()/"ThirdParty/Idontknow/BuildSupport/Windows";}
bool ProjectBuilder::HasRuntime(const std::string& platform){if(platform!="standalone"&&platform!="steam"&&platform!="epic")return false;std::error_code error;return fs::is_regular_file(SupportRoot()/platform/"runtime.idkr",error);}
BuildSnapshot ProjectBuilder::Snapshot()const{std::lock_guard<std::mutex> lock(state->mutex);return state->snapshot;}
void ProjectBuilder::Cancel(){state->cancel=true;}
void ProjectBuilder::Start(const fs::path& project,const std::string& platform){
    {std::lock_guard<std::mutex> lock(state->mutex);if(state->snapshot.running)throw std::runtime_error("A project build is already running");}
    if(state->worker.joinable())state->worker.join();state->cancel=false;
    {std::lock_guard<std::mutex> lock(state->mutex);state->snapshot={};state->snapshot.running=true;state->snapshot.project=fs::absolute(project);state->snapshot.phase="Validating project";}
    try{state->worker=std::thread([this,project,platform]{Run(project,platform);});}catch(...){std::lock_guard<std::mutex> lock(state->mutex);state->snapshot.running=false;throw;}
}
bool ProjectBuilder::BuildBlocking(const fs::path& project,const std::string& platform){Start(project,platform);state->worker.join();return Snapshot().succeeded;}
void ProjectBuilder::InstallRuntime(const fs::path& sourceBuild,const std::string& platform,const fs::path& textureCooker){
    RequirePlatform(platform);const auto source=fs::weakly_canonical(sourceBuild),root=SupportRoot();
    auto relative=root.lexically_relative(source);if(!relative.empty()&&*relative.begin()!="..")throw std::runtime_error("Runtime source must be a game build, outside editor build support");
    if(!fs::is_regular_file(source/"Idontknow.exe")||!fs::is_directory(source/"ThirdParty")||!fs::is_regular_file(textureCooker))throw std::runtime_error("A compiled game runtime and texture cooker are required");
    fs::create_directories(root);const auto stage=root/("."+platform+"-"+NewId());fs::create_directories(stage);
    try{
        std::map<std::string,std::string> files;files["Runtime.exe"]=CopyHash(source/"Idontknow.exe",stage/"Runtime.exe");
        for(auto& file:Files(source/"ThirdParty")){auto name="ThirdParty/"+Utf8(file.lexically_relative(source/"ThirdParty"));std::replace(name.begin(),name.end(),'\\','/');files[name]=CopyHash(file,Child(stage,name));}
        files["Tools/CookStreamingTexture.exe"]=CopyHash(textureCooker,stage/"Tools/CookStreamingTexture.exe");
        std::vector<Asset> shaders;
        for(unsigned volume=1;volume<=9;++volume){auto name="content"+std::string(volume<10?"0":"")+std::to_string(volume)+".pak";for(auto& entry:ArchiveNames(source/"Contents"/name))if(entry.starts_with(u"shaders/")){auto bytes=EngineStorage::ReadContentArchive(entry,source/"Contents");if(bytes.empty())throw std::runtime_error("Runtime shader could not be authenticated");shaders.push_back({entry,{},std::move(bytes)});}}
        if(shaders.empty())throw std::runtime_error("Runtime contains no compiled shaders");
        EngineStorage::WriteAuthenticatedPak(stage/"ShaderLibrary/content01.pak",shaders);files["ShaderLibrary/content01.pak"]=CopyHash(stage/"ShaderLibrary/content01.pak",{});
        std::ostringstream manifest;manifest<<"IDK_RUNTIME_1 "<<std::quoted(platform)<<'\n';for(auto& [name,hash]:files)manifest<<std::quoted(name)<<' '<<std::quoted(hash)<<'\n';
        auto plain=Text(manifest.str()),key=PakCrypto::GetMasterKey();uint8_t iv[16]{};Bytes encrypted;
        if(!PakCrypto::RandomBytes(iv,16)||!PakCrypto::EncryptBlock(key.data(),key.size(),iv,16,plain.data(),plain.size(),encrypted,"runtime-template/"+platform))throw std::runtime_error("Cannot authenticate runtime template");
        Bytes record;EngineStorage::AppendU32(record,0x52544B49);record.insert(record.end(),iv,iv+16);EngineStorage::AppendU32(record,static_cast<uint32_t>(encrypted.size()));record.insert(record.end(),encrypted.begin(),encrypted.end());Write(stage/"runtime.idkr",record);
        const auto destination=root/platform,backup=root/("."+platform+"-previous-"+NewId());bool moved=false;
        if(fs::exists(destination)){RejectLink(destination);fs::rename(destination,backup);moved=true;}
        try{fs::rename(stage,destination);}catch(...){if(moved)fs::rename(backup,destination);throw;}
        if(moved)RemoveOwnedStage(backup,root);
        std::cout<<"[Build] Installed "<<platform<<" runtime: "<<Utf8(destination)<<std::endl;
    }catch(...){if(fs::exists(stage))RemoveOwnedStage(stage,root);throw;}
}
void ProjectBuilder::Run(fs::path projectFile,std::string platform){
    fs::path stage,stageParent;std::ofstream logfile;
    auto checkpoint=[&]{if(state->cancel||EngineLog::closeRequested)throw Cancelled();};
    auto log=[&](const std::string& text){std::cout<<"[Build] "<<text<<std::endl;if(logfile){logfile<<text<<'\n';logfile.flush();}std::lock_guard<std::mutex> lock(state->mutex);auto& lines=state->snapshot.lines;if(lines.size()>=500)lines.erase(lines.begin());lines.push_back(text);};
    auto phase=[&](const std::string& name,float progress){checkpoint();{std::lock_guard<std::mutex> lock(state->mutex);state->snapshot.phase=name;state->snapshot.progress=progress;}log(name);};
    try{
        auto project=Project::Load(projectFile);if(!platform.empty())project.platform=platform;RequirePlatform(project.platform);
        const auto root=project.Root(),output=project.Resolve("Build");const auto id=NewId();stageParent=project.Resolve(".idk-build");stage=stageParent/id;
        for(auto& path:{project.content,project.editorScene,project.startupLevel,project.mainMenu,project.splash})if(!path.empty()){
            const auto input=project.Resolve(path);for(auto& generated:{output,stageParent,project.Resolve("Saved/BuildLogs"),project.Resolve("Saved/BuildHistory")}){
                auto relative=input.lexically_relative(generated);if(!relative.empty()&&*relative.begin()!="..")throw std::runtime_error("Project sources must be outside generated build directories");
            }
        }
        const auto logs=project.Resolve("Saved/BuildLogs");fs::create_directories(logs);Handle lock;
        lock.value=CreateFileW((logs/"project-build.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(lock.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Another editor is already building this project");
        const auto logPath=logs/("Build-"+id+".log");logfile.open(logPath,std::ios::binary|std::ios::trunc);if(!logfile)throw std::runtime_error("Cannot create project build log");
        {std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.output=output;state->snapshot.logFile=logPath;}
        phase("Validating installed "+project.platform+" runtime",.02f);
        if(!HasRuntime(project.platform))throw std::runtime_error("Build support for "+project.platform+" is not installed in this engine");
        if(project.platform=="epic" && (project.epicProductId.empty() || project.epicSandboxId.empty() || project.epicDeploymentId.empty() || project.epicClientId.empty() || project.epicClientSecret.empty()))
            throw std::runtime_error("Epic Online Services build requires Product, Sandbox, Deployment and client credentials in Project Settings");
        auto runtime=LoadRuntime(project.platform);
        fs::create_directories(stage/"Contents");phase("Copying and verifying game runtime",.05f);size_t copied=0;
        for(auto& [name,hash]:runtime.files){checkpoint();fs::path destination;
            if(name=="Runtime.exe")destination=stage/"Idontknow.exe";else if(name.starts_with("ThirdParty/"))destination=Child(stage,name);else destination=Child(stage/".work/Runtime",name);
            if(CopyHash(Child(runtime.root,name),destination,checkpoint)!=hash)throw std::runtime_error("Installed runtime file failed SHA-256 verification: "+name);
            {std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.progress=.05f+.25f*float(++copied)/float(runtime.files.size());}
        }
        phase("Collecting project assets",.32f);std::vector<Asset> assets;std::vector<std::pair<std::string,fs::path>> images;
        const auto content=project.Resolve(project.content);for(auto& file:Files(content)){checkpoint();auto relative=Utf8(file.lexically_relative(content));std::replace(relative.begin(),relative.end(),'\\','/');assets.push_back({Path("Content/"+relative).u16string(),file,{}});
            auto ext=Utf8(file.extension());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga"||ext==".bmp")images.emplace_back(relative,file);}
        Project packaged=project;packaged.content="Content";auto remap=[&](std::string& path){if(path.starts_with(project.content+"/"))path="Content/"+path.substr(project.content.size()+1);};remap(packaged.editorScene);remap(packaged.startupLevel);remap(packaged.mainMenu);remap(packaged.splash);
        assets.push_back({u"Runtime/Project.idkp",{},Text(packaged.Serialize())});
        std::set<std::string> external;
        for(auto& path:{project.editorScene,project.startupLevel,project.mainMenu,project.splash})if(!path.empty()&&!path.starts_with(project.content+"/")&&external.insert(path).second){auto source=project.Resolve(path);if(fs::is_regular_file(source))assets.push_back({Path("Runtime/"+path).u16string(),{},EngineStorage::ReadBuildInput(source)});else if(path!=project.editorScene)throw std::runtime_error("Project startup asset missing: "+path);}
        std::sort(assets.begin(),assets.end(),[](auto& a,auto& b){return a.name<b.name;});std::string fileList=id+"\n";for(auto& asset:assets)fileList+=Utf8(fs::path(asset.name))+"\n";assets.push_back({u"Runtime/files.txt",{},Text(fileList)});
        const auto shaders=stage/".work/Runtime/ShaderLibrary";for(auto& name:ArchiveNames(shaders/"content01.pak")){auto bytes=EngineStorage::ReadContentArchive(name,shaders);if(bytes.empty())throw std::runtime_error("Runtime shader data failed authentication");assets.push_back({name,{},std::move(bytes)});}
        phase("Cooking streaming textures",.38f);std::vector<CookedTexture> textures;
        for(auto& [name,source]:images){checkpoint();if(name.size()>127)throw std::runtime_error("Streaming texture path exceeds 127 UTF-8 bytes: "+name);log("Texture: "+name);auto cooked=stage/".work/Cooked"/(std::to_string(textures.size())+".gdst");
            auto modified=fs::last_write_time(source);CookTexture(stage/".work/Runtime/Tools/CookStreamingTexture.exe",source,cooked,stage/"ThirdParty/DirectStorage",checkpoint,log);if(fs::last_write_time(source)!=modified)throw std::runtime_error("Texture changed while cooking; retry the build");
            auto bytes=EngineStorage::ReadBuildInput(cooked);if(bytes.size()<32)throw std::runtime_error("Cooked texture is truncated");EngineStorage::StreamingTextureHeader header;std::memcpy(&header,bytes.data(),32);
            if(header.magic!=0x58545347||header.version!=1||!header.compressedBytes||header.compressedBytes+32ull!=bytes.size()||!header.width||!header.height||header.width>16384||header.height>16384||!header.mips||header.mips>15||header.format!=28||header.rawBytes>256u*1024*1024)throw std::runtime_error("Invalid cooked texture");
            CookedTexture texture;std::memcpy(texture.entry.name,name.c_str(),name.size()+1);texture.entry.compressedBytes=header.compressedBytes;texture.entry.rawBytes=header.rawBytes;texture.entry.width=header.width;texture.entry.height=header.height;texture.entry.mips=header.mips;texture.entry.format=header.format;texture.source=cooked;textures.push_back(std::move(texture));}
        WriteStreaming(stage/"Contents/content10.pak",textures,checkpoint);
        phase("Generating DLL integrity manifest",.50f);assets.push_back(DllManifest(stage/"ThirdParty",checkpoint));assets.push_back(License());
        std::array<std::vector<Asset>,9> volumes;size_t entry=0;for(auto& asset:assets)volumes[entry++%9].push_back(std::move(asset));
        for(unsigned volume=0;volume<9;++volume){phase("Encrypting content volume "+std::to_string(volume+1)+"/9",.55f+.35f*float(volume)/9.f);auto name="content0"+std::to_string(volume+1)+".pak";EngineStorage::WriteAuthenticatedPak(stage/"Contents"/name,volumes[volume],checkpoint);}
        phase("Verifying packaged project",.92f);if(!EngineStorage::AuthenticateArchiveHeaders(stage/"Contents"))throw std::runtime_error("Packaged content failed authentication");
        for(const auto& volume:volumes)for(const auto& asset:volume){checkpoint();bool authenticated=false;auto bytes=EngineStorage::ReadContentArchive(asset.name,stage/"Contents",&authenticated);if(!authenticated)throw std::runtime_error("Packaged resource could not be verified");}
        RemoveOwnedStage(stage/".work",stage);phase("Publishing project Build",.98f);
        if(fs::exists(output)){
            if(fs::is_regular_file(output/"Idontknow.exe")){Handle writable;writable.value=CreateFileW((output/"Idontknow.exe").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(writable.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Close the running project game before replacing its Build");}
            RejectLink(output);for(auto& file:fs::directory_iterator(output))if(file.path().filename()!="Contents"&&file.path().filename()!="ThirdParty"&&file.path().filename()!="Idontknow.exe")throw std::runtime_error("Build contains unrelated files; choose a clean generated Build directory");
            const auto history=project.Resolve("Saved/BuildHistory");fs::create_directories(history);const auto previous=history/id;
            fs::rename(output,previous);try{fs::rename(stage,output);}catch(...){fs::rename(previous,output);throw;}log("Previous Build preserved: "+Utf8(previous));
        }else fs::rename(stage,output);
        log("Build ready: "+Utf8(output/"Idontknow.exe"));{std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.succeeded=true;state->snapshot.progress=1;state->snapshot.phase="Build completed";}
    }catch(const Cancelled&){log("Build cancelled; previous Build preserved");std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.cancelled=true;state->snapshot.phase="Build cancelled";}
    catch(const std::exception& error){log(std::string("Build failed: ")+error.what());std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.error=error.what();state->snapshot.phase="Build failed";}
    try{if(!stage.empty()&&fs::exists(stage))RemoveOwnedStage(stage,stageParent);if(!stageParent.empty()&&fs::exists(stageParent)&&fs::is_empty(stageParent))fs::remove(stageParent);}catch(const std::exception& error){log(std::string("Staging cleanup: ")+error.what());}
    std::lock_guard<std::mutex> guard(state->mutex);state->snapshot.running=false;
}
}
#endif
