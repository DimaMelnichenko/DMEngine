#pragma once

#include <unordered_map>

#include "..\..\Common\DM3DObject.h"
#include "DMTexture.h"

class DMTexturePool : public DM3DObject
{
public:
	DMTexturePool( DMD3D* );
	~DMTexturePool();

	bool initialize( const std::wstring& def_texture_file );

	unsigned int load_texture( const std::wstring& file );
	ID3D11ShaderResourceView* texture( unsigned int );

private:
	std::unordered_map<std::wstring, unsigned int> m_path_to_index;
	std::vector<std::unique_ptr<DMTexture>> m_texure_pool;
};

