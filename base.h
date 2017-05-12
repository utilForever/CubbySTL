#pragma once

#ifndef CUBBY_STD_BASE
#define CUBBY_STD_BASE

#pragma warning(push)
#pragma warning(disable:4312)

namespace PlatformDepency
{
    namespace Memory
    {
        void* Alloc(unsigned flags);
        void Free(void* context, unsigned flags);
        void Lock(void* context, size_t size);
        size_t getDefaultPageSize();
    }
}

#pragma warning(pop)
#endif