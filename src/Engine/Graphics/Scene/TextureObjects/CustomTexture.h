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

	const com_unique_ptr<ID3D11ShaderResourceView>& srv() const override;

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
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
	uint16_t m_width = 256;
	uint16_t m_hight = 256;
};

}