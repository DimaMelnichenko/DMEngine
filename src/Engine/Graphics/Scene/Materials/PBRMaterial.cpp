#include "PBRMaterial.h"
#include <algorithm>
#include "Shaders\slots.h"
#include "System.h"

namespace GS
{

namespace
{

// Текстура материала: 0 — не задана, тогда подставляется defaultTexture
const ShaderView& materialTexture( const PropertyContainer& params, const char* name, uint32_t defaultTexture )
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

PBRMaterial::PBRMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

std::vector<VertexElement> PBRMaterial::initLayouts()
{
	// Основной поток VertexPool (слот 0) — VertexData::PTNTB. Второй поток (слот 1) — данные ветра дерева в половинной
	// точности, VertexData::WindHalf: их читают только вершинные шейдеры с WIND_TREE (материал PBRTree); остальным лишние
	// элементы раскладки не мешают
	return {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, VertexElement::appendOffset },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, VertexElement::appendOffset },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, VertexElement::appendOffset },
		{ "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, VertexElement::appendOffset },
		{ "BRANCH", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 0 },
		{ "BRANCH", 1, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 8 },
		{ "WINDWEIGHTS", 0, DXGI_FORMAT_R16G16_FLOAT, 1, 16 },
	};
}

bool PBRMaterial::initialize()
{
	// Пиксельные шейдеры: 0 — из базы, 1 — он же с отсечением по альфе, 2 — только отсечение по альфе (глубина MASK);
	// с дизерингом смены LOD: 3 — цвет, 4 — цвет с отсечением по альфе, 5 — глубина, 6 — глубина MASK
	const std::optional<ShaderSource> pixel = shaderSource( ShaderStageType::pixel );
	if( !pixel )
		return false;
	const std::string maskDefines = withDefine( pixel->defines, "ALPHA_MASK=1" );
	const std::string pixelDitherDefines = withDefine( pixel->defines, "LOD_DITHER=1" );
	const std::string maskDitherDefines = withDefine( maskDefines, "LOD_DITHER=1" );
	if( !addShaderPassFromFile( ShaderStageType::pixel, pixel->function, pixel->file, maskDefines ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", pixel->file, maskDefines ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, pixel->function, pixel->file, pixelDitherDefines ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, pixel->function, pixel->file, maskDitherDefines ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", pixel->file, pixelDitherDefines ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", pixel->file, maskDitherDefines ) )
		return false;

	// Вершинные шейдеры по [вариант][только глубина]: 0 — из базы, инстансный вариант (INST_MATRIX) — у моделей
	// уровня (у материала расстановки, defines INST_*, свои инстансы), вариант смены LOD дизерингом (LOD_DITHER: у
	// расстановки доля перехода — в экземплярах её списков перехода, у моделей — в константах объекта; без инстансинга
	// моделей) и те же «только глубина» (DEPTH_ONLY: позиция и UV, Shaders/depth_only.sh). Без инстансинга инстансные
	// номера совпадают с обычными
	const std::optional<ShaderSource> vertex = shaderSource( ShaderStageType::vertex );
	if( !vertex )
		return false;
	m_instancing = vertex->defines.find( "INST_" ) == std::string::npos;
	int vertexCount = 1;
	const auto addVertexShader = [&]( const std::string& defines )
	{
		return addShaderPassFromFile( ShaderStageType::vertex, vertex->function, vertex->file, defines ) ? vertexCount++ : -1;
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

std::vector<int> PBRMaterial::depthPhases() const
{
	std::vector<int> phases;
	for( const auto& variant : m_depthPhases )
		for( int phase : variant )
			if( std::find( phases.begin(), phases.end(), phase ) == phases.end() )
				phases.push_back( phase );
	return phases;
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
	DMD3D::instance().setSRV( 0, materialTexture( params, "BaseColor", white ) );
	DMD3D::instance().setSRV( 1, materialTexture( params, "Normal", DMTextureStorage::flatNormalId ) );
	DMD3D::instance().setSRV( 2, materialTexture( params, "MetallicRoughness", white ) );
	DMD3D::instance().setSRV( 3, materialTexture( params, "Occlusion", white ) );
	DMD3D::instance().setSRV( 4, materialTexture( params, "Emissive", white ) );
	DMD3D::instance().setSRV( 5, materialTexture( params, "DiffuseTransmissionColor", white ) );

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

	Device::updateResourceData<PSParam>( m_psCB, param );
	// b2 читают и пиксельный, и вершинный шейдер (отклик на ветер)
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_psCB );
}

}
