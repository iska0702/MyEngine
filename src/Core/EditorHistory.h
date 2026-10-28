#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace EngineEditor {
// Serialized documents avoid retaining deleted GPU resources in the undo stack.
class History {
    std::vector<std::string> states;
    size_t cursor=0;
    static constexpr size_t Budget=32*1024*1024;
public:
    void Reset(std::string state){states.clear();states.push_back(std::move(state));cursor=0;}
    bool Commit(std::string state){
        if(states.empty()){Reset(std::move(state));return false;}
        if(states[cursor]==state)return false;
        states.resize(cursor+1);states.push_back(std::move(state));++cursor;
        size_t bytes=0;for(const auto& item:states)bytes+=item.size();
        while(states.size()>1&&(states.size()>64||bytes>Budget)){
            bytes-=states.front().size();states.erase(states.begin());--cursor;
        }
        return true;
    }
    bool CanUndo()const{return !states.empty()&&cursor>0;}
    bool CanRedo()const{return !states.empty()&&cursor+1<states.size();}
    const std::string* Undo(){return CanUndo()?&states[--cursor]:nullptr;}
    const std::string* Redo(){return CanRedo()?&states[++cursor]:nullptr;}
    size_t Count()const{return states.size();}
};
}
