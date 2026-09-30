#pragma once
#include "DDSTexture.h"

namespace GS
{

class CustomTexture : public DMTexture
{
public:
	CustomTexture( uint32_t id, const std::string& name );
	CustomTexture( CustomTexture&& );
	~CustomTexture();

	const ShaderView& srv() const override;
	const Texture& texture() const override;

	bool generateTexture(  );

	uint32_t height() const override
	{
		return m_hight;
	}

	uint32_t width() const override
	{
		return m_width;
	}

private:
	Texture m_texture;
	ShaderView m_srv;
	uint16_t m_width = 256;
	uint16_t m_hight = 256;
};

}
