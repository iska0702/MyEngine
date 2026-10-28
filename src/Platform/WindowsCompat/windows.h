#pragma once

#if defined(_WIN32)
    #if defined(__clang__) || defined(__GNUC__)
        #include_next <windows.h>
    #endif
#else
    #include "../PlatformTypes.h"
#endif
