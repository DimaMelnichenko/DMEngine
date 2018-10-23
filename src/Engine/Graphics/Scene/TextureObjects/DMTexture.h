#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include "Storage\DMResource.h"
#include <DirectXTex.h>

namespace GS
{

class DMTexture : public DMResource
{
public:
	DMTexture( uint32_t id, const std::string& name, ScratchImage&& image );
	DMTexture( DMTexture&& );
	~DMTexture();

	com_unique_ptr<ID3D11ShaderResourceView>& srv();

	bool createSRV();

	const ScratchImage& image() const;

private:
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
	ScratchImage m_image;

};

}