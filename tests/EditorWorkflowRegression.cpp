#include "Core/EditorHistory.h"
#include "Scene/GameplaySettings.h"
#include "Prefab/Prefab.h"
#include <iostream>
#include <stdexcept>
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    EngineEditor::History history;history.Reset("initial");
    Check(history.Commit("create object"),"Create was not recorded");
    Check(!history.Commit("create object"),"Unchanged state created undo step");
    history.Commit("delete object");Check(*history.Undo()=="create object","Delete undo failed");
    Check(*history.Redo()=="delete object","Delete redo failed");history.Undo();history.Commit("edit material");Check(!history.CanRedo(),"New edit retained stale redo branch");
    for(int i=0;i<100;++i)history.Commit(std::to_string(i));Check(history.Count()==64,"History is not bounded");
    EngineScene::GameplaySettings source;source.trigger=true;source.audio=true;source.target="Door with spaces";source.sound="Content/Audio/room sound.wav";source.radius=4;
    std::ostringstream out;EngineScene::WriteGameplay(out,source);std::istringstream in(out.str());std::string tag;in>>tag;EngineScene::GameplaySettings restored;
    Check(EngineScene::ReadGameplay(in,restored)&&restored.target==source.target&&restored.sound==source.sound&&restored.trigger&&restored.radius==4,"Gameplay serialization failed");
    auto previous=restored;std::istringstream invalid("1 1 broken");Check(!EngineScene::ReadGameplay(invalid,restored)&&restored.target==previous.target,"Malformed component replaced valid settings");
    EnginePrefab::Prefab prefab("Regression");prefab.GetRootData().properties["model_settings"]="\"IDKMAT1 material with spaces\" 1 2 3";
    EnginePrefab::Prefab decoded;Check(decoded.DeserializeFromString(prefab.SerializeToString()),"Prefab decode failed");
    Check(decoded.GetRootData().properties==prefab.GetRootData().properties,"Prefab custom settings were discarded");
    std::cout<<"PASS: undo/redo, branch invalidation, history bounds, component roundtrip and rejection, prefab property persistence\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
