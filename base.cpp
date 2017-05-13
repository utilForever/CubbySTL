#include "base.h"

#if defined(WIN32) || defined(_WIN32)
#   include <Windows.h>
#endif

#if defined(__linux__)
#endif

void* PageAlloc(unsigned flags)
{
#   if defined(WIN32) || defined(_WIN32) // IF Windows
    return VirtualAlloc(nullptr, getDefaultPageSize(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#   endif
}
void PageFree(void* context, unsigned flags)
{
#   if defined(WIN32) || defined(_WIN32) // If Windows
    VirtualFree(context, 0, MEM_RELEASE);
#   endif
}
void PageLock(void* context, size_t size)
{
#   if defined(WIN32) || defined(_WIN32) // If Windows
    VirtualLock(context, size);
#   endif
}

size_t getDefaultPageSize()
{
#   if defined(WIN32) || defined(_WIN32)
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwPageSize;
#   endif
}