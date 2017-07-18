/*************************************************************************
> File Name: Vector.h
> Project Name: CubbySTL
> Author: Chan-Ho Chris Ohk
> Purpose: Implements a vector, much like the C++ std::vector class.
> Created Time: 2017/07/18
> Copyright (c) 2017, Chan-Ho Chris Ohk
*************************************************************************/
#ifndef CUBBYSTL_VECTOR_H
#define CUBBYSTL_VECTOR_H

#include <memory>
#include <type_traits>

namespace CubbySTL
{
	template <typename T, typename Alloc = std::allocator<T>>
	class Vector
	{
	public:
		typedef T			value_type;
		typedef T*			iterator;
		typedef const T&	const_iterator;
		typedef T*			pointer;
		typedef const T*	const_pointer;
		typedef T&			reference;
		typedef const T&	const_reference;
		typedef size_t		size_type;
		typedef ptrdiff_t	difference_type;

	private:
		T*		m_begin;
		T*		m_end;
		Alloc	m_allocator;
	};
}

#endif