#include "Core/Project.h"
#include "Storage/PakValidation.h"
#include <iostream>
int main(){
    if(EngineStorage::ValidLegacyPakHeader(EngineStorage::StreamingPakMagic,4096) || EngineStorage::ValidLegacyPakHeader(0xffffffff,4096) || EngineStorage::ValidLegacyPakHeader(1,267) || !EngineStorage::ValidLegacyPakHeader(1,268))return 10;
    using namespace EngineProject;
    auto root=fs::temp_directory_path()/"Idontknow-project-regression";
    fs::create_directories(root);
    auto file=root/"Regression.idkp";
    std::error_code ec;fs::remove(file,ec);
    try{
        Create(file);current.name="Тест Project With Spaces";current.width=1600;
        current.developer="Developer";current.version="2.1";current.description="Physics settings test";
        current.tuning.gravityY=-1.62f;current.tuning.physicsHz=120;current.tuning.ragdollMass=85;current.tuning.pushForce=100;
        current.Save();
        auto p=Project::Load(file);
        if(p.shaderAppId.size()!=36||p.shaderAppId!=current.shaderAppId||p.shaderAppId[14]!='4')return 13;
        {auto invalid=p;invalid.shaderAppId="invalid";bool rejected=false;try{invalid.Validate();}catch(...){rejected=true;}if(!rejected)return 14;}
        if(p.name!=current.name||p.width!=1600||p.Resolve("Content/test.png")!=root/"Content/test.png")return 1;
        if(p.developer!="Developer"||p.version!="2.1"||p.tuning.physicsHz!=120||p.tuning.ragdollMass!=85||p.tuning.pushForce!=100||std::abs(p.tuning.gravityY+1.62f)>.001f)return 11;
        bool invalid=false;auto bad=p;bad.tuning.physicsHz=0;try{bad.Save();}catch(...){invalid=true;}if(!invalid)return 12;
        bool rejected=false;try{p.Resolve("../outside");}catch(...){rejected=true;}if(!rejected)return 2;
        rejected=false;p.width=-1;try{p.Save();}catch(...){rejected=true;}if(!rejected)return 3;
        rejected=false;try{Create(file);}catch(...){rejected=true;}if(!rejected)return 4;
        std::cout<<"Project round-trip, unicode, path isolation, invalid settings and overwrite protection passed\n";
    }catch(const std::exception& e){std::cerr<<e.what();return 5;}
    fs::remove_all(root);return 0;
}
