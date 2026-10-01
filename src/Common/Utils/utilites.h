#pragma once

#include "DirectX.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

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

std::wstring utf8ToWide( const std::string& str );

bool strToVec2( const std::string& str, DirectX::XMFLOAT2& vec );

bool strToVec3( const std::string& str, DirectX::XMFLOAT3& vec );

bool strToVec4( const std::string& str, DirectX::XMFLOAT4& vec );
std::string vec4ToStr( const DirectX::XMFLOAT4& vec );
// "x,y,z" с короткой записью чисел (%g), как пишет Tools/import_gltf.py
std::string vec3ToStr( const DirectX::XMFLOAT3& vec );