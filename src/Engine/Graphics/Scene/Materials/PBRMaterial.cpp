#include "PBRMaterial.h"
#include <algorithm>
#include "Shaders\slots.h"
#include "System.h"

using namespace DirectX;

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

// Defines без экземпляров (INST_*): у материала расстановки экземпляр — из пула, а запеканию нужна матрица объекта
std::string withoutInstances( const std::string& defines )
{
	std::string result;
	size_t begin = 0;
	while( begin <= defines.size() )
	{
		const size_t end = std::min( defines.find( ',', begin ), defines.size() );
		const std::string define = defines.substr( begin, end - begin );
		if( !define.empty() && define.rfind( "INST_", 0 ) != 0 )
			result += ( result.empty() ? "" : "," ) + define;
		begin = end + 1;
	}
	return result;
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
	// Перестановки — от шейдеров из базы: вершинный — обычный, с матрицами экземпляров (у моделей уровня; у материала
	// расстановки, defines INST_*, свои экземпляры — тогда инстансный ключ ведёт к обычной фазе) и со сменой LOD
	// дизерингом (у расстановки доля перехода — в экземплярах её списков перехода, у моделей — в константах объекта);
	// каждый — с перестановками «только глубина» (DEPTH_ONLY: позиция и UV, Shaders/depth_only.sh). Экземпляры расстановки
	// — enablePlacedInstances; у материала расстановки (вершинный шейдер из базы с INST_POS) они — те же фазы
	const std::optional<ShaderSource> vertex = shaderSource( ShaderStageType::vertex );
	const std::optional<ShaderSource> pixel = shaderSource( ShaderStageType::pixel );
	if( !vertex || !pixel )
		return false;
	m_vertexSource = *vertex;
	m_pixelSource = *pixel;
	m_instancing = vertex->defines.find( "INST_" ) == std::string::npos;

	using namespace ShaderPermutation;
	for( uint32_t features : { 0u, instanced, lodDither } )
	{
		if( !addVertexVariant( features ) )
			return false;
	}
	if( vertex->defines.find( "INST_POS" ) != std::string::npos && !enablePlacedInstances() )
		return false;

	DMD3D::instance().createShaderConstantBuffer( sizeof( PBRMaterialBuffer ), m_psCB );

	return true;
}

bool PBRMaterial::addVertexVariant( uint32_t features )
{
	using namespace ShaderPermutation;
	const uint32_t dither = ( features & lodDither ) ? clipDither : 0;
	for( uint32_t alpha : { 0u, clipAlpha } )
	{
		if( !createPermutation( features | alpha ) || ( dither && !createPermutation( features | alpha | dither ) ) ||
			!createPermutation( features | depthOnly | alpha | dither ) )
			return false;
	}
	return true;
}

bool PBRMaterial::createPermutation( uint32_t key )
{
	using namespace ShaderPermutation;
	const int vertex = shader( ShaderStageType::vertex, m_vertexSource.function, m_vertexSource.file, vertexDefines( key ) );
	// «Только глубина» без отсечения — без пиксельного шейдера; с отсечением — точка входа mainDepth (только clip)
	const bool noPixel = ( key & depthOnly ) && !( key & ( clipAlpha | clipDither ) );
	const std::string function = ( key & impostorBake ) ? "mainBake" : ( key & depthOnly ) ? "mainDepth" : m_pixelSource.function;
	const int pixel = noPixel ? -1 : shader( ShaderStageType::pixel, function, m_pixelSource.file, pixelDefines( key ) );
	if( vertex < 0 || ( !noPixel && pixel < 0 ) )
		return false;
	setPermutation( key, createPhase( vertex, pixel ) );
	return permutation( key ) >= 0;
}

std::string PBRMaterial::vertexDefines( uint32_t key ) const
{
	using namespace ShaderPermutation;
	const std::string& base = m_vertexSource.defines;
	// Запекание: мировая матрица — константа объекта; у материала расстановки (PBRInstance: в базе INST_*) база берёт
	// экземпляр из пула, которого при запекании нет, — вариант без них
	if( key & impostorBake )
		return withoutInstances( base );
	std::string defines = base;
	// Экземпляр — положение, размер и поворот из пула расстановки (Shaders/instance.sh, ScatterPass::ScatterItem)
	if( ( key & placed ) && base.find( "INST_POS" ) == std::string::npos )
		defines = withDefine( defines, "INST_POS=1,INST_SCALE=1,INST_ROTATE=1" );
	if( ( key & instanced ) && m_instancing )
		defines = withDefine( defines, "INST_MATRIX=1" );
	if( key & lodDither )
		defines = withDefine( defines, "LOD_DITHER=1" );
	if( key & depthOnly )
		defines = withDefine( defines, "DEPTH_ONLY=1" );
	return defines;
}

