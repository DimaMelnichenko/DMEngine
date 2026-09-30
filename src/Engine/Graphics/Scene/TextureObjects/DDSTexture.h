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
	bool createSRV();

	const ShaderView& srv() const override;
	const Texture& texture() const override;

	uint32_t height() const override;
	uint32_t width() const override;

private:
	ScratchImage m_image;
	Texture m_texture;
	ShaderView m_srv;
};

}
