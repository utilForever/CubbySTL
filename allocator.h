#pragma once

#ifndef CUBBY_STD_ALLOCATOR
#define CUBBY_STD_ALLOCATOR

#include "base.h"
#include <stdio.h>
#include <type_traits>
#include <exception>
#include <list>

#pragma warning(push)
#pragma warning(disable:4267)

constexpr size_t AlignAs(size_t szBlock, size_t szAlign)
{
    return (szBlock) - (szBlock % szAlign) + (szAlign);
}

inline void* Offset(void* base, void* offset)
{
    return (void*)((size_t)(base) + (size_t)(offset));
}

inline void* Offset(void* base, size_t offset)
{
    return Offset(base, (void*) offset);
}

template<class AllocType, size_t pages = 1, size_t szAligned = AlignAs(sizeof(AllocType), 4)>
class CubbyPage
{
private:
    struct PageInfo
    {
        void*  piObject;
        size_t piUsing;
    };

    typedef AllocType*   AllocTypePtr;
    typedef AllocType&   AllocTypeRef;

    bool                 m_isLocked;
    std::list<PageInfo*> m_pageList;

    // This array uses LIFO mechanism.
    AllocTypePtr*        m_arrayObject;
    unsigned             m_arraySize;
    unsigned             m_arrayFrontIndex;
    unsigned             m_arrayBackIndex;

    const unsigned       m_defaultPageSize = pages * getDefaultPageSize();

    inline AllocTypePtr AllocateFromArrayObject()
    {
        AllocTypePtr object = m_arrayObject[m_arrayFrontIndex - 1];
        m_arrayFrontIndex--;
        m_arrayBackIndex++;

        return object;
    }

    inline void ResizeObjectArray(unsigned newSize)
    {
        AllocTypePtr*  oldArray      = m_arrayObject;
        AllocTypePtr*  newArray      = new AllocTypePtr[newSize];
        const unsigned newArraySize  = newSize;

        // copy old array object to new object.
        // TODO : use memcpy
        for (unsigned i = 0; i < m_arrayFrontIndex; ++i)
        {
            newArray[i] = oldArray[i];
        }

        // replace array object with new array.
        m_arrayObject       = newArray;
        m_arraySize         = newArraySize;


        // delete old once
        if(oldArray != nullptr)
        {
            delete oldArray;
        }
    }

    inline void AddObjectOnArray(void* pageStart, void* pageEnd)
    {
#if defined(DEBUG) || defined(_DEBUG)
        // Index SHOULD NOT over than Size.
        if (m_arrayFrontIndex > m_arraySize)
        {
            throw std::exception("Bad Front Index!");
        }

        if (pageStart > pageEnd)
        {
            throw std::exception("end cannot be less than start");
        }
#endif
        while (pageStart != pageEnd)
        {
            m_arrayObject[m_arrayFrontIndex] = (AllocTypePtr)pageStart;

            // refresh index.
            if (m_arrayFrontIndex == m_arraySize - m_arrayBackIndex)
            {
                m_arrayBackIndex++;
            }
            m_arrayFrontIndex++;


            pageStart = Offset(pageStart, szAligned);
        }
    }

    inline PageInfo* AllocateNewPage()
    {
        PageInfo* piNew = new PageInfo();

        // initialize new PageInfo object.
        piNew->piObject = PageAlloc(pages ,0);
        piNew->piUsing  = 0;

        if(m_isLocked == true)
        {
            PageLock(piNew->piObject, 0);
        }
        
        // push on page list.
        m_pageList.push_back(piNew);

        return piNew;
    }

