#include "DMLightShader.h"


DMLightShader::DMLightShader()
{

}

DMLightShader::~DMLightShader()
{
	Shutdown();
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMLightShader::initLayouts()
{	
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	
	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	// Create the vertex input layout description.
	// This setup needs to match the VertexType stucture in the ModelClass and in the shader.
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

	polygonLayout.SemanticName = "NORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TANGENT";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "BINORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	return vertex_layout;
}

void DMLightShader::Shutdown()
{
	return;
}

bool DMLightShader::setTexture( DMModel::TextureType type, ID3D11ShaderResourceView* texture )
{
	// Set shader texture resource in the pixel shader.
	switch( type )
	{
		case DMModel::albedo:
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &texture );
			break;
		case DMModel::normal:
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 1, 1, &texture );
			break;
		case DMModel::height:
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 2, 1, &texture );
			break;
		case DMModel::gim:
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 3, 1, &texture );
			break;
	}

	return true;
}

bool DMLightShader::setCubeTexture( ID3D11ShaderResourceView* texture )
{
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 5, 1, &texture );

	return true;
}



void DMLightShader::prepareRender( )
{
	return;
}

void DMLightShader::Update( float )
{

}

bool DMLightShader::setLights( DMLightDriver* _light_driver )
{
	_light_driver->setShaderBuffer();

	return true;
}