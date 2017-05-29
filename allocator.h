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

//***************************************************************************************
// class CubbyPage
//      Slower reserve speed than CubbyHeap but faster object 
//      access speed, supports advanced page options, also less heap fragmentation
//
//      - Template Argument
//          - AllocType ; Type to allocate
//          - pages     ; Number of pages to reserve at once
//              Reserving more pages at once can cause more memory uses
//              but, it makes [Reserve] method speed lot faster.
//          - szAligned ; Aligned AllocType size
//              [NOTE] recommend not to change
//      - Methods
//          - CubbyPage(unsigned szReserve)         ; Initialize CubbyPage.
//              - szReserve ; size to reserve
//          - Reserve(unsigned szReserve)           ; Reserve page object.
//              Reserve pages size that [szReserve * szAligned]
//              if [szReserve * szAligned] is bigger than [(default page size) * pages]
//              it will call itself more if more page is needed.
//
//              - szReserve ; size to reserve
//          - Create(ArgumentsTypes... Arguemnts)   ; Allocate and construct object.
//              if AllocType is pod type it will not calling constructor.
//
//              - Arguments ; Construct arguements
//          - Distroy(AllocTypePTr object)          ; Free and distruct object.
//              - object    ; object to Distroy
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

    // Resize(reallocate) memory pool array and reinitialize.
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

    // devide page object, and push objects on pool array.
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

    // allocates new page from os.
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

    // allocate new pages and add devided objects on object pool array
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
    // Pre allocate pages
    CubbyPage(unsigned szReserve)
    {
        AllocateNewPage();
        Reserve(szReserve);
    }
    CubbyPage()
    {
        AllocateNewPage();
    }

    // distroy all pages
    ~CubbyPage()
    {
        for(PageInfo* &info : m_pageList)
        {
            PageFree(info->piObject, 0);
            delete info;
        }

        delete m_arrayObject;
    }

    // creates with copy constructor.
    template<class ...ArgumentTypes>
    AllocTypePtr Create(ArgumentTypes ...Arguments)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        new (newObject) AllocType(Arguments...);

        return newObject;
    }

    // creates without copy constructor, it will calling move constructor.
    template<>
    AllocTypePtr Create<AllocType&&>(AllocType&& object)
    {
        AllocTypePtr newObject = AllocateFromArrayObject();

        std::move(newObject, object);

        return newObject;
    }

    // creates object.
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
    
    // Reserve heaps, and reinitialize heap pool array.
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
    
    // free and distruct target object.
    void Distroy(AllocTypePtr object)
    {
#if defined(DEBUG) || defined(_DEBUG)
        // this checks object that freeing is from this allocator.
        // you can't free object that from other allocator, or new, malloc, etc...
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
        // [m_pageList] cannot be empty! nothing to lock!
        if(m_pageList.empty())
        {
            throw std::exception("Page list is empty!");
        }
#endif
        m_isLocked = true;

        // locks all pages at [m_pageList]
        for(PageInfo* object : m_pageList)
        {
            PageLock(object->piObject, 0);
        }
    }
};

//***************************************************************************************
// class CubbyHeap
//      Faster reserve speed that CubbyPage, but more fragmentation.
//
//      - Methods
//          - Reserve(unsigned szReserve)           ; Reserve page object.
//              Reserve pages size that [szReserve * szAligned]
//              if [szReserve * szAligned] is bigger than [(default page size) * pages]
//              it will call itself more if more page is needed.
//
//              - szReserve ; size to reserve
//          - Create(ArgumentsTypes... Arguemnts)   ; Allocate and construct object.
//              if AllocType is pod type it will not calling constructor.
//
//              - Arguments ; Construct arguements
//          - Distroy(AllocTypePTr object)          ; Free and distruct object.
//              - object    ; object to Distroy
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

    // Resize(reallocate) memory pool array and reinitialize.
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
    
    // devide heap object, and push objects on pool array.
    inline void AddObjectOnArray(void* heapStart, void* heapEnd)
    {
#if defined(DEBUG) || defined(_DEBUG)
        // m_arrayFrontIndex cannot be bigger or eqal at m_arraySize
        if (m_arrayFrontIndex >= m_arraySize)
        {
            throw std::exception("Bad Front Index!");
        }
#endif

        // pushes all devided heap objects on heap pool array.
        // NOTE : need optimize way that incresing [m_arrayBackIndex] and [m_arrayFrontIndex]
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

    // Implementation of Create method, 
    // allocates memory from memory pool array that reserved.
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