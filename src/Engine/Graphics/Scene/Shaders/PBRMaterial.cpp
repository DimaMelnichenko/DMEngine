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

	// Второй поток VertexPool (слот 1) — данные ветра дерева в половинной точности, VertexData::WindHalf. Их читают
	// только вершинные шейдеры с WIND_TREE (материал PBRTree); остальным лишние элементы раскладки не мешают
	const struct { const char* semantic; UINT index; DXGI_FORMAT format; UINT offset; } windElements[] = {
		{ "BRANCH", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0 },
		{ "BRANCH", 1, DXGI_FORMAT_R16G16B16A16_FLOAT, 8 },
		{ "WINDWEIGHTS", 0, DXGI_FORMAT_R16G16_FLOAT, 16 },
	};
	for( const auto& element : windElements )
	{
		polygonLayout.SemanticName = element.semantic;
		polygonLayout.SemanticIndex = element.index;
		polygonLayout.Format = element.format;
		polygonLayout.InputSlot = 1;
		polygonLayout.AlignedByteOffset = element.offset;
		polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		polygonLayout.InstanceDataStepRate = 0;
		vertex_layout.push_back( polygonLayout );
	}

	return vertex_layout;
}

bool PBRMaterial::innerInitialize()
{
	// Пиксельные шейдеры: 0 — из базы, 1 — он же с отсечением по альфе, 2 — только отсечение по альфе (глубина MASK);
	// с дизерингом смены LOD: 3 — цвет, 4 — цвет с отсечением по альфе, 5 — глубина, 6 — глубина MASK
	const std::optional<ShaderSource> pixel = shaderSource( SRVType::ps );
	if( !pixel )
		return false;
	const std::string maskDefines = withDefine( pixel->defines, "ALPHA_MASK=1" );
	const std::string pixelDitherDefines = withDefine( pixel->defines, "LOD_DITHER=1" );
	const std::string maskDitherDefines = withDefine( maskDefines, "LOD_DITHER=1" );
	if( !addShaderPassFromFile( SRVType::ps, pixel->function, pixel->file, maskDefines ) ||
		!addShaderPassFromFile( SRVType::ps, "mainDepth", pixel->file, maskDefines ) ||
		!addShaderPassFromFile( SRVType::ps, pixel->function, pixel->file, pixelDitherDefines ) ||
		!addShaderPassFromFile( SRVType::ps, pixel->function, pixel->file, maskDitherDefines ) ||
		!addShaderPassFromFile( SRVType::ps, "mainDepth", pixel->file, pixelDitherDefines ) ||
		!addShaderPassFromFile( SRVType::ps, "mainDepth", pixel->file, maskDitherDefines ) )
		return false;

	// Вершинные шейдеры по [вариант][только глубина]: 0 — из базы, инстансный вариант (INST_MATRIX) — у моделей
	// уровня (у материала расстановки, defines INST_*, свои инстансы), вариант смены LOD дизерингом (LOD_DITHER: у
	// расстановки доля перехода — в экземплярах её списков перехода, у моделей — в константах объекта; без инстансинга
	// моделей) и те же «только глубина» (DEPTH_ONLY: позиция и UV, Shaders/depth_only.sh). Без инстансинга инстансные
	// номера совпадают с обычными
	const std::optional<ShaderSource> vertex = shaderSource( SRVType::vs );
	if( !vertex )
		return false;
	m_instancing = vertex->defines.find( "INST_" ) == std::string::npos;
	int vertexCount = 1;
	const auto addVertexShader = [&]( const std::string& defines )
	{
		return addShaderPassFromFile( SRVType::vs, vertex->function, vertex->file, defines ) ? vertexCount++ : -1;
	};
	int vertexShaders[vertexVariantCount][2] = {};
	vertexShaders[vertexDefault][1] = addVertexShader( withDefine( vertex->defines, "DEPTH_ONLY=1" ) );
	if( m_instancing )
	{
		const std::string instancedDefines = withDefine( vertex->defines, "INST_MATRIX=1" );
		vertexShaders[vertexInstanced][0] = addVertexShader( instancedDefines );
		vertexShaders[vertexInstanced][1] = addVertexShader( withDefine( instancedDefines, "DEPTH_ONLY=1" ) );
	}
	else
	{
		vertexShaders[vertexInstanced][0] = vertexShaders[vertexDefault][0];
		vertexShaders[vertexInstanced][1] = vertexShaders[vertexDefault][1];
	}
	const std::string vertexDitherDefines = withDefine( vertex->defines, "LOD_DITHER=1" );
	vertexShaders[vertexLodDither][0] = addVertexShader( vertexDitherDefines );
	vertexShaders[vertexLodDither][1] = addVertexShader( withDefine( vertexDitherDefines, "DEPTH_ONLY=1" ) );
	for( const auto& shaders : vertexShaders )
	{
		if( shaders[0] < 0 || shaders[1] < 0 )
			return false;
	}

	// Фазы: цвет — пиксельный шейдер 0 или 1 (с отсечением по альфе), с дизерингом — 3 или 4; глубина — вершинный
	// «только глубина» без пиксельного шейдера или с mainDepth (2), с дизерингом — всегда с mainDepth (5 или 6)
	for( int variant = 0; variant < vertexVariantCount; ++variant )
	{
		const bool dither = variant == vertexLodDither;
		const int color = vertexShaders[variant][0];
		const int depth = vertexShaders[variant][1];
		for( int masked = 0; masked < 2; ++masked )
		{
			int* colorPhases = m_colorPhases[variant][masked];
			colorPhases[0] = createPhase( color, masked );
			colorPhases[1] = dither ? createPhase( color, masked ? 4 : 3 ) : colorPhases[0];
			m_depthPhases[variant][masked] = dither ? createPhase( depth, masked ? 6 : 5 ) : createPhase( depth, masked ? 2 : -1 );
			if( colorPhases[0] < 0 || colorPhases[1] < 0 || m_depthPhases[variant][masked] < 0 )
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
	state.ditheredLodTransition = materialValue( params, "DitheredLODTransition", false );
	return state;
}

PBRMaterial::VertexVariant PBRMaterial::vertexVariant( const ShaderPhaseOptions& options )
{
	return options.lodDither ? vertexLodDither : options.instanced ? vertexInstanced : vertexDefault;
}

int PBRMaterial::phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options ) const
{
	// После depth prepass отсечения нет: маска и дизеринг уже в глубине, проверка EQUAL
	const bool clipAlpha = renderState( params ).blendMode == BlendMode::masked && !options.depthFromPrepass;
	const bool clipDither = options.lodDither && !options.depthFromPrepass;
	return m_colorPhases[vertexVariant( options )][clipAlpha ? 1 : 0][clipDither ? 1 : 0];
}

bool PBRMaterial::supportsInstancing() const
{
	return m_instancing;
}

int PBRMaterial::depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options ) const
{
	const MaterialRenderState state = renderState( params );
	if( state.blendMode == BlendMode::translucent )
		return -1;
	return m_depthPhases[vertexVariant( options )][state.blendMode == BlendMode::masked ? 1 : 0];
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
	DMD3D::instance().setSRV( SRVType::ps, 5, materialTexture( params, "DiffuseTransmissionColor", white ) );

	PSParam param;
	param.baseColorFactor = materialValue( params, "BaseColorFactor", XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	param.emissiveFactor = materialValue( params, "EmissiveFactor", XMFLOAT3( 0.0f, 0.0f, 0.0f ) );
	param.metallic = materialValue( params, "Metallic", 0.0f );
	param.roughness = materialValue( params, "Roughness", 0.5f );
	param.normalScale = materialValue( params, "NormalScale", 1.0f );
	param.normalGreenUp = materialValue( params, "NormalGreenUp", false ) ? 1.0f : 0.0f;
	param.occlusionStrength = materialValue( params, "OcclusionStrength", 1.0f );
	param.alphaCutoff = materialValue( params, "AlphaCutoff", 0.5f );
	param.windWeight = materialValue( params, "WindWeight", 0.0f );
	param.diffuseTransmissionColorFactor = materialValue( params, "DiffuseTransmissionColorFactor", XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	param.diffuseTransmissionFactor = materialValue( params, "DiffuseTransmissionFactor", 0.0f );
	param.windGlobalAngle = XMConvertToRadians( materialValue( params, "WindGlobalAngle", 1.5f ) );
	param.windGlobalExponent = materialValue( params, "WindGlobalExponent", 1.5f );
	param.windGlobalFrequency = materialValue( params, "WindGlobalFrequency", 0.25f );
	param.windBranchAngle = XMConvertToRadians( materialValue( params, "WindBranchAngle", 6.0f ) );
	param.windBranchFrequency = materialValue( params, "WindBranchFrequency", 0.8f );
	param.windTwigAngle = XMConvertToRadians( materialValue( params, "WindTwigAngle", 12.0f ) );
	param.windTwigFrequency = materialValue( params, "WindTwigFrequency", 2.0f );
	param.windRippleAmplitude = materialValue( params, "WindRippleAmplitude", 0.02f );
	param.windRippleFrequency = materialValue( params, "WindRippleFrequency", 6.0f );

	Device::updateResourceData<PSParam>( m_psCB.get(), param );
	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_MATERIAL, m_psCB );
	// Вершинному шейдеру — отклик на ветер
	DMD3D::instance().setConstantBuffer( SRVType::vs, SLOT_CB_MATERIAL, m_psCB );
}

}
