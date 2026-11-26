#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <vector>
namespace EngineScene {
enum class GameplayActionType { ToggleDoor, ToggleLight, PlaySound, ShowMessage };
struct GameplayAction {
    GameplayActionType type = GameplayActionType::ToggleDoor;
    std::string value;
};
struct DialogueChoice {
    std::string label, next, questFlag;
};
struct GameplaySettings {
    bool trigger=false,door=false,button=false,pickup=false,audio=false;
    float radius=2,doorAngle=90,doorSpeed=90,volume=1,cooldown=1;
    bool once=true;
    std::string target,sound;
    std::vector<GameplayAction> actions;
    std::string dialogue, requiredQuestFlag;
    std::vector<DialogueChoice> choices;
};
inline void WriteGameplay(std::ostream& out,const GameplaySettings& s){
    out<<"GAMEPLAY "<<s.trigger<<' '<<s.door<<' '<<s.button<<' '<<s.pickup<<' '<<s.audio<<' '
       <<s.radius<<' '<<s.doorAngle<<' '<<s.doorSpeed<<' '<<s.volume<<' '<<s.cooldown<<' '
       <<s.once<<' '<<std::quoted(s.target)<<' '<<std::quoted(s.sound);
    const bool hasDialogue = !s.dialogue.empty() || !s.requiredQuestFlag.empty() || !s.choices.empty();
    if (!s.actions.empty() || hasDialogue) {
        out << " ACTIONS " << s.actions.size();
        for (const auto& action : s.actions)
            out << ' ' << static_cast<int>(action.type) << ' ' << std::quoted(action.value);
    }
    if (hasDialogue) {
        out << " DIALOGUE " << std::quoted(s.dialogue) << ' ' << std::quoted(s.requiredQuestFlag)
            << ' ' << s.choices.size();
        for (const auto& choice : s.choices)
            out << ' ' << std::quoted(choice.label) << ' ' << std::quoted(choice.next) << ' ' << std::quoted(choice.questFlag);
    }
    out << '\n';
}
inline bool ReadGameplay(std::istream& in,GameplaySettings& settings){
    GameplaySettings s;
    if(!(in>>s.trigger>>s.door>>s.button>>s.pickup>>s.audio>>s.radius>>s.doorAngle>>s.doorSpeed>>s.volume>>s.cooldown>>s.once>>std::quoted(s.target)>>std::quoted(s.sound)))return false;
    if(!std::isfinite(s.radius)||!std::isfinite(s.doorAngle)||!std::isfinite(s.doorSpeed)||!std::isfinite(s.volume)||!std::isfinite(s.cooldown))return false;
    std::string extension;
    bool readActions = false, readDialogue = false;
    while (in >> extension) {
        size_t count = 0;
        if (extension == "DIALOGUE" && !readDialogue) {
            readDialogue = true;
            if (!(in >> std::quoted(s.dialogue) >> std::quoted(s.requiredQuestFlag) >> count) || count > 4) return false;
            for (size_t i = 0; i < count; ++i) {
                DialogueChoice choice;
                if (!(in >> std::quoted(choice.label) >> std::quoted(choice.next) >> std::quoted(choice.questFlag))) return false;
                s.choices.push_back(std::move(choice));
            }
            continue;
        }
        if (extension != "ACTIONS" || readActions || !(in >> count) || count > 32) return false;
        readActions = true;
        for (size_t i = 0; i < count; ++i) {
            int type;
            GameplayAction action;
            if (!(in >> type >> std::quoted(action.value)) || type < 0 || type > 3) return false;
            action.type = static_cast<GameplayActionType>(type);
            s.actions.push_back(std::move(action));
        }
    }
    s.radius=std::clamp(s.radius,.1f,1000.f);s.doorAngle=std::clamp(s.doorAngle,-360.f,360.f);
    s.doorSpeed=std::clamp(s.doorSpeed,1.f,720.f);s.volume=std::clamp(s.volume,0.f,1.f);s.cooldown=std::clamp(s.cooldown,.05f,3600.f);
    settings=std::move(s);return true;
}
}
