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
	createPhase( 0, -1, 0 );
	return true;
}

bool TextureShader::Prepare()
{
	return DMShader::setPass( 0 );
}

std::vector<D3D11_INPUT_ELEMENT_DESC> TextureShader::initLayouts()
{
	D3D11_INPUT_ELEMENT_DESC polygonLayout;

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	// Create the vertex input layout description.	
	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	return vertex_layout;
}

void TextureShader::setParams( const ParamSet& params )
{
	if( params.count( "Albedo" ) )
	{
		uint32_t idTexture = params.at( "Albedo" ).textId();
		if( System::textures().exists( idTexture ) )
		{
			ID3D11ShaderResourceView* texture = System::textures().get( idTexture )->srv();
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );
		}
	}
}

}