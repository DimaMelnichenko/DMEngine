#pragma once

//////////////
// INCLUDES //
//////////////
#include "D3D/DMD3D.h"
#include "Storage\DMResource.h"

namespace GS
{

class DMTexture : public DMResource
{
public:
	DMTexture( uint32_t id, const std::string& name );
	DMTexture( DMTexture&& );
	~DMTexture();

	// Вид для шейдеров и сама текстура (копия на CPU — GpuImages::captureTexture)
	virtual const ShaderView& srv() const = 0;
	virtual const Texture& texture() const = 0;

	virtual uint32_t height() const = 0;
	virtual uint32_t width() const = 0;
};

}
