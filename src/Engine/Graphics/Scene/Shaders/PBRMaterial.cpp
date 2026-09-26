#include "PBRMaterial.h"
#include "System.h"

namespace GS
{

namespace
{

// Текстура материала: 0 — не задана, тогда подставляется defaultTexture
const com_unique_ptr<ID3D11ShaderResourceView>& materialTexture( const PropertyContainer& params, const char* name, uint32_t defaultTexture )
{
	uint32_t id = params.exists( name ) ? params[name].data<uint32_t>() : 0;
	return System::textures().get( id != 0 ? id : defaultTexture )->srv();
}

template<class TYPE>
TYPE materialValue( const PropertyContainer& params, const char* name, const TYPE& defaultValue )
{
	return params.exists( name ) ? params[name].data<TYPE>() : defaultValue;
}

}

PBRMaterial::PBRMaterial()
{

}

PBRMaterial::~PBRMaterial()
{

}

std::vector<D3D11_INPUT_ELEMENT_DESC> PBRMaterial::initLayouts()
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

bool PBRMaterial::innerInitialize()
{
	createPhase( 0, 0 );

	DMD3D::instance().createShaderConstantBuffer( sizeof( PSParam ), m_psCB );

	return true;
}

void PBRMaterial::setParams( const PropertyContainer& params )
{
	// Значения по умолчанию совпадают с default_value в MaterialParameterDef: набор параметров другого материала
	// (у старых моделей такое бывает) не роняет отрисовку
	const uint32_t white = DMTextureStorage::whiteId;
	DMD3D::instance().setSRV( SRVType::ps, 0, materialTexture( params, "BaseColor", white ) );
	DMD3D::instance().setSRV( SRVType::ps, 1, materialTexture( params, "Normal", DMTextureStorage::flatNormalId ) );
	DMD3D::instance().setSRV( SRVType::ps, 2, materialTexture( params, "MetallicRoughness", white ) );
	DMD3D::instance().setSRV( SRVType::ps, 3, materialTexture( params, "Occlusion", white ) );
	DMD3D::instance().setSRV( SRVType::ps, 4, materialTexture( params, "Emissive", white ) );

	PSParam param;
	param.baseColorFactor = materialValue( params, "BaseColorFactor", XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	param.emissiveFactor = materialValue( params, "EmissiveFactor", XMFLOAT3( 0.0f, 0.0f, 0.0f ) );
	param.metallic = materialValue( params, "Metallic", 0.0f );
	param.roughness = materialValue( params, "Roughness", 0.5f );
	param.normalScale = materialValue( params, "NormalScale", 1.0f );
	param.normalGreenUp = materialValue( params, "NormalGreenUp", false ) ? 1.0f : 0.0f;
	param.occlusionStrength = materialValue( params, "OcclusionStrength", 1.0f );

	Device::updateResourceData<PSParam>( m_psCB.get(), param );
	DMD3D::instance().setConstantBuffer( SRVType::ps, 2, m_psCB );
}

}
