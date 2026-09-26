#include "VertexLight.h"
#include "System.h"

namespace GS
{

VertexLight::VertexLight()
{

}

VertexLight::~VertexLight()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> VertexLight::initLayouts()
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

bool VertexLight::innerInitialize()
{
	createPhase( 0, 0 );

	DMD3D::instance().createShaderConstantBuffer( sizeof( PSParam ), m_psCB );

	return true;
}

void VertexLight::setParams( const PropertyContainer& params )
{
	// Без параметра Albedo — заглушка: обращение к отсутствующему параметру бросает std::out_of_range
	const uint32_t albedo = params.exists( "Albedo" ) ? params["Albedo"].data<uint32_t>() : 0;
	DMD3D::instance().setSRV( SRVType::ps, 0, System::textures().get( albedo )->srv() );

	// Значения по умолчанию для отсутствующих параметров: белый оттенок, без добавки окружения и цвета земли.
	// Без них структура оставалась неинициализированной, и модель могла выйти чёрной
	PSParam param = { XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ), XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ), XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ) };

	if( params.exists( "Color" ) )
	{	
		param.tintColor = params["Color"].data<XMFLOAT4>();
	}
	if( params.exists( "AmbientColor" ) )
	{
		param.ambientColor = params["AmbientColor"].data<XMFLOAT4>();
	}
	if( params.exists( "blendFactor" ) )
	{
		param.blendFactor = params["blendFactor"].data<XMFLOAT4>();
	}

	Device::updateResourceData<PSParam>( m_psCB.get(), param );
	DMD3D::instance().setConstantBuffer( SRVType::ps, 2, m_psCB );
	
}

}