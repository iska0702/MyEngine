#pragma once
#if defined(_WIN32)
#include_next <shlobj.h>
#else
#include "windows.h"
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_MYDOCUMENTS 0x000c
inline int SHGetFolderPathW(void*, int, void*, unsigned long, wchar_t*) { return -1; }
inline int SHGetFolderPathA(void*, int, void*, unsigned long, char*) { return -1; }
#endif
