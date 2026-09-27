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

// Defines варианта шейдера: исходные из базы плюс ещё один
std::string withDefine( const std::string& defines, const char* define )
{
	return defines.empty() ? std::string( define ) : defines + "," + define;
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
	if( !addShaderPassFromFile( SRVType::ps, pixel->function, pixel->file, withDefine( pixel->defines, "ALPHA_MASK=1" ) ) ||
		!addShaderPassFromFile( SRVType::ps, "mainDepth", pixel->file, pixel->defines ) )
		return false;

	// Вершинные шейдеры по [инстансный][только глубина]: 0 — из базы, инстансный вариант (INST_MATRIX) — у моделей
	// уровня (у материала расстановки, defines INST_*, свои инстансы), и те же «только глубина» (DEPTH_ONLY: позиция
	// и UV, Shaders/depth_only.sh). Без инстансинга инстансные номера совпадают с обычными
	const std::optional<ShaderSource> vertex = shaderSource( SRVType::vs );
	if( !vertex )
		return false;
	m_instancing = vertex->defines.find( "INST_" ) == std::string::npos;
	int vertexCount = 1;
	const auto addVertexShader = [&]( const std::string& defines )
	{
		return addShaderPassFromFile( SRVType::vs, vertex->function, vertex->file, defines ) ? vertexCount++ : -1;
	};
	int vertexShaders[2][2] = {};
	vertexShaders[0][1] = addVertexShader( withDefine( vertex->defines, "DEPTH_ONLY=1" ) );
	if( m_instancing )
	{
		const std::string instancedDefines = withDefine( vertex->defines, "INST_MATRIX=1" );
		vertexShaders[1][0] = addVertexShader( instancedDefines );
		vertexShaders[1][1] = addVertexShader( withDefine( instancedDefines, "DEPTH_ONLY=1" ) );
	}
	else
	{
		vertexShaders[1][0] = vertexShaders[0][0];
		vertexShaders[1][1] = vertexShaders[0][1];
	}
	for( const auto& shaders : vertexShaders )
	{
		if( shaders[0] < 0 || shaders[1] < 0 )
			return false;
	}

	// Фазы по [инстансный][Masked]: цвет — пиксельный шейдер 0 или 1 (с отсечением), глубина — вершинный «только
	// глубина» без пиксельного шейдера или с mainDepth (2)
	for( int instanced = 0; instanced < 2; ++instanced )
	{
		const int color = vertexShaders[instanced][0];
		const int depth = vertexShaders[instanced][1];
		m_colorPhases[instanced][0] = createPhase( color, 0 );
		m_colorPhases[instanced][1] = createPhase( color, 1 );
		m_depthPhases[instanced][0] = createPhase( depth, -1 );
		m_depthPhases[instanced][1] = createPhase( depth, 2 );
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

int PBRMaterial::phaseFor( const PropertyContainer& params, bool instanced, bool maskedInDepthPrepass ) const
{
	const bool clipAlpha = renderState( params ).blendMode == BlendMode::masked && !maskedInDepthPrepass;
	return m_colorPhases[instanced ? 1 : 0][clipAlpha ? 1 : 0];
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
