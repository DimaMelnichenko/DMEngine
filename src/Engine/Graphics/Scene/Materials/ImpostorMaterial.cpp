#include "ImpostorMaterial.h"
#include <cmath>
#include "Shaders\slots.h"
#include "ConstantBuffers.h"
#include "SceneObject.h"
#include "Scene\VertexPool.h"
#include "System.h"
#include "D3D\TextureImages.h"
#include "Texture\ImageMips.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

ImpostorMaterial::ImpostorMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

std::vector<VertexElement> ImpostorMaterial::initLayouts()
{
	// Карточка (MeshStorage::cardId) в основном потоке VertexPool: нужны угол и UV
	return {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, VertexElement::appendOffset },
	};
}

bool ImpostorMaterial::initialize()
{
	// Вершинные шейдеры: экземпляр расстановки, 1 — со сменой LOD дизерингом. Пиксельные: 0 — с отсечением по покрытию,
	// 1 — без (после depth prepass), 2 — с отсечением и дизерингом, 3 — глубина, 4 — глубина с дизерингом
	const std::string placed = "INST_POS=1,INST_SCALE=1,INST_ROTATE=1";
	const std::string placedDither = placed + ",LOD_DITHER=1";
	if( !addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\impostor.vs", placed ) ||
		!addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\impostor.vs", placedDither ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps", "ALPHA_MASK=1" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps", "ALPHA_MASK=1,LOD_DITHER=1" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", "Shaders\\impostor.ps" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", "Shaders\\impostor.ps", "LOD_DITHER=1" ) )
		return false;

	for( int dither = 0; dither < 2; ++dither )
	{
		m_colorPhases[dither][1][0] = createPhase( dither, 0 );
		m_colorPhases[dither][0][0] = createPhase( dither, 1 );
		m_colorPhases[dither][1][1] = dither ? createPhase( dither, 2 ) : m_colorPhases[dither][1][0];
		m_colorPhases[dither][0][1] = m_colorPhases[dither][0][0];
		m_depthPhases[dither] = createPhase( dither, dither ? 4 : 3 );
	}
	for( const auto& variant : m_colorPhases )
		for( const auto& masked : variant )
			for( int phase : masked )
				if( phase < 0 )
					return false;
	return m_depthPhases[0] >= 0 && m_depthPhases[1] >= 0;
}

MaterialRenderState ImpostorMaterial::renderState( const PropertyContainer& ) const
{
	MaterialRenderState state;
	state.blendMode = BlendMode::masked;
	state.twoSided = true;
	state.ditheredLodTransition = true;
	return state;
}

int ImpostorMaterial::phaseFor( const PropertyContainer&, const ShaderPhaseOptions& options ) const
{
	// После depth prepass отсечения нет: покрытие и дизеринг уже в глубине, проверка EQUAL
	const bool clipAlpha = !options.depthFromPrepass;
	const bool clipDither = options.lodDither && !options.depthFromPrepass;
	return m_colorPhases[options.lodDither ? 1 : 0][clipAlpha ? 1 : 0][clipDither ? 1 : 0];
}

int ImpostorMaterial::depthPhaseFor( const PropertyContainer&, const ShaderPhaseOptions& options ) const
{
	return m_depthPhases[options.lodDither ? 1 : 0];
}

std::vector<int> ImpostorMaterial::depthPhases() const
{
	return { m_depthPhases[0], m_depthPhases[1] };
}

void ImpostorMaterial::setParams( const PropertyContainer& )
{
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( 0, System::textures().get( m_colorTexture )->srv() );
	d3d.setSRV( 1, System::textures().get( m_normalTexture )->srv() );
	d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );
}

