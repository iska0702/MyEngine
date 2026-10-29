#include "Core/ProjectBuilder.h"
#include "Core/Project.h"
#include "Storage/ContentFile.h"
#include "Storage/PakWriter.h"
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace EngineProject;
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;Check(bool(out),"Fixture write failed");}
int wmain(int argc,wchar_t** argv){try{
    Check(argc==4,"Source build, cooker and fixture directory required");auto source=fs::absolute(argv[1]),cooker=fs::absolute(argv[2]),root=fs::absolute(argv[3]);
    const auto minimal=root/"RuntimeSource";fs::create_directories(minimal/"Contents");fs::create_directories(minimal/"ThirdParty/Microsoft/DirectStorage");
    fs::copy_file(source/"Idontknow.exe",minimal/"Idontknow.exe",fs::copy_options::overwrite_existing);
    for(auto& entry:fs::directory_iterator(source/"Contents"))if(entry.path().filename()!="content10.pak")fs::copy_file(entry.path(),minimal/"Contents"/entry.path().filename(),fs::copy_options::overwrite_existing);
    for(auto& entry:fs::directory_iterator(source/"ThirdParty/Microsoft/DirectStorage"))if(entry.is_regular_file())fs::copy_file(entry.path(),minimal/"ThirdParty/Microsoft/DirectStorage"/entry.path().filename(),fs::copy_options::overwrite_existing);
    ProjectBuilder::InstallRuntime(minimal,"standalone",cooker);
    Project project;project.file=root/L"Проект с пробелами"/"Game Test.idkp";project.name="Native build fixture";project.content="Assets";project.editorScene="Assets/Scene.save";project.mainMenu="Assets/UI/menu.html";project.splash="Presentation.html";
    fs::create_directories(project.Resolve(project.content));Write(project.Resolve(project.mainMenu),"<html>Native menu fixture</html>");Write(project.Resolve(project.splash),"<html>Splash fixture</html>");
    Write(project.Resolve(project.editorScene),"Scene fixture; packaging preserves bytes without interpreting the scene");
    fs::create_directories(project.Resolve("Assets/DemoTexture"));
    auto engine=source.parent_path().parent_path().parent_path();
    fs::copy_file(engine/"Projects/Test/Content/DemoTexture/demo_texture.jpg",project.Resolve("Assets/DemoTexture/demo_texture.jpg"),fs::copy_options::overwrite_existing);
    Write(project.Resolve("Assets/Empty.lua"),"");
    project.Save();auto originalProject=EngineStorage::ReadBuildInput(project.file),originalScene=EngineStorage::ReadBuildInput(project.Resolve(project.editorScene));
    auto& builder=ProjectBuilder::Get();Check(builder.BuildBlocking(project.file),"Native build failed");
    const auto output=project.Root()/"Build";auto game=EngineStorage::ReadBuildInput(output/"Idontknow.exe");
    bool emptyAuthenticated=false;auto empty=EngineStorage::ReadContentArchive(u"Content/Empty.lua",output/"Contents",&emptyAuthenticated);Check(emptyAuthenticated&&empty.empty(),"Empty authenticated asset was treated as a missing file");
    Check(!game.empty()&&fs::is_directory(output/"ThirdParty")&&fs::is_directory(output/"Contents"),"Native output layout invalid");
    auto settings=EngineStorage::ReadContentArchive(u"Runtime/Project.idkp",output/"Contents");std::string text(settings.begin(),settings.end());
    Check(text.find("content \"Content\"")!=std::string::npos&&text.find("editorScene \"Content/Scene.save\"")!=std::string::npos&&text.find("mainMenu \"Content/UI/menu.html\"")!=std::string::npos,"Custom project paths were not remapped");
    Check(EngineStorage::ReadContentArchive(u"Content/Scene.save",output/"Contents")==originalScene,"Scene bytes changed");
    Check(!EngineStorage::ReadContentArchive(u"Runtime/Presentation.html",output/"Contents").empty(),"External startup asset not packed");
    Check(EngineStorage::ReadBuildInput(project.file)==originalProject&&EngineStorage::ReadBuildInput(project.Resolve(project.editorScene))==originalScene,"Build changed project sources");
    auto runtime=ProjectBuilder::SupportRoot()/"standalone/Runtime.exe";auto binary=EngineStorage::ReadBuildInput(runtime);auto corrupted=binary;corrupted[0]^=1;
    {std::ofstream out(runtime,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char*>(corrupted.data()),corrupted.size());}
    Check(!builder.BuildBlocking(project.file)&&builder.Snapshot().error.find("SHA-256")!=std::string::npos,"Modified runtime was accepted");
    {std::ofstream out(runtime,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char*>(binary.data()),binary.size());}
    Check(EngineStorage::ReadBuildInput(output/"Idontknow.exe")==game,"Failed build replaced previous output");
    builder.Start(project.file);builder.Cancel();while(builder.Snapshot().running)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(builder.Snapshot().cancelled&&EngineStorage::ReadBuildInput(output/"Idontknow.exe")==game,"Cancellation did not preserve previous Build");
    Check(!builder.BuildBlocking(project.file,"epic")&&builder.Snapshot().error.find("not installed")!=std::string::npos,"Missing runtime was not reported");
    Write(root/"Bad.idkp","IDKP 1\nname \"Bad\"\ncontent \"../Escape\"\n");
    Check(!builder.BuildBlocking(root/"Bad.idkp"),"Project path escape accepted");
    Write(output/"UserFile.txt","Do not replace unrelated files");
    Check(!builder.BuildBlocking(project.file)&&builder.Snapshot().error.find("unrelated")!=std::string::npos&&fs::is_regular_file(output/"UserFile.txt"),"Unrelated output file was overwritten");
    std::cout<<"PASS: native .idkp packaging, spaces/Unicode/custom paths, encrypted archives and streaming texture cooking, source preservation, runtime SHA-256 rejection, cancellation, missing platform, path escape and previous-build preservation\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
