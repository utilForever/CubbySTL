#pragma once

#ifndef CUBBY_STL_ALLOCATOR
#define CUBBY_STL_ALLOCATOR

#include "base.h"
#include <stdio.h>
#include <type_traits>
#include <exception>

#pragma warning(push)
#pragma warning(disable:4267)

struct PageInfo
{
    void*       object;

    size_t      sizeTotal;
    size_t      sizeNow;

    PageInfo* next;
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
    
    PageInfo *m_heapInfo;

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
            for (PageInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
            {
                if (
                    ((size_t)heap->object <= (size_t)object) &&
                        ((size_t)Offset(heap->object, heap->sizeNow) >= (size_t)object))
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

        unsigned index = 0;
        for (PageInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
        {
            fprintf_s(fOut, " - Heap#%d                                             \n", index);
            fprintf_s(fOut, "     - Heap Object   : 0x%x                            \n", heap->object);
            fprintf_s(fOut, "     - Heap Size     : %d ( 0x%x )                     \n", heap->sizeNow, heap->sizeNow);
            fprintf_s(fOut, "=======================================================\n");
            index++;
        }
    }

    CubbyAllocator(unsigned szReserve)
    {
        m_isReserved            = true;
        m_szBlocks              = szReserve;

        size_t szAligned        = AlignAs(szBlock, szAlign);
        void* contextHeap       = PlatformDepency::Memory::Alloc(0);

        m_szBlockAligned        = szAligned;

        m_heapInfo              = new PageInfo;
        m_heapInfo->sizeNow     = szAligned * szReserve;
        m_heapInfo->sizeTotal   = PlatformDepency::Memory::getDefaultPageSize();
        m_heapInfo->object      = contextHeap;
        m_heapInfo->next        = nullptr;

        m_blockStart            = new void*[szReserve];
        m_blockIndex            = 0;

        // Initialize List.
        for (unsigned i = 0; i < szReserve; ++i)
        {
            m_blockStart[i] = Offset(contextHeap, szAligned * i);
        }
    }

    virtual ~CubbyAllocator()
    {
        PlatformDepency::Memory::Free(m_blockStart, 0);
        
        // Free all heaps.
        for (PageInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Free(heap->object, 0);
        }
    }

    //
    // NOTE : NEED REFACTORING. LOOKS DIRTY.
    void Reserve(unsigned Blocks)
    {
        unsigned       szToAllocate = Blocks * m_szBlockAligned;
        PageInfo*      piHeapLast   = m_heapInfo;

        m_szBlocks += Blocks;

        // reallocate heap object array.
        void** contextList = new void*[m_szBlocks];

        // Iterate heapLast to end.
        while (piHeapLast->next != nullptr)
        {
            piHeapLast = piHeapLast->next;
        }

        // if new page doesn't needed.
        if ((piHeapLast->sizeTotal - piHeapLast->sizeNow) >= szToAllocate)
        {
            void* offsetStart = Offset(piHeapLast->object, piHeapLast->sizeNow);

            piHeapLast->sizeNow += szToAllocate;

            unsigned i = 0;

            // Add objects on contextList in new allocated heap.
            for (; i < Blocks; ++i)
            {
                contextList[i] = Offset(offsetStart, m_szBlockAligned * i);
            }
            for (unsigned index = 0; i < m_szBlocks; ++i)
            {
                contextList[i] = m_blockStart[index++];
            }
        }
        else
        {
            unsigned i = 0;

            // If old page object can allocatable. (mean has extra heap)
            if ((piHeapLast->sizeTotal - piHeapLast->sizeNow) > m_szBlockAligned)
            {
                void* offsetStart = Offset(piHeapLast->object, piHeapLast->sizeNow);

                // Add objects on contextList in old page object
                for (; i < (piHeapLast->sizeTotal - piHeapLast->sizeNow) / m_szBlockAligned; ++i)
                {
                    contextList[i] = Offset(offsetStart, m_szBlockAligned * i);
                }

                piHeapLast->sizeNow += m_szBlockAligned * i;
                szToAllocate        -= m_szBlockAligned * i;
            }

            // NOTE : NEW PAGE OBJECT SECTION

            PageInfo* heapNext = new PageInfo;

            // set next heap as heapNext(new heap)
            piHeapLast->next = heapNext;

            // allocate & initialize heap.
            heapNext->object    = PlatformDepency::Memory::Alloc(0);
            heapNext->sizeTotal = PlatformDepency::Memory::getDefaultPageSize();
            heapNext->sizeNow   = szToAllocate;
            heapNext->next      = nullptr;

            // lock heap if other heap already locked.
            if (m_isLocked)
            {
                PlatformDepency::Memory::Lock(heapNext->object, heapNext->sizeTotal);
            }

            // Add objects on contextList in new allocated page.
            for (; i < szToAllocate / m_szBlockAligned; ++i)
            {
                contextList[i] = Offset(heapNext->object, m_szBlockAligned * i);
            }
            for (unsigned index = 0; i < m_szBlocks; ++i)
            {
                contextList[i] = m_blockStart[index++];
            }
        }
        // free old heap object array, and set array as new allocated array.
        delete m_blockStart;
        m_blockStart = contextList;
    }

    template<class ...ArgumentsTypes>
    Type* Create(ArgumentsTypes&&... Arguments)
    {
        Type* context = AllocImpl();

        // ============================= NOTE =====================================
        //      IT DOESN'T EFFECTS ON PERFORMANCE BECAUSE COMPILER OPTIMIZATION.
        //      AND ALSO "NEW OPERATOR INITIALIZE" DOES LOT EFFECTS ON PERFORMANCE.
        // ========================================================================
        // if Type has default constructor or its pod. don't call constructor.
        if (std::is_pod<Type>::value == false)
        {
            // call constructor
            new (context) Type(std::forward<ArgumentsTypes>(Arguments)...);
        }

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
        // if Type has default constructor or its pod. don't call constructor.
        if (std::is_pod<Type>::value == false)
        {
            // call constructor
            new (context) Type();
        }

        return context;
    }

    void Distroy(Type* object)
    {
        // call distructor.
        object->~Type();

        FreeImpl(object);
    }

    void Lock()
    {
        m_isLocked = true;

        for (PageInfo* heap = m_heapInfo; heap != nullptr; heap = heap->next)
        {
            PlatformDepency::Memory::Lock(heap->object, heap->sizeTotal);
        }
    }
};

#pragma warning(pop)
#endif