std::string PBRMaterial::pixelDefines( uint32_t key ) const
{
	using namespace ShaderPermutation;
	std::string defines = m_pixelSource.defines;
	if( key & clipAlpha )
		defines = withDefine( defines, "ALPHA_MASK=1" );
	// Запекание рисует модель целиком: дизеринга смены LOD нет
	if( ( key & clipDither ) && !( key & impostorBake ) )
		defines = withDefine( defines, "LOD_DITHER=1" );
	return defines;
}

bool PBRMaterial::enablePlacedInstances()
{
	using namespace ShaderPermutation;
	if( !m_placed )
		m_placed = addVertexVariant( placed ) && addVertexVariant( placed | lodDither );
	return m_placed;
}

bool PBRMaterial::enableImpostorBake()
{
	using namespace ShaderPermutation;
	return permutation( impostorBake ) >= 0 || ( createPermutation( impostorBake ) && createPermutation( impostorBake | clipAlpha ) );
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

bool PBRMaterial::supportsInstancing() const
{
	return m_instancing;
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

	PBRMaterialBuffer param;
	param.baseColorFactor = materialValue( params, "BaseColorFactor", XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ) );
	param.emissiveFactor = materialValue( params, "EmissiveFactor", XMFLOAT3( 0.0f, 0.0f, 0.0f ) );
	param.metallic = materialValue( params, "Metallic", 0.0f );
	param.roughness = materialValue( params, "Roughness", 0.5f );
	param.normalScale = materialValue( params, "NormalScale", 1.0f );
	param.normalGreenUp = materialValue( params, "NormalGreenUp", false ) ? 1.0f : 0.0f;
	param.occlusionStrength = materialValue( params, "OcclusionStrength", 1.0f );
	param.alphaCutoff = materialValue( params, "AlphaCutoff", 0.5f );
	param.windWeight = materialValue( params, "WindWeight", 0.0f );
	param.specularFactor = materialValue( params, "SpecularFactor", 1.0f );
	param.diffuseTransmissionColorFactor = materialValue( params, "DiffuseTransmissionColorFactor", XMFLOAT3( 1.0f, 1.0f, 1.0f ) );
	param.diffuseTransmissionFactor = materialValue( params, "DiffuseTransmissionFactor", 0.0f );
	param.transmissionForward = materialValue( params, "TransmissionForward", defaultTransmissionForward );
	param.transmissionForwardRoughness = materialValue( params, "TransmissionForwardRoughness", defaultTransmissionForwardRoughness );
	param.windGlobalAngle = XMConvertToRadians( materialValue( params, "WindGlobalAngle", 1.5f ) );
	param.windGlobalExponent = materialValue( params, "WindGlobalExponent", 1.5f );
	param.windGlobalFrequency = materialValue( params, "WindGlobalFrequency", 0.25f );
	param.windBranchAngle = XMConvertToRadians( materialValue( params, "WindBranchAngle", 6.0f ) );
	param.windBranchFrequency = materialValue( params, "WindBranchFrequency", 0.8f );
	param.windTwigAngle = XMConvertToRadians( materialValue( params, "WindTwigAngle", 12.0f ) );
	param.windTwigFrequency = materialValue( params, "WindTwigFrequency", 2.0f );
	param.windRippleAmplitude = materialValue( params, "WindRippleAmplitude", 0.02f );
	param.windRippleFrequency = materialValue( params, "WindRippleFrequency", 6.0f );
	param.trampleWeight = materialValue( params, "TrampleWeight", 1.0f );
	param.trampleBendLength = materialValue( params, "TrampleBendLength", 0.25f );

	Device::updateResourceData<PBRMaterialBuffer>( m_psCB, param );
	// b2 читают и пиксельный, и вершинный шейдер (отклик на ветер)
	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_psCB );
}

}
