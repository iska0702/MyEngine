#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <random>
#include <cctype>
#include "ProjectTuning.h"

namespace EngineProject {
inline std::string NewShaderAppId() {
    std::random_device random;
    unsigned char bytes[16];for(auto& b:bytes)b=static_cast<unsigned char>(random());
    bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(int i=0;i<16;++i){if(i==4||i==6||i==8||i==10)out<<'-';out<<std::setw(2)<<unsigned(bytes[i]);}
    return out.str();
}
namespace fs = std::filesystem;
inline fs::path Path(const std::string& value) { return fs::path(std::u8string(value.begin(),value.end())); }
inline std::string Utf8(const fs::path& value) { auto s=value.u8string();return {s.begin(),s.end()}; }
struct Project {
    fs::path file;
    std::string name="New Project", content="Content", editorScene="Saved/Editor.save";
    std::string startupLevel, mainMenu="Content/UI/main_menu.html", splash;
    std::string platform="standalone";
    unsigned int steamAppId=480;
    bool steamRestart=false;
    std::string epicProductId,epicSandboxId,epicDeploymentId,epicClientId,epicClientSecret;
    float splashSeconds=2;
    int width=1280,height=720;
    bool startWithMenu=true;
    ProjectTuning tuning;
    std::string developer,version="1.0.0",description;
    std::string shaderAppId;
    fs::path Root() const { return file.parent_path(); }
    fs::path Resolve(const std::string& relative) const {
        if(relative.empty())return {};
        auto input=Path(relative);
        if(input.is_absolute()||input.has_root_name())throw std::runtime_error("Project paths must be relative");
        auto root=fs::weakly_canonical(Root());
        auto result=fs::weakly_canonical(root/input);
        auto inside=result.lexically_relative(root);
        if(inside.empty()||inside=="."||*inside.begin()=="..")throw std::runtime_error("Project path escapes project directory");
        return result;
    }
    void Validate() const {
        if(!shaderAppId.empty()){
            if(shaderAppId.size()!=36)throw std::runtime_error("Invalid ASD application GUID");
            for(size_t i=0;i<36;++i){const bool dash=i==8||i==13||i==18||i==23;
                if(dash?shaderAppId[i]!='-':!std::isxdigit(static_cast<unsigned char>(shaderAppId[i])))throw std::runtime_error("Invalid ASD application GUID");}
        }
        tuning.Validate();
        if(name.empty()||name.size()>200)throw std::runtime_error("Invalid project name");
        if(width<320||width>16384||height<240||height>16384)throw std::runtime_error("Invalid window resolution");
        if(!(splashSeconds>=0&&splashSeconds<=120))throw std::runtime_error("Invalid splash duration");
        if(platform!="standalone"&&platform!="steam"&&platform!="epic")throw std::runtime_error("Invalid project platform");
        if(!steamAppId)throw std::runtime_error("Invalid Steam App ID");
        if(content.empty()||editorScene.empty())throw std::runtime_error("Content and scene paths are required");
        for(auto& p:{content,editorScene,startupLevel,mainMenu,splash})Resolve(p);
    }
    std::string Serialize() const {
        Validate();std::ostringstream out;
        out<<"IDKP 1\n";
        auto field=[&](const char* key,const std::string& value){out<<key<<' '<<std::quoted(value)<<'\n';};
        field("name",name);field("content",content);field("editorScene",editorScene);
        field("startupLevel",startupLevel);field("mainMenu",mainMenu);field("splash",splash);field("platform",platform);
        field("developer",developer);field("version",version);field("description",description);
        field("shaderAppId",shaderAppId);
        field("epicProductId",epicProductId);field("epicSandboxId",epicSandboxId);field("epicDeploymentId",epicDeploymentId);
        field("epicClientId",epicClientId);field("epicClientSecret",epicClientSecret);
        out<<"steamAppId "<<steamAppId<<"\nsteamRestart "<<steamRestart<<'\n';
        auto values=tuning;values.Visit([&](auto key,float value,auto,auto,auto,auto){out<<"tuning_"<<key<<' '<<value<<'\n';});
        out<<"splashSeconds "<<splashSeconds<<"\nwidth "<<width<<"\nheight "<<height<<"\nstartWithMenu "<<startWithMenu<<'\n';
        return out.str();
    }
    void Save() const {
        Validate();
        if(!fs::is_directory(Resolve(content)))throw std::runtime_error("Project Content directory not found");
        for(auto& asset:{startupLevel,mainMenu,splash})if(!asset.empty()&&!fs::is_regular_file(Resolve(asset)))throw std::runtime_error("Project startup asset not found: "+asset);
        auto temporary=file;temporary+=L".tmp";
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
        out<<Serialize();
        out.flush();if(!out)throw std::runtime_error("Cannot write project file");out.close();
        std::error_code error;fs::rename(temporary,file,error);
        if(error){fs::copy_file(temporary,file,fs::copy_options::overwrite_existing);fs::remove(temporary);}
    }
    static Project Load(const fs::path& source) {
        Project p;p.file=fs::absolute(source).lexically_normal();
        if(p.file.extension()!=L".idkp")throw std::runtime_error("Select an .idkp project file");
        std::ifstream in(p.file,std::ios::binary);std::string magic;int version=0;
        if(!(in>>magic>>version)||magic!="IDKP"||version!=1)throw std::runtime_error("Invalid or unsupported .idkp project");
        std::string line;std::getline(in,line);
        while(std::getline(in,line)){
            std::istringstream row(line);std::string key;if(!(row>>key)||key[0]=='#')continue;
            std::string* target=nullptr;
            if(key=="name")target=&p.name;else if(key=="content")target=&p.content;
            else if(key=="editorScene")target=&p.editorScene;else if(key=="startupLevel")target=&p.startupLevel;
            else if(key=="mainMenu")target=&p.mainMenu;else if(key=="splash")target=&p.splash;else if(key=="platform")target=&p.platform;
            else if(key=="developer")target=&p.developer;else if(key=="version")target=&p.version;else if(key=="description")target=&p.description;
            else if(key=="shaderAppId")target=&p.shaderAppId;
            else if(key=="epicProductId")target=&p.epicProductId;else if(key=="epicSandboxId")target=&p.epicSandboxId;
            else if(key=="epicDeploymentId")target=&p.epicDeploymentId;else if(key=="epicClientId")target=&p.epicClientId;
            else if(key=="epicClientSecret")target=&p.epicClientSecret;
            if(target)row>>std::quoted(*target);
            else if(key=="width")row>>p.width;else if(key=="height")row>>p.height;
            else if(key=="splashSeconds")row>>p.splashSeconds;else if(key=="startWithMenu")row>>p.startWithMenu;
            else if(key=="steamAppId")row>>p.steamAppId;else if(key=="steamRestart")row>>p.steamRestart;
            else {
                bool found=false;p.tuning.Visit([&](auto name,float& value,auto,auto,auto,auto){if(key==std::string("tuning_")+name){row>>value;found=true;}});
                if(!found)throw std::runtime_error("Unknown project setting: "+key);
            }
            if(!row)throw std::runtime_error("Invalid project setting: "+key);
        }
        if(in.bad())throw std::runtime_error("Cannot read project file");p.Validate();
        if(!fs::is_directory(p.Resolve(p.content)))throw std::runtime_error("Project Content directory not found");
        for(auto& asset:{p.startupLevel,p.mainMenu,p.splash})if(!asset.empty()&&!fs::is_regular_file(p.Resolve(asset)))throw std::runtime_error("Project startup asset not found: "+asset);
        return p;
    }
};
inline Project current;
inline bool active=false;
inline void Open(const fs::path& file){
    current=Project::Load(file);
    if(current.shaderAppId.empty()){current.shaderAppId=NewShaderAppId();current.Save();}
    active=true;
}
inline void Create(const fs::path& file,const fs::path& templateContent={}){
    if(fs::exists(file))throw std::runtime_error("Project already exists");
    if(file.extension()!=L".idkp")throw std::runtime_error("Project file must use the .idkp extension");
    Project p;p.file=fs::absolute(file);p.name=Utf8(file.stem());p.Validate();
    if(!templateContent.empty()){
        auto relative=fs::weakly_canonical(p.Root()).lexically_relative(fs::weakly_canonical(templateContent));
        if(!relative.empty()&&*relative.begin()!="..")throw std::runtime_error("Create projects outside the template Content folder");
    }
    fs::create_directories(p.Root());
    fs::create_directories(p.Resolve(p.content));fs::create_directories(p.Root()/"Saved");
    if(!templateContent.empty()&&fs::exists(templateContent)){
        for(auto& entry:fs::recursive_directory_iterator(templateContent)){
            if(entry.is_symlink())continue;
            auto destination=p.Resolve(p.content)/entry.path().lexically_relative(templateContent);
            if(entry.is_directory())fs::create_directories(destination);
            else if(entry.is_regular_file()&&!fs::exists(destination)){fs::create_directories(destination.parent_path());fs::copy_file(entry.path(),destination);}
        }
    }
    if(!fs::exists(p.Resolve(p.mainMenu)))p.mainMenu.clear();
    p.shaderAppId=NewShaderAppId();p.Save();Open(file);
}
}
