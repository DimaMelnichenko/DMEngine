#pragma once

#include "DirectX.h"
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


bool strToVec2( const std::string& str, XMFLOAT2& vec );
std::string vec2ToStr( const XMFLOAT2& vec );

bool strToVec3( const std::string& str, XMFLOAT3& vec );
std::string vec3ToStr( const XMFLOAT3& vec );

bool strToVec4( const std::string& str, XMFLOAT4& vec );
std::string vec4ToStr( const XMFLOAT4& vec );