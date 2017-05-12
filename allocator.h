#pragma once

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

struct PageInfo
{
    void*  piObject;
    size_t piUsing;
};

template<class AllocType, size_t szAligned = AlignAs(sizeof(AllocType), 4)>
class CubbyAllocator
{
private:
    typedef AllocType*   AllocTypePtr;
    typedef AllocType&   AllocTypeRef;

    std::list<PageInfo*> m_pageList;

    // This array uses LIFO mechanism.
    AllocTypePtr*        m_arrayObject;
    unsigned             m_arraySize;

    unsigned             m_arrayFrontIndex;
    unsigned             m_arrayBackIndex;

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
        if (m_arrayFrontIndex >= m_arraySize)
        {
            throw std::exception("Bad Front Index!");
        }
#endif
        while (pageStart != pageEnd)
        {
            m_arrayObject[m_arrayFrontIndex] = (AllocTypePtr)pageStart;

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
        piNew->piObject = PlatformDepency::Memory::Alloc(0);
        piNew->piUsing  = 0;
        
        // push on page list.
        m_pageList.push_back(piNew);

        return piNew;
    }

    inline void AllocateNewObject(size_t szObject)
    {
        PageInfo* piLast = getLastPage();

        // Resize object array before allocate.
        ResizeObjectArray(szObject + m_arraySize);

        if(CheckLastPageVaild(szObject)) {
            unsigned pageOldUsing = piLast->piUsing; 
            unsigned pageNewUsing = piLast->piUsing + (szAligned * szObject);

            void* pageStart = Offset(piLast->piObject, pageOldUsing);
            void* pageEnd   = Offset(piLast->piObject, pageNewUsing);

            piLast->piUsing = pageNewUsing;

            AddObjectOnArray(pageStart, pageEnd);
        } else {
            // is over one object is allocatable?
            if(CheckLastPageVaild(1))
            {
                unsigned objAllocable = (PlatformDepency::Memory::getDefaultPageSize() - piLast->piUsing) / szAligned;

                unsigned pageOldUsing = piLast->piUsing; 
                unsigned pageNewUsing = piLast->piUsing + (szAligned * objAllocable);

                void* pageStart = Offset(piLast->piObject, pageOldUsing);
                void* pageEnd   = Offset(piLast->piObject, pageNewUsing);

                AddObjectOnArray(pageStart, pageEnd);

                piLast->piUsing = pageNewUsing;
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
        PageInfo* piLast = m_pageList.back();

        if( PlatformDepency::Memory::getDefaultPageSize() - piLast->piUsing >= szObject * szAligned )
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
    CubbyAllocator(unsigned szReserve)
    {
        PageInfo* piNew = AllocateNewPage();

        unsigned pageNewUsing   = (szAligned * szReserve);
        void*    pageEnd        = Offset(piNew->piObject, pageNewUsing);

        ResizeObjectArray(szReserve);
        AddObjectOnArray(piNew->piObject, pageEnd);

        piNew->piUsing = pageNewUsing;
    }
    ~CubbyAllocator()
    {
        for(PageInfo* &info : m_pageList)
        {
            PlatformDepency::Memory::Free(info->piObject, 0);
            delete info;
        }
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

        *newObject = object;

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
        AllocateNewObject(szReserve);
    }
    void Distroy(AllocTypePtr object)
    {
        // call distructor.
        object->~AllocType();

        m_arrayObject[m_arrayFrontIndex] = object;
        m_arrayFrontIndex++;
        m_arrayBackIndex--;
    }
};