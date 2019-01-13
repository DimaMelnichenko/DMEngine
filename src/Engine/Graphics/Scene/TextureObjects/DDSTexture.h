#pragma once

#include "DMTexture.h"
#include <DirectXTex.h>

namespace GS
{

class DDSTexture : public DMTexture
{
public:
	DDSTexture( uint32_t id, const std::string& name, ScratchImage&& image );
	DDSTexture( DDSTexture&& );
	~DDSTexture();

	const ScratchImage& image() const;
	bool createSRV();
	
	const com_unique_ptr<ID3D11ShaderResourceView>& srv() const override;

	uint32_t height() const override;
	uint32_t width() const override;

private:
	ScratchImage m_image;
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
};

}