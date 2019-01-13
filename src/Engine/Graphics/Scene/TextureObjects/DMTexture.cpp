#include "DMTexture.h"
//#include <D3DX11tex.h>

namespace GS
{

DMTexture::DMTexture( uint32_t id, const std::string& name ) :
	DMResource( id, name )
{
}

DMTexture::DMTexture( DMTexture&& obj ) : DMResource( std::move( obj ) )
{
	
}

DMTexture::~DMTexture()
{

}

}
