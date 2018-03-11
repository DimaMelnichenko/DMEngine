#pragma once

#include <unordered_map>

#include "DMResourceStorage.h"
#include "DMTexture.h"

class DMTextureStorage : public DMResourceStorage<DMTexture>
{
public:
	DMTextureStorage( const std::wstring& path );
	~DMTextureStorage();

	bool load( const std::wstring& file );
};

