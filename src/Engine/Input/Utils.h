#pragma once
#pragma once

#include <d3d11.h>
#include <d3dx10math.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

void delete_com( IUnknown* p );

template<typename T>
struct com_input_deleter
{
	void operator()( T* p ) const
	{
		if( p )
		{
			p->Unacquire();
			p->Release();
			p = nullptr;
		}
	}
};

template<typename T>
using com_input_ptr = std::unique_ptr<T, com_input_deleter<T>>;

template<typename T>
com_input_ptr<T> make_input_ptr( T* p )
{
	return std::move( com_input_ptr<T>( p, com_input_deleter<T>() ) );
}