#include "DMColorShader.h"
#include "Shaders\slots.h"

namespace GS
{

DMColorShader::DMColorShader()
{

}

DMColorShader::~DMColorShader()
{
	
}


bool DMColorShader::innerInitialize()
{
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_constantBuffer, nullptr ) )
		return false;

	createPhase( 0, 0 );
	return true;
}

std::vector<D3D11_INPUT_ELEMENT_DESC> DMColorShader::initLayouts()
{
	HRESULT result;
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

	return vertex_layout;
}

void DMColorShader::setParams( const PropertyContainer& params )
{
	Device::updateResource<XMFLOAT4>( m_constantBuffer, [&]( XMFLOAT4& v )
	{
		const XMFLOAT4* color = params.exists( "Color" ) ? params["Color"].dataPtr<XMFLOAT4>() : nullptr;
		v = color ? *color : XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );
	} );

	ID3D11Buffer* buffer = m_constantBuffer.get();
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( SLOT_CB_MATERIAL, 1, &buffer );
}

}