RenderView ImpostorMaterial::frameView( uint32_t x, uint32_t y ) const
{
	// Направление кадра — точка сетки на полуоктаэдре (hemiOctDecode в Shaders/impostor.sh)
	const float px = static_cast<float>( x ) / ( frames - 1 ) * 2.0f - 1.0f;
	const float py = static_cast<float>( y ) / ( frames - 1 ) * 2.0f - 1.0f;
	const float dx = ( px + py ) * 0.5f;
	const float dz = ( px - py ) * 0.5f;
	const XMVECTOR direction = XMVector3Normalize( XMVectorSet( dx, 1.0f - std::fabs( dx ) - std::fabs( dz ), dz, 0.0f ) );
	const float radius = m_params.bounds.w;
	const XMVECTOR center = XMVectorSet( m_params.bounds.x, m_params.bounds.y, m_params.bounds.z, 1.0f );
	// Базис — как impostorFrameBasis: у зенита «верх» кадра — +Z
	const XMVECTOR up = std::fabs( XMVectorGetY( direction ) ) > 0.999f ? XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f ) : XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	const XMVECTOR eye = XMVectorAdd( center, XMVectorScale( direction, 2.0f * radius ) );

	RenderView view;
	view.view = XMMatrixLookAtLH( eye, center, up );
	// Обратная глубина, как у камеры: 1 у ближней плоскости (у камеры), 0 у дальней — за сферой
	view.projection = XMMatrixOrthographicLH( 2.0f * radius, 2.0f * radius, 4.0f * radius, 0.0f );
	view.viewProjection = XMMatrixMultiply( view.view, view.projection );
	view.viewInverse = XMMatrixInverse( nullptr, view.view );
	XMStoreFloat3( &view.position, eye );
	XMStoreFloat3( &view.direction, XMVectorNegate( direction ) );
	view.lodOrigin = view.position;
	view.nearPlane = 0.0f;
	view.farPlane = 4.0f * radius;
	view.frustum = DMFrustum( view.viewProjection );
	return view;
}

