#pragma once

#ifndef CUBBY_STL_ALLOCATOR
#define CUBBY_STL_ALLOCATOR

#include "base.h"
#include <stdio.h>
#include <exception>

#pragma warning(push)
#pragma warning(disable:4267)

template<class Type, size_t szBlock = sizeof(Type), size_t szAlign = 8>
class CubbyAllocator
{
protected:
    unsigned m_szBlockAligned;
    unsigned m_szBlocks;

    bool m_isReserved;
    bool m_isLocked;
    
    void**   m_blockStart;
    unsigned m_blockIndex;
    
    struct HeapInfo {
        void* object;
        size_t size;
        
        HeapInfo* next;
    } *m_heapInfo;

    Type* AllocImpl()
    {
        return *(Type**)Offset(m_blockStart, sizeof(void*) * (m_szBlocks - m_blockIndex++));
    }
    void FreeImpl(Type* object)
    {
        memcpy(Offset(m_blockStart, sizeof(void*) * (m_szBlocks - m_blockIndex--)), &object, sizeof(void*));
    }

public:
    // Returns is this allocater Allocable(Usable)
    inline bool isUsable() const { return m_szBlocks != m_blockIndex; }
    // Returns is this allocater already reserved;
    inline bool isReserved() const { return m_isReserved;  }
    // Returns is this allocater fully using;
    inline bool isFull() const { return !isUsable(); }
    // Returns is this allocater heap is locked(not moving, only on RAM)
    inline bool isLocked() const { return m_isLocked; }
    // Returns block that already using;
    
    // Dump current allocater infomation to file stream
    void DumpTo(FILE* fOut)
    {
        fprintf_s(fOut, "=======================================================\n");
        fprintf_s(fOut, "                    ALLOCATER DUMP                     \n");
        fprintf_s(fOut, " - Currnet Allocater, 0x%x                             \n", (void*)this);
        fprintf_s(fOut, "=======================================================\n");
        fprintf_s(fOut, " - Heap Stsuts                                         \n");
        fprintf_s(fOut, "     - isUsable    : %s                                \n", isUsable() ? "true" : "false");
        fprintf_s(fOut, "     - isFull      : %s                                \n", isFull()   ? "true" : "false");
        fprintf_s(fOut, "     - isLocked    : %s                                \n", isLocked() ? "true" : "false");
        fprintf_s(fOut, "=======================================================\n");

        unsigned index;
        for (HeapInfo* heap = m_heapInfo; heap->next != nullptr; heap = heap->next)
        {
            fprintf_s(fOut, " - Heap#%d                                             \n", index);
            fprintf_s(fOut, "     - Heap Object   : 0x%x                            \n", heap->object);
            fprintf_s(fOut, "     - Heap Size     : %d ( 0x%x)                      \n", heap->size, heap->size);
            fprintf_s(fOut, "=======================================================\n");
            index++;
        }
    }

    CubbyAllocator(unsigned szReserve)
    {
        m_isReserved = true;
        m_szBlocks = szReserve;

        size_t szAligned  = AlignAs(szBlock, szAlign);
        void* contextHeap = PlatformDepency::Memory::Alloc((szAligned * szReserve), 0);

        m_szBlockAligned  = szAligned;

        m_heapInfo = new HeapInfo;
        m_heapInfo->size    = (szAligned * szReserve);
        m_heapInfo->object  = contextHeap;
        m_heapInfo->next    = nullptr;

        void** contextList = (void**)PlatformDepency::Memory::Alloc((sizeof(void*) * szReserve), 0);
        m_blockStart = contextList;
        m_blockIndex = 0;

        PlatformDepency::Memory::Lock(contextList, sizeof(void*) * m_szBlocks);

        // Initialize List.
        for (unsigned i = 0; i < szReserve; ++i)
        {
            contextList[i] = Offset(contextHeap, szAligned * i);
        }
    }

    virtual ~CubbyAllocator()
    {
        PlatformDepency::Memory::Free(m_blockStart, 0);
        
        for (HeapInfo* heap = m_heapInfo; heap->next != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Free(heap->object, 0);
            delete heap;
        }
    }

    void Reserve(unsigned Blocks)
    {
        m_szBlocks += Blocks;

        HeapInfo* heapLast = m_heapInfo;
        HeapInfo* heapNext = new HeapInfo;

        void** contextList = (void**)PlatformDepency::Memory::Alloc(sizeof(void*) * m_szBlocks, 0);
        PlatformDepency::Memory::Lock(contextList, sizeof(void*) * m_szBlocks);

        while (heapLast->next != nullptr) heapLast = heapLast->next;
        heapLast->next = heapNext;

        heapNext->object = PlatformDepency::Memory::Alloc((m_szBlockAligned * Blocks), 0);
        heapNext->size   = Blocks * m_szBlockAligned;
        heapNext->next   = nullptr;
        

        unsigned i = 0;
        for (; i < Blocks; ++i)
        {
            contextList[i] = Offset(heapNext->object, m_szBlockAligned * i);
        }
        for (unsigned index = 0; i < m_szBlocks; ++i)
        {
            contextList[i] = m_blockStart[index++];
        }

        PlatformDepency::Memory::Free(m_blockStart, 0);
        m_blockStart = contextList;
    }

    template<class ...ArgumentsTypes>
    Type* Create(ArgumentsTypes... Arguments)
    {
        Type* context = AllocImpl();

        new (context) Type(Arguments...);

        return context;
    }
    void Distroy(Type* object)
    {
        object->~Type();

        FreeImpl(object);
    }

    void Lock()
    {
        for (HeapInfo* heap = m_heapInfo; heap->next != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Lock(heap->object, heap->size);
        }
    }
};

#pragma warning(pop)
#endif
