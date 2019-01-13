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

	virtual const com_unique_ptr<ID3D11ShaderResourceView>& srv() const = 0;

	virtual uint32_t height() const = 0;
	virtual uint32_t width() const = 0;




};

}