bool ImpostorMaterial::bake( DMModel& model, const BakeContext& context )
{
	DMModel::LodBlock* lod0 = model.getLodById( 0 );
	if( !lod0 || lod0->sections.empty() )
	{
		LOG( name() + ": model has no LOD0" );
		return false;
	}
	const BoundingBox& box = lod0->bounds;
	// Сфера с запасом: кадр не обрезает кончики ветвей
	const float radius = XMVectorGetX( XMVector3Length( XMLoadFloat3( &box.Extents ) ) ) * 1.02f;
	m_params.bounds = XMFLOAT4( box.Center.x, box.Center.y, box.Center.z, radius );
	m_params.transmission = XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );
	m_params.frames = frames;
	m_params.alphaCutoff = 0.5f;
	m_params.roughness = 0.8f;

	// Секции LOD0 и их материалы с вариантом запекания; цвет пропускания — у секции, которая пропускает свет (хвоя)
	std::vector<std::pair<DMModel::Section*, Material*>> sections;
	for( const auto& section : lod0->sections )
	{
		Material* material = System::materials().get( section->material ).get();
		if( !material || !material->enableImpostorBake() )
		{
			LOG( name() + ": material of model " + model.properties()->name() + " can't bake impostors" );
			return false;
		}
		sections.push_back( { section.get(), material } );
		const PropertyContainer& params = section->params;
		if( params.exists( "DiffuseTransmissionFactor" ) && params["DiffuseTransmissionFactor"].data<float>() > 0.0f &&
			params.exists( "DiffuseTransmissionColorFactor" ) )
		{
			const XMFLOAT3 tint = params["DiffuseTransmissionColorFactor"].data<XMFLOAT3>();
			m_params.transmission = XMFLOAT4( tint.x, tint.y, tint.z, 1.0f );
		}
	}

	// Цели запекания: кадр — срез массива (цвет sRGB и нормаль), глубина общая
	DMD3D& d3d = DMD3D::instance();
	const uint32_t count = frames * frames;
	TextureDesc colorDesc;
	colorDesc.width = colorDesc.height = frameSize;
	colorDesc.arraySize = count;
	colorDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	colorDesc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;
	colorDesc.hasClearColor = true;
	TextureDesc normalDesc = colorDesc;
	normalDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	const float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 0.0f };
	std::copy( std::begin( clearNormal ), std::end( clearNormal ), normalDesc.clearColor );
	TextureDesc depthDesc;
	depthDesc.width = depthDesc.height = frameSize;
	depthDesc.format = DXGI_FORMAT_D32_FLOAT;
	depthDesc.usage = TextureUsage::depthStencil;
	Texture color, normal, depth;
	TargetView depthTarget;
	if( !d3d.createTexture( colorDesc, nullptr, color ) || !d3d.createTexture( normalDesc, nullptr, normal ) ||
		!d3d.createTexture( depthDesc, nullptr, depth ) || !d3d.createTargetView( depth, {}, depthTarget ) )
	{
		LOG( name() + ": can't create bake targets" );
		return false;
	}
	const float clearColor[4] = {};
	for( uint32_t y = 0; y < frames; ++y )
	for( uint32_t x = 0; x < frames; ++x )
	{
		// Виды целей — на кадр: дескриптор RTV читается при записи команды, а куча RTV мала для всех срезов сразу
		const uint32_t slice = y * frames + x;
		TextureViewDesc target;
		target.kind = TextureViewDesc::Kind::texture2DArray;
		target.firstSlice = slice;
		target.sliceCount = 1;
		TargetView colorTarget;
		TargetView normalTarget;
		if( !d3d.createTargetView( color, target, colorTarget ) || !d3d.createTargetView( normal, target, normalTarget ) )
		{
			LOG( name() + ": can't create frame target views" );
			return false;
		}
		PassDesc pass;
		pass.name = "Impostor bake";
		pass.colors = { { &colorTarget, "impostor color" }, { &normalTarget, "impostor normal" } };
		pass.depth = { &depthTarget, "impostor depth" };
		pass.width = pass.height = frameSize;
		d3d.beginPass( pass );
		d3d.clearTarget( colorTarget, clearColor );
		d3d.clearTarget( normalTarget, clearNormal );
		d3d.clearDepth( depthTarget, 0.0f );
		context.constants.setViewBuffer( frameView( x, y ) );
		context.constants.setPerObjectBuffer( XMMatrixIdentity() );
		context.vertexPool.setBuffers();
		for( const auto& [section, material] : sections )
		{
			const MaterialRenderState state = material->renderState( section->params );
			ScopedRenderState scoped( materialRasterState( state.twoSided, false, RasterState::solid ), DepthState::enabled, BlendState::opaque );
			ShaderPhaseOptions options;
			options.impostorBake = true;
			if( !material->setPass( material->phaseFor( section->params, options ) ) )
				continue;
			material->setParams( section->params );
			const AbstractMesh* mesh = System::meshes().get( section->mesh ).get();
			d3d.drawIndexed( mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );
		}
	}

	// Кадры — на CPU (ждёт GPU): цвет в прозрачные тексели от соседних (без тёмной каймы на мипах), мипы с сохранением
	// покрытия при пороге отсечения, обратно на GPU текстурами хранилища
	ScratchImage colorImage;
	ScratchImage normalImage;
	if( !GpuImages::captureTexture( color, colorImage ) || !GpuImages::captureTexture( normal, normalImage ) )
	{
		LOG( name() + ": can't read frames back" );
		return false;
	}
	ImageMips::dilateTransparent( normalImage, 8, &colorImage );
	ImageMips::dilateTransparent( colorImage, 8 );
	if( !ImageMips::generate( colorImage, m_params.alphaCutoff ) || !ImageMips::generate( normalImage ) )
	{
		LOG( name() + ": can't build mips" );
		return false;
	}

	DMTextureStorage& textures = System::textures();
	auto colorTexture = std::make_unique<DMTexture>( textures.freeId(), name() + " color" );
	if( !colorTexture->create( colorImage ) )
	{
		LOG( name() + ": can't create textures" );
		return false;
	}
	m_colorTexture = colorTexture->id();
	textures.insertResource( std::move( colorTexture ) );
	auto normalTexture = std::make_unique<DMTexture>( textures.freeId(), name() + " normal" );
	if( !normalTexture->create( normalImage ) )
		return false;
	m_normalTexture = normalTexture->id();
	textures.insertResource( std::move( normalTexture ) );

	BufferDesc constants;
	constants.size = sizeof( Params );
	constants.usage = BufferUsage::constant;
	return d3d.createBuffer( constants, &m_params, m_constantBuffer );
}

}
