#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include "DMResource.h"
#include <DirectXTex.h>

namespace GS
{

class DMTexture : public DMResource
{
public:
	DMTexture( uint32_t id, const std::string& name, ScratchImage&& image );
	DMTexture( DMTexture&& );
	~DMTexture();

	ID3D11ShaderResourceView* srv();

	bool createSRV();

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
	ScratchImage m_image;

};

}