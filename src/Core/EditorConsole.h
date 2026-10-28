#pragma once
#include "EngineLog.h"
#if defined(_WIN32) && defined(ENGINE_EDITOR)
#include "DiagnosticPaths.h"
#include <fstream>
#include <mutex>
#include <memory>
#include <cstdio>
namespace EngineLog {
class EditorConsole {
    class Output : public std::streambuf {
        EditorConsole& owner; std::streambuf* pipe;
    public:
        Output(EditorConsole& owner, std::streambuf* pipe) : owner(owner),pipe(pipe) {}
        std::streamsize xsputn(const char* data,std::streamsize count) override {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if(pipe)pipe->sputn(data,count);
            if(owner.console!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(owner.console,data,static_cast<DWORD>(count),&written,nullptr);}
            if(owner.file!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(owner.file,data,static_cast<DWORD>(count),&written,nullptr);}
            return count;
        }
        int overflow(int c) override {if(c!=traits_type::eof()){char ch=static_cast<char>(c);xsputn(&ch,1);}return traits_type::not_eof(c);}
        int sync() override {return 0;}
    };
    std::mutex mutex; HANDLE file=INVALID_HANDLE_VALUE;
    HANDLE console=INVALID_HANDLE_VALUE;
    std::streambuf *coutOld=nullptr,*cerrOld=nullptr,*clogOld=nullptr;
    std::unique_ptr<Output> out,error,log;
    static BOOL WINAPI Handle(DWORD event) {
        if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT || event==CTRL_CLOSE_EVENT){closeRequested=true;return TRUE;}
        return FALSE;
    }
public:
    EditorConsole() {
        const bool outPipe=GetFileType(GetStdHandle(STD_OUTPUT_HANDLE))==FILE_TYPE_PIPE;
        const bool errPipe=GetFileType(GetStdHandle(STD_ERROR_HANDLE))==FILE_TYPE_PIPE;
        if(outPipe)SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE),HANDLE_FLAG_INHERIT,0);
        if(errPipe)SetHandleInformation(GetStdHandle(STD_ERROR_HANDLE),HANDLE_FLAG_INHERIT,0);
        if(!GetConsoleWindow())AllocConsole();
        SetConsoleTitleW(L"MyEngine Editor — Console");
        SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
        console=CreateFileW(L"CONOUT$",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        // Keep benchmark pipes intact while also displaying their output.
        FILE* ignored=nullptr;
        if(!outPipe)freopen_s(&ignored,"CONOUT$","w",stdout);
        if(!errPipe)freopen_s(&ignored,"CONOUT$","w",stderr);
        SYSTEMTIME time{};GetLocalTime(&time);
        wchar_t name[128]{};
        swprintf_s(name,L"editor-%04u%02u%02u-%02u%02u%02u-%lu.log",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId());
        auto path=EngineDiagnostics::Directory()/name;
        file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        coutOld=std::cout.rdbuf();cerrOld=std::cerr.rdbuf();clogOld=std::clog.rdbuf();
        out=std::make_unique<Output>(*this,outPipe?coutOld:nullptr);
        error=std::make_unique<Output>(*this,errPipe?cerrOld:nullptr);
        log=std::make_unique<Output>(*this,errPipe?clogOld:nullptr);
        std::cout.rdbuf(out.get());std::cerr.rdbuf(error.get());std::clog.rdbuf(log.get());
        std::cout.clear();std::cerr.clear();std::clog.clear();
        SetConsoleCtrlHandler(Handle,TRUE);
        Write("[Editor] Console ready. Log: "+path.string());
        Write("[Editor] Ctrl+C stops the editor. MYENGINE_RENDER_TRACE=1 enables per-frame render tracing.");
    }
    ~EditorConsole() {
        Write("[Editor] Shutdown complete.");
        std::cout.rdbuf(coutOld);std::cerr.rdbuf(cerrOld);std::clog.rdbuf(clogOld);
        SetConsoleCtrlHandler(Handle,FALSE);
        if(console!=INVALID_HANDLE_VALUE)CloseHandle(console);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    }
};
}
#endif
