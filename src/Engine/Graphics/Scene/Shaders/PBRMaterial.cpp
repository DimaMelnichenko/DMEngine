#include "PBRMaterial.h"
#include "Shaders\slots.h"
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
	// Пиксельные шейдеры: 0 — из базы, 1 — он же с отсечением по альфе, 2 — только отсечение (глубина теней MASK)
	const std::optional<ShaderSource> pixel = shaderSource( SRVType::ps );
	if( !pixel )
		return false;
	const std::string maskedDefines = pixel->defines.empty() ? "ALPHA_MASK=1" : pixel->defines + ",ALPHA_MASK=1";
	if( !addShaderPassFromFile( SRVType::ps, pixel->function, pixel->file, maskedDefines ) ||
		!addShaderPassFromFile( SRVType::ps, "mainDepth", pixel->file, pixel->defines ) )
		return false;

	// Инстансный вариант вершинного шейдера — у моделей уровня; у материала расстановки (defines INST_*) свои инстансы
	const std::optional<ShaderSource> vertex = shaderSource( SRVType::vs );
	m_instancing = vertex && vertex->defines.find( "INST_" ) == std::string::npos;
	if( m_instancing )
	{
		const std::string instancedDefines = vertex->defines.empty() ? "INST_MATRIX=1" : vertex->defines + ",INST_MATRIX=1";
		if( !addShaderPassFromFile( SRVType::vs, vertex->function, vertex->file, instancedDefines ) )
			return false;
	}

	// Фазы по [инстансный вершинный шейдер][Masked]: цвет — пиксельный шейдер 0 или 1 (с отсечением), глубина — без
	// пиксельного шейдера или mainDepth (2). Без инстансинга инстансные номера совпадают с обычными
	for( int instanced = 0; instanced < 2; ++instanced )
	{
		const int vertex = instanced && m_instancing ? 1 : 0;
		m_colorPhases[instanced][0] = createPhase( vertex, 0 );
		m_colorPhases[instanced][1] = createPhase( vertex, 1 );
		m_depthPhases[instanced][0] = createPhase( vertex, -1 );
		m_depthPhases[instanced][1] = createPhase( vertex, 2 );
		for( int masked = 0; masked < 2; ++masked )
		{
			if( m_colorPhases[instanced][masked] < 0 || m_depthPhases[instanced][masked] < 0 )
				return false;
		}
	}

	DMD3D::instance().createShaderConstantBuffer( sizeof( PSParam ), m_psCB );

	return true;
}

MaterialRenderState PBRMaterial::renderState( const PropertyContainer& params ) const
{
	MaterialRenderState state;
	switch( materialValue( params, "AlphaMode", 0 ) )
	{
		case 1: state.blendMode = BlendMode::masked; break;
		case 2: state.blendMode = BlendMode::translucent; break;
		default: state.blendMode = BlendMode::opaque; break;
	}
	state.twoSided = materialValue( params, "DoubleSided", false );
	return state;
}

int PBRMaterial::phaseFor( const PropertyContainer& params, bool instanced ) const
{
	return m_colorPhases[instanced ? 1 : 0][renderState( params ).blendMode == BlendMode::masked ? 1 : 0];
}

bool PBRMaterial::supportsInstancing() const
{
	return m_instancing;
}

int PBRMaterial::depthPhaseFor( const PropertyContainer& params, bool instanced ) const
{
	const MaterialRenderState state = renderState( params );
	if( state.blendMode == BlendMode::translucent )
		return -1;
	return m_depthPhases[instanced ? 1 : 0][state.blendMode == BlendMode::masked ? 1 : 0];
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
	param.alphaCutoff = materialValue( params, "AlphaCutoff", 0.5f );

	Device::updateResourceData<PSParam>( m_psCB.get(), param );
	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_MATERIAL, m_psCB );
}

}
