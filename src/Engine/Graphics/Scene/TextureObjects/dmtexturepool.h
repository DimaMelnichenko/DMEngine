#pragma once

#include <unordered_map>

#include "D3D/DMD3D.h"
#include "DMTexture.h"

class DMTexturePool
{
private:
	DMTexturePool();
	static DMTexturePool* m_instance;

public:
	static DMTexturePool& instance();
	static void close();
	~DMTexturePool();

	bool initialize( const std::wstring& def_texture_file );

	unsigned int load_texture( const std::wstring& file );
	ID3D11ShaderResourceView* texture( unsigned int );

private:	
	std::unordered_map<std::wstring, unsigned int> m_path_to_index;
	std::vector<DMTexture> m_texure_pool;
};

