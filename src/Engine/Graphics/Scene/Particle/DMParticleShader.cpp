#include "DMParticleShader.h"



DMParticleShader::DMParticleShader()
{

}

DMParticleShader::~DMParticleShader()
{
}

void DMParticleShader::Update( float )
{
	
}

void DMParticleShader::prepareRender()
{
	setDrawType( DMShader::by_vertex );
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMParticleShader::initLayouts( )
{
	return std::vector<D3D11_INPUT_ELEMENT_DESC>();
}

void DMParticleShader::setTexture( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );
}

void DMParticleShader::setTextureDistributon( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 0, 1, &texture );
}

void DMParticleShader::setTextureHeight( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->GSSetShaderResources( 1, 1, &texture );
}

void DMParticleShader::StrimOutputDeclaration( D3D11_SO_DECLARATION_ENTRY* entry )
{
	//entry = new D3D11_SO_DECLARATION_ENTRY;

	entry->Stream = 0;
	entry->SemanticName = "POSITION";
	entry->SemanticIndex = 0;
	entry->StartComponent = 0;
	entry->ComponentCount = 4;
	entry->OutputSlot = 0;
}