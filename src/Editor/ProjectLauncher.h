#pragma once
#include "Project.h"
#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#pragma comment(lib,"comctl32.lib")
namespace EngineProject {
inline fs::path ExecutableDirectory(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return fs::path(path).parent_path();}
inline fs::path PickProject(bool create,HWND owner=nullptr){
    wchar_t file[32768]{};if(create)wcscpy_s(file,L"NewProject.idkp");
    OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=owner;d.lpstrFilter=L"Idontknow Project (*.idkp)\0*.idkp\0\0";
    d.lpstrFile=file;d.nMaxFile=32768;d.lpstrDefExt=L"idkp";d.lpstrTitle=create?L"Create project":L"Open project";
    d.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(create?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    return (create?GetSaveFileNameW(&d):GetOpenFileNameW(&d))?fs::path(file):fs::path();
}
inline void RegisterFileType(){
    std::wstring command=L"\""+(ExecutableDirectory()/L"Idontknow.exe").wstring()+L"\" \"%1\"";
    auto set=[](const wchar_t* path,const std::wstring& value){HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);}};
    set(L"Software\\Classes\\Idontknow.Project\\shell\\build",L"Build project");
    set(L"Software\\Classes\\Idontknow.Project\\shell\\build\\command",L"\""+(ExecutableDirectory()/L"Idontknow.exe").wstring()+L"\" --build-project \"%1\"");
    set(L"Software\\Classes\\.idkp",L"Idontknow.Project");set(L"Software\\Classes\\Idontknow.Project",L"Idontknow Project");set(L"Software\\Classes\\Idontknow.Project\\shell\\open\\command",command);
}
inline bool LaunchProjectPicker(){
    const TASKDIALOG_BUTTON buttons[]={{101,L"Open project (.idkp)"},{102,L"Create project"}};
    TASKDIALOGCONFIG c{};c.cbSize=sizeof(c);c.pszWindowTitle=L"Idontknow — Projects";c.pszMainInstruction=L"Open or create a project";
    c.pszContent=L"The editor works with .idkp project files.";c.dwCommonButtons=TDCBF_CANCEL_BUTTON;c.pButtons=buttons;c.cButtons=2;c.nDefaultButton=101;
    int chosen=0;if(FAILED(TaskDialogIndirect(&c,&chosen,nullptr,nullptr))||chosen==IDCANCEL)return false;
    auto path=PickProject(chosen==102);if(path.empty())return false;
    if(chosen==102)Create(path,ExecutableDirectory().parent_path()/"src/Content");else Open(path);return true;
}

}
#endif
