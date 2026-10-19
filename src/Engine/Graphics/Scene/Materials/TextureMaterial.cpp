#include "TextureMaterial.h"
#include "Assets.h"

namespace GS
{

TextureMaterial::TextureMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

bool TextureMaterial::initialize()
{
	// Одна фаза на все вызовы: ключ без признаков
	setPermutation( 0, createPhase( 0, 0 ) );
	return permutation( 0 ) >= 0;
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
		if( Assets::textures().exists( idTexture ) )
		{
			DMD3D::instance().setSRV( 0, Assets::textures().get( idTexture )->srv() );
		}
	}
}

}
