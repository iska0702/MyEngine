#pragma once
#include "ProjectLauncher.h"
#include "Storage/ContentFile.h"
#include <shlobj.h>

namespace EngineProject {
// WebView and file-based importers need physical files. Keep their runtime
// cache in the user profile; the distributed game contains only PAK assets.
inline fs::path runtimeUserRoot;
inline void OpenPackagedProject(){
    const auto bytes=EngineStorage::ReadContentArchive(u"Runtime/files.txt");
    if(bytes.empty())throw std::runtime_error("Packaged project manifest not found");
    std::istringstream manifest(std::string(bytes.begin(),bytes.end()));
    std::string build;std::getline(manifest,build);
    if(!build.empty()&&build.back()=='\r')build.pop_back();
    if(build.size()!=32||build.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid packaged project build ID");
    PWSTR local=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))
        throw std::runtime_error("Cannot locate runtime user directory");
    fs::path user(local);CoTaskMemFree(local);
    uint64_t install=14695981039346656037ull;
    for(auto c:Utf8(ExecutableDirectory())){install^=static_cast<unsigned char>(c);install*=1099511628211ull;}
    std::ostringstream id;id<<std::hex<<install;
    runtimeUserRoot=user/"Idontknow/Games"/id.str();
    const auto root=runtimeUserRoot/"Cache"/build;
    fs::create_directories(root/"Content");
    const auto complete=root/".complete";
    { // A .complete file is not an integrity proof. Revalidate cached bytes every launch.
        std::string key;
        while(std::getline(manifest,key)){
            if(!key.empty()&&key.back()=='\r')key.pop_back();
            if(key.empty())continue;
            std::string relative;
            if(key.starts_with("Runtime/"))relative=key.substr(8);
            else if(key.starts_with("Content/"))relative=key;
            else throw std::runtime_error("Invalid packaged project entry");
            Project boundary;boundary.file=root/"Project.idkp";
            const auto target=boundary.Resolve(relative);
            bool authenticated=false;const auto data=EngineStorage::ReadContentArchive(Path(key).u16string(),{},&authenticated);
            if(!authenticated)throw std::runtime_error("Packaged asset missing or failed authentication: "+key);
            std::error_code cacheError;
            if(fs::is_regular_file(target,cacheError)&&fs::file_size(target,cacheError)==data.size()){
                std::ifstream existing(target,std::ios::binary);std::vector<uint8_t> cached(data.size());
                existing.read(reinterpret_cast<char*>(cached.data()),cached.size());if(existing&&cached==data)continue;
            }
            fs::create_directories(target.parent_path());
            std::ofstream out(target,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();
            if(!out)throw std::runtime_error("Cannot prepare runtime asset: "+key);
        }
        // Validate before marking the cache complete, so failed extraction is retried.
        Project::Load(root/"Project.idkp");
        std::ofstream marker(complete);marker<<build;marker.close();
        if(!marker)throw std::runtime_error("Cannot finish runtime cache");
    }
    Open(root/"Project.idkp");
}
}
