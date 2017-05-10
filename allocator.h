#pragma once

#ifndef CUBBY_STL_ALLOCATOR
#define CUBBY_STL_ALLOCATOR

#include "base.h"
#include <stdio.h>
#include <type_traits>
#include <exception>

#pragma warning(push)
#pragma warning(disable:4267)

struct HeapInfo
{
    void* object;
    size_t size;

    HeapInfo* next;
};

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
    
    HeapInfo *m_heapInfo;

    Type* AllocImpl()
    {
#if defined( _DEBUG ) || defined( DEBUG )
        if (m_blockIndex - m_szBlocks == 0)
        {
            throw std::bad_alloc();
        }
#endif // DEBUG

        return (Type*)m_blockStart[m_szBlocks - ++m_blockIndex];
    }
    void FreeImpl(Type* object)
    {
#if defined( _DEBUG ) || defined( DEBUG )
        auto CheckIsFromAllocater = [this, object]() -> bool
        {
            for (HeapInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
            {
                if (
                    ((size_t)heap->object <= (size_t)object) &&
                        ((size_t)Offset(heap->object, heap->size) >= (size_t)object))
                {
                    return true;
                }
             }

            return false;
        };

        // check is this object is from this allocater.
        if (CheckIsFromAllocater() == false)
        {
            throw std::bad_alloc();
        }
#endif // DEBUG

        m_blockStart[m_szBlocks - m_blockIndex--] = object;
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
        for (HeapInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
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
        m_isReserved        = true;
        m_szBlocks          = szReserve;

        size_t szAligned    = AlignAs(szBlock, szAlign);
        void* contextHeap   = PlatformDepency::Memory::Alloc((szAligned * szReserve), 0);

        m_szBlockAligned    = szAligned;

        m_heapInfo          = new HeapInfo;
        m_heapInfo->size    = (szAligned * szReserve);
        m_heapInfo->object  = contextHeap;
        m_heapInfo->next    = nullptr;

        void** contextList  = (void**)PlatformDepency::Memory::Alloc((sizeof(void*) * szReserve), 0);
        m_blockStart        = contextList;
        m_blockIndex        = 0;

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
        
        // Free all heaps.
        for (HeapInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Free(heap->object, 0);
        }
    }

    void Reserve(unsigned Blocks)
    {
        m_szBlocks += Blocks;

        HeapInfo* heapLast = m_heapInfo;
        HeapInfo* heapNext = new HeapInfo;

        // reallocate heap object array.
        void** contextList = (void**)PlatformDepency::Memory::Alloc(sizeof(void*) * m_szBlocks, 0);
        PlatformDepency::Memory::Lock(contextList, sizeof(void*) * m_szBlocks);

        // Iterate heapLast to end.
        while (heapLast->next != nullptr)
        {
            heapLast = heapLast->next;
        }

        // set next heap as heapNext(new heap)
        heapLast->next = heapNext;

        // allocate & initialize heap.
        heapNext->object = PlatformDepency::Memory::Alloc((m_szBlockAligned * Blocks), 0);
        heapNext->size   = Blocks * m_szBlockAligned;
        heapNext->next   = nullptr;

        // lock heap if other heap already locked.
        if (m_isLocked)
        {
            PlatformDepency::Memory::Lock(heapNext->object, heapNext->size);
        }

        // reinitialize heap object array.
        unsigned i = 0;
        for (; i < Blocks; ++i)
        {
            contextList[i] = Offset(heapNext->object, m_szBlockAligned * i);
        }
        for (unsigned index = 0; i < m_szBlocks; ++i)
        {
            contextList[i] = m_blockStart[index++];
        }

        // free old heap object array, and set array as new allocated array.
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
    
    // Template specalization for Create function(no constructor arguments)
    template<> Type* Create<>()
    {
        Type* context = AllocImpl();

        // ============================= NOTE =====================================
        //      IT DOESN'T EFFECTS ON PERFORMANCE BECAUSE COMPILER OPTIMIZATION.
        //      AND ALSO "NEW OPERATOR INITIALIZE" DOES LOT EFFECTS ON PERFORMANCE.
        // ========================================================================
        // if Type is pod. don't call constructor.
        if (std::is_pod<Type>::value == false)
        {
            // call constructor
            new (context) Type();
        }
        return context;
    }

    void Distroy(Type* object)
    {
        // call distructor if type is non-pod
        if( std::is_pod::value == false)
        {
            object->~Type();
        }

        FreeImpl(object);
    }

    void Lock()
    {
        m_isLocked = true;

        for (HeapInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Lock(heap->object, heap->size);
        }
    }
};

#pragma warning(pop)
#endif