    inline void AllocateNewObject(size_t szObject)
    {
        PageInfo* piLast = getLastPage();

        // Resize object array before allocate.
        ResizeObjectArray(szObject + m_arraySize);

        // if last page is allocatable. use it.
        if(CheckLastPageVaild(szObject)) {
            unsigned pageOldUsing = piLast->piUsing; 
            unsigned pageNewUsing = piLast->piUsing + (szAligned * szObject);

            void* pageStart = Offset(piLast->piObject, pageOldUsing);
            void* pageEnd   = Offset(piLast->piObject, pageNewUsing);

            piLast->piUsing = pageNewUsing;

            AddObjectOnArray(pageStart, pageEnd);
        } else { // If not allocatable on last page.

            // is over one object is allocatable?
            if(CheckLastPageVaild(1))
            {
                unsigned objAllocable = (m_defaultPageSize - piLast->piUsing) / szAligned;

                unsigned pageOldUsing = piLast->piUsing; 
                unsigned pageNewUsing = piLast->piUsing + (szAligned * objAllocable);

                void* pageStart = Offset(piLast->piObject, pageOldUsing);
                void* pageEnd   = Offset(piLast->piObject, pageNewUsing);

                AddObjectOnArray(pageStart, pageEnd);

                piLast->piUsing = pageNewUsing;

                // Sub szObject size that allocated from old page.
                szObject -= objAllocable;
            }

            PageInfo* piNew = AllocateNewPage();

            unsigned pageNewSize = szObject * szAligned;
            void*    pageStart   = piNew->piObject;
            void*    pageEnd     = Offset(piNew->piObject, pageNewSize);
            
            AddObjectOnArray(pageStart, pageEnd);

            piNew->piUsing = pageNewSize;
        }
    }

    inline bool CheckLastPageVaild(size_t szObject)
    {
#if defined(DEBUG) || defined(_DEBUG)
        if(m_pageList.empty())
        {
            throw std::exception("Page list is empty!");
        }
#endif
        PageInfo* piLast = m_pageList.back();

        if( m_defaultPageSize - piLast->piUsing > szObject * szAligned )
        {
            return true;
        }
        return false;
    }

    inline PageInfo* getLastPage()
    {
        return m_pageList.back();
    }

public:
    CubbyPage(unsigned szReserve)
    {
        AllocateNewPage();
        Reserve(szReserve);
    }

    CubbyPage()
    {
        AllocateNewPage();
    }

    ~CubbyPage()
    {
        for(PageInfo* &info : m_pageList)
        {
            PageFree(info->piObject, 0);
            delete info;
        }

        delete m_arrayObject;
    }
    template<class ...ArgumentTypes>
    AllocTypePtr Create(ArgumentTypes ...Arguments)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        new (newObject) AllocType(Arguments...);

        return newObject;
    }

    template<>
    AllocTypePtr Create<AllocType&&>(AllocType&& object)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        std::move(newObject, object);

        return newObject;
    }

    template<>
    AllocTypePtr Create<>()
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        // ============================= NOTE =====================================
        //      IT DOESN'T EFFECTS ON PERFORMANCE BECAUSE COMPILER OPTIMIZATION.
        //      AND ALSO "NEW OPERATOR INITIALIZE" DOES LOT EFFECTS ON PERFORMANCE.
        // ========================================================================
        // if Type is pod. don't call constructor.
        if(std::is_pod<AllocType>::value == false)
        {
            new (newObject) AllocType();
        }

        return newObject;
    }
    
    void Reserve(size_t szReserve)
    {
        // NEED REFACTORING.
        // if szReserve is larger than default page size.
        if (szReserve * szAligned > m_defaultPageSize)
        {
            unsigned szToReserve = (m_defaultPageSize / szAligned);

            while (szReserve * szAligned > m_defaultPageSize)
            {
                AllocateNewObject(szToReserve);
                szReserve -= szToReserve;
            }
        }

        AllocateNewObject(szReserve);
    }
    
    void Distroy(AllocTypePtr object)
    {
#if defined(DEBUG) || defined(_DEBUG)
        auto CheckIsObjectFromAllocator = [this, object]() -> bool
        {
            for(PageInfo* &info : m_pageList)
            {
                if((size_t)info->piObject <= (size_t)object)
                {
                    if((size_t)Offset(info->piObject, info->piUsing) >= (size_t)object)
                    {
                        return true;
                    }
                }
            }

            return false;
        };

        if(CheckIsObjectFromAllocator() == false)
        {
            throw std::exception("Bad Object Pointer!");
        }
#endif // DEBUG

        // call distructor.
        if(std::is_pod<AllocType>::value == false)
        {
            object->~AllocType();
        }

        m_arrayObject[m_arrayFrontIndex] = object;
        m_arrayFrontIndex++;
        m_arrayBackIndex--;
    }

    void Lock()
    {
#if defined(DEBUG) || defined(_DEBUG)
        if(m_pageList.empty())
        {
            throw std::exception("Page list is empty!");
        }
#endif

        m_isLocked = true;

        for(PageInfo* object : m_pageList)
        {
            PageLock(object->piObject, 0);
        }
    }
};

