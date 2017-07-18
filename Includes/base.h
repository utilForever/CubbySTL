#pragma once

#ifndef CUBBY_STD_BASE
#define CUBBY_STD_BASE

#pragma warning(push)
#pragma warning(disable:4312)

void* PageAlloc(unsigned pages, unsigned flags);
void PageFree(void* context, unsigned flags);
void PageLock(void* context, size_t size);
size_t getDefaultPageSize();


#pragma warning(pop)
#endif