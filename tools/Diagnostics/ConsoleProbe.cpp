#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <filesystem>
int wmain(int argc,wchar_t** argv){
    if(argc<3)return 2;
    const DWORD process=wcstoul(argv[1],nullptr,10);
    FreeConsole();
    const bool attached=AttachConsole(process)!=FALSE;
    SetConsoleCtrlHandler(nullptr,TRUE);
    wchar_t title[512]{};DWORD ids[64]{};
    const bool window=attached&&GetConsoleWindow()!=nullptr;
    const auto length=attached?GetConsoleTitleW(title,512):0;
    const auto count=attached?GetConsoleProcessList(ids,64):0;
    bool engineAttached=false;for(DWORD i=0;i<count&&i<64;++i)engineAttached|=ids[i]==process;
    std::ofstream report(std::filesystem::path(argv[2]),std::ios::trunc);
    report<<"attached "<<attached<<" console_window "<<window<<" engine_in_console "<<engineAttached<<" title_length "<<length<<'\n';report.flush();
    bool stopped=true;if(argc>3&&wcscmp(argv[3],L"--stop")==0)stopped=GenerateConsoleCtrlEvent(CTRL_C_EVENT,0)!=FALSE;
    if(attached)FreeConsole();
    return attached&&window&&engineAttached&&length&&stopped?0:1;
}
