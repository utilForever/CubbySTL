#include "base.h"

#if defined(WIN32) || defined(_WIN32)
#   include <Windows.h>
#endif

#if defined(__linux__)
#endif

void* PlatformDepency::Memory::Alloc(unsigned flags)
{
#   if defined(WIN32) || defined(_WIN32) // IF Windows
    return VirtualAlloc(nullptr, getDefaultPageSize(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#   endif
}
void PlatformDepency::Memory::Free(void* context, unsigned flags)
{
#   if defined(WIN32) || defined(_WIN32) // If Windows
    VirtualFree(context, 0, MEM_RELEASE);
#   endif
}
void PlatformDepency::Memory::Lock(void* context, size_t size)
{
#   if defined(WIN32) || defined(_WIN32) // If Windows
    VirtualLock(context, size);
#   endif
}

size_t PlatformDepency::Memory::getDefaultPageSize()
{
#   if defined(WIN32) || defined(_WIN32)
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwPageSize;
#   endif
}