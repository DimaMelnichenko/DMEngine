#include "TextureShader.h"
#include "System.h"

namespace GS
{

TextureShader::TextureShader()
{

}

TextureShader::~TextureShader()
{

}

bool TextureShader::innerInitialize()
{
	return createPhase( 0, 0 ) >= 0;
}

std::vector<VertexElement> TextureShader::initLayouts()
{
	return {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, VertexElement::appendOffset },
	};
}

void TextureShader::setParams( const PropertyContainer& params )
{
	if( params.exists( "Albedo" ) )
	{
		uint32_t idTexture = params["Albedo"].data<uint32_t>();
		if( System::textures().exists( idTexture ) )
		{
			DMD3D::instance().setSRV( SRVType::ps, 0, System::textures().get( idTexture )->srv() );
		}
	}
}

}