template<class AllocType>
class CubbyHeap
{
    typedef AllocType*          AllocTypePtr;
    typedef AllocType&          AllocTypeRef;

    unsigned                    m_heapAllocated;
    std::list<AllocTypePtr>     m_heapList;
    
    AllocTypePtr*               m_arrayObject;
    unsigned                    m_arraySize;
    unsigned                    m_arrayFrontIndex;
    unsigned                    m_arrayBackIndex;

    inline void ResizeObjectArray(unsigned newSize)
    {
        AllocTypePtr*  oldArray      = m_arrayObject;
        AllocTypePtr*  newArray      = new AllocTypePtr[newSize];
        const unsigned newArraySize  = newSize;

        // copy old array object to new object.
        for (unsigned i = 0; i < m_arrayFrontIndex; ++i)
        {
            newArray[i] = oldArray[i];
        }

        // replace array object with new array.
        m_arrayObject       = newArray;
        m_arraySize         = newArraySize;

        // delete old once
        if(oldArray != nullptr)
        {
            delete oldArray;
        }
    }
    inline void AddObjectOnArray(void* heapStart, void* heapEnd)
    {
#if defined(DEBUG) || defined(_DEBUG)
        if (m_arrayFrontIndex >= m_arraySize)
        {
            throw std::exception("Bad Front Index!");
        }
#endif
        while (heapStart != heapEnd)
        {
            m_arrayObject[m_arrayFrontIndex] = (AllocTypePtr)heapStart;
            
            if (m_arrayFrontIndex == m_arraySize - m_arrayBackIndex)
            {
                m_arrayBackIndex++;
            }
            m_arrayFrontIndex++;

            heapStart = Offset(heapStart, sizeof(AllocType));
        }
    }
    inline AllocTypePtr AllocateFromArrayObject()
    {
        AllocTypePtr object = m_arrayObject[m_arrayFrontIndex - 1];
        m_arrayFrontIndex--;
        m_arrayBackIndex++;

        return object;
    }

public:
    void Reserve(size_t szBlock)
    {
        AllocTypePtr object = new AllocType[szBlock];

        m_heapList.push_back(object);
        ResizeObjectArray(m_heapAllocated + szBlock);
        AddObjectOnArray(object, Offset(object, szBlock* sizeof(AllocType)));
    }
    template<class ...ArgumentTypes>
    AllocTypePtr Create(ArgumentTypes ...Arguments)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        new (newObject) AllocType(Arguments...);

        return newObject;
    }

    template<>
    AllocTypePtr Create<AllocType&&>(AllocType&& object)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        std::move(newObject, object);

        return newObject;
    }

    template<>
    AllocTypePtr Create<>()
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        // ============================= NOTE =====================================
        //      IT DOESN'T EFFECTS ON PERFORMANCE BECAUSE COMPILER OPTIMIZATION.
        //      AND ALSO "NEW OPERATOR INITIALIZE" DOES LOT EFFECTS ON PERFORMANCE.
        // ========================================================================
        // if Type is pod. don't call constructor.
        if (std::is_pod<AllocType>::value == false)
        {
            new (newObject) AllocType();
        }

        return newObject;
    }
    void Distroy(AllocTypePtr object)
    {
        // call distructor.
        if (std::is_pod<AllocType>::value == false)
        {
            object->~AllocType();
        }

        m_arrayObject[m_arrayFrontIndex] = object;
        m_arrayFrontIndex++;
        m_arrayBackIndex--;
    }
};

#pragma warning(pop)
#endif