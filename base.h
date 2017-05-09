#pragma once

#ifndef CUBBY_STD_BASE
#define CUBBY_STD_BASE

#pragma warning(push)
#pragma warning(disable:4312)

namespace PlatformDepency
{
    namespace Memory
    {
        void* Alloc(size_t size, unsigned flags);
        void Free(void* context, unsigned flags);
        void Lock(void* context, size_t size);
    }
}

inline void* Offset(void* base, void* offset)
{
    return (void*)((size_t)(base) + (size_t)(offset));
}
inline void* Offset(void* base, int offset)
{
    return Offset(base, (void*)offset);
}
inline size_t AlignAs(size_t szBlock, size_t szAlign)
{
    if (szBlock % szAlign == 0) return szBlock;
    return (szBlock) - (szBlock % szAlign) + (szAlign);
}
inline size_t AlignAs(size_t szHeader, size_t szBlock, size_t szAlign)
{
    return 
        ((szBlock) - (szBlock % szAlign) + (szAlign)) 
            + ((szHeader) - (szHeader % szAlign) + (szAlign));
}

#pragma warning(pop)
#endif