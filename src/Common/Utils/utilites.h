#pragma once

#include <d3d11.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

void delete_com( IUnknown* p );

template<typename T>
struct com_deleter
{
	void operator()( T* p ) const
	{
		if( p )
		{
			p->Release();
			p = nullptr;
		}
	}
};

template<typename T>
using com_shared_ptr = std::shared_ptr<T>;

template<typename T>
com_shared_ptr<T> make_com_sptr( T* p )
{
	return std::move( com_shared_ptr<T>( p, com_deleter<T>() ) );
}

template<typename T>
using com_unique_ptr = std::unique_ptr<T, com_deleter<T>>;

template<typename T>
com_unique_ptr<T> make_com_ptr( T* p )
{
	return std::move( com_unique_ptr<T>( p, com_deleter<T>() ) );
}

void str_split( const std::string& str, std::vector<std::string>& tokens, const std::string& delimiters = " " );
