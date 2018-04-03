#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include "DMResource.h"

namespace GS
{

class DMTexture : public DMResource
{
public:
	DMTexture( uint32_t id, ID3D11ShaderResourceView* );
	DMTexture( DMTexture&& );
	~DMTexture();

	ID3D11ShaderResourceView* GetTexture();

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_texture;

};

}