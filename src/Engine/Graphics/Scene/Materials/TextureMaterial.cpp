#include "TextureMaterial.h"
#include "System.h"

namespace GS
{

TextureMaterial::TextureMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

bool TextureMaterial::initialize()
{
	return createPhase( 0, 0 ) >= 0;
}

std::vector<VertexElement> TextureMaterial::initLayouts()
{
	return {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, VertexElement::appendOffset },
	};
}

void TextureMaterial::setParams( const PropertyContainer& params )
{
	if( params.exists( "Albedo" ) )
	{
		uint32_t idTexture = params["Albedo"].data<uint32_t>();
		if( System::textures().exists( idTexture ) )
		{
			DMD3D::instance().setSRV( 0, System::textures().get( idTexture )->srv() );
		}
	}
}

}
