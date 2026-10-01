#include "ImpostorMaterial.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "Shaders\slots.h"
#include "ConstantBuffers.h"
#include "SceneObject.h"
#include "Scene\VertexPool.h"
#include "System.h"
#include "D3D\TextureImages.h"
#include "Texture\ImageMips.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace
{

float srgbToLinear( float value )
{
	return value <= 0.04045f ? value / 12.92f : std::pow( ( value + 0.055f ) / 1.055f, 2.4f );
}

uint8_t linearToSrgb( float value )
{
	value = std::min( std::max( value, 0.0f ), 1.0f );
	const float encoded = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow( value, 1.0f / 2.4f ) - 0.055f;
	return static_cast<uint8_t>( encoded * 255.0f + 0.5f );
}

// Направление кадра сетки и его базис — как impostorFrameDirection / impostorFrameBasis в Shaders/impostor.sh
struct FrameBasis
{
	XMVECTOR direction;
	XMVECTOR right;
	XMVECTOR up;
};

FrameBasis frameBasis( uint32_t x, uint32_t y, uint32_t frames )
{
	const float px = static_cast<float>( x ) / ( frames - 1 ) * 2.0f - 1.0f;
	const float py = static_cast<float>( y ) / ( frames - 1 ) * 2.0f - 1.0f;
	const float dx = ( px + py ) * 0.5f;
	const float dz = ( px - py ) * 0.5f;
	FrameBasis basis;
	basis.direction = XMVector3Normalize( XMVectorSet( dx, 1.0f - std::fabs( dx ) - std::fabs( dz ), dz, 0.0f ) );
	const XMVECTOR forward = XMVectorNegate( basis.direction );
	const XMVECTOR worldUp = std::fabs( XMVectorGetY( basis.direction ) ) > 0.999f ? XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f ) :
																					XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	basis.right = XMVector3Normalize( XMVector3Cross( worldUp, forward ) );
	basis.up = XMVector3Cross( forward, basis.right );
	return basis;
}

// Кадры со сверхвыборкой (сторона size × samples) → кадры size: альфа цвета — доля покрытых выборок × density, цвет (в
// линейном), нормаль с пропусканием и глубина — средние по покрытым. Непокрытый тексель — цвет 0, плоская нормаль и
// плоскость через центр (их заполнит растекание). Глубина — в R16G16: R — глубина, G — место под затенение
bool downsampleFrames( const ScratchImage& color, const ScratchImage& normal, const ScratchImage& offset, uint32_t size, uint32_t samples,
					   float density, ScratchImage& outColor, ScratchImage& outNormal, ScratchImage& outOffset )
{
	const size_t count = color.GetMetadata().arraySize;
	if( FAILED( outColor.Initialize2D( color.GetMetadata().format, size, size, count, 1 ) ) ||
		FAILED( outNormal.Initialize2D( normal.GetMetadata().format, size, size, count, 1 ) ) ||
		FAILED( outOffset.Initialize2D( DXGI_FORMAT_R16G16_UNORM, size, size, count, 1 ) ) )
		return false;
	float toLinear[256];
	for( int i = 0; i < 256; ++i )
		toLinear[i] = srgbToLinear( i / 255.0f );
	const float sampleCount = static_cast<float>( samples * samples );
	for( size_t item = 0; item < count; ++item )
	{
		const Image& srcColor = *color.GetImage( 0, item, 0 );
		const Image& srcNormal = *normal.GetImage( 0, item, 0 );
		const Image& srcOffset = *offset.GetImage( 0, item, 0 );
		const Image& dstColor = *outColor.GetImage( 0, item, 0 );
		const Image& dstNormal = *outNormal.GetImage( 0, item, 0 );
		const Image& dstOffset = *outOffset.GetImage( 0, item, 0 );
		for( uint32_t y = 0; y < size; ++y )
		for( uint32_t x = 0; x < size; ++x )
		{
			float rgb[3] = {};
			float nrm[4] = {};
			float depth = 0.0f;
			uint32_t covered = 0;
			for( uint32_t sy = 0; sy < samples; ++sy )
			for( uint32_t sx = 0; sx < samples; ++sx )
			{
				const size_t px = x * samples + sx;
				const size_t py = y * samples + sy;
				const uint8_t* c = srcColor.pixels + py * srcColor.rowPitch + px * 4;
				if( c[3] < 128 )
					continue;
				const uint8_t* n = srcNormal.pixels + py * srcNormal.rowPitch + px * 4;
				const uint16_t* o = reinterpret_cast<const uint16_t*>( srcOffset.pixels + py * srcOffset.rowPitch ) + px;
				for( int i = 0; i < 3; ++i )
					rgb[i] += toLinear[c[i]];
				for( int i = 0; i < 4; ++i )
					nrm[i] += n[i];
				depth += *o;
				++covered;
			}
			uint8_t* c = dstColor.pixels + y * dstColor.rowPitch + x * 4;
			uint8_t* n = dstNormal.pixels + y * dstNormal.rowPitch + x * 4;
			uint16_t* o = reinterpret_cast<uint16_t*>( dstOffset.pixels + y * dstOffset.rowPitch ) + x * 2;
			if( covered == 0 )
			{
				c[0] = c[1] = c[2] = c[3] = 0;
				n[0] = n[1] = 128;
				n[2] = 255;
				n[3] = 0;
				o[0] = 32768;
				o[1] = 65535;
				continue;
			}
			const float inv = 1.0f / covered;
			for( int i = 0; i < 3; ++i )
				c[i] = linearToSrgb( rgb[i] * inv );
			c[3] = static_cast<uint8_t>( std::min( covered / sampleCount * density, 1.0f ) * 255.0f + 0.5f );
			for( int i = 0; i < 4; ++i )
				n[i] = static_cast<uint8_t>( nrm[i] * inv + 0.5f );
			o[0] = static_cast<uint16_t>( depth * inv + 0.5f );
			o[1] = 65535;
		}
	}
	return true;
}

// Затенение окружающего света (ambient occlusion) по запечённым кадрам: точка поверхности кадра — из его глубины; она
// видна из направления другого кадра, если там поверх неё (ближе к его камере) нет покрытой поверхности. Доля таких
// направлений верхней полусферы — в G глубины. Внутренние слои хвои закрыты наружными почти со всех сторон и темнеют
void bakeOcclusion( const ScratchImage& color, ScratchImage& offset, uint32_t frames, uint32_t size )
{
	const uint32_t count = frames * frames;
	std::vector<FrameBasis> bases;
	for( uint32_t y = 0; y < frames; ++y )
		for( uint32_t x = 0; x < frames; ++x )
			bases.push_back( frameBasis( x, y, frames ) );
	// В координатах кадра в долях радиуса: u, v — −1…1 по сторонам, d — смещение к камере (глубина × 2 − 1)
	const auto depthAt = [&]( uint32_t frame, int tx, int ty ) -> float
	{
		const Image& image = *offset.GetImage( 0, frame, 0 );
		return reinterpret_cast<const uint16_t*>( image.pixels + ty * image.rowPitch )[tx * 2] / 65535.0f * 2.0f - 1.0f;
	};
	const auto coveredAt = [&]( uint32_t frame, int tx, int ty )
	{
		const Image& image = *color.GetImage( 0, frame, 0 );
		return image.pixels[ty * image.rowPitch + tx * 4 + 3] > 0;
	};
	// Запас — два текселя: поверхность не закрывает саму себя
	const float bias = 4.0f / size;
	std::vector<uint16_t> result( static_cast<size_t>( count ) * size * size, 65535 );
	for( uint32_t f = 0; f < count; ++f )
	for( uint32_t ty = 0; ty < size; ++ty )
	for( uint32_t tx = 0; tx < size; ++tx )
	{
		if( !coveredAt( f, tx, ty ) )
			continue;
		const float u = ( tx + 0.5f ) / size * 2.0f - 1.0f;
		const float v = 1.0f - ( ty + 0.5f ) / size * 2.0f;
		const XMVECTOR point = XMVectorAdd( XMVectorAdd( XMVectorScale( bases[f].right, u ), XMVectorScale( bases[f].up, v ) ),
											XMVectorScale( bases[f].direction, depthAt( f, tx, ty ) ) );
		uint32_t visible = 0;
		for( uint32_t g = 0; g < count; ++g )
		{
			const float ug = XMVectorGetX( XMVector3Dot( point, bases[g].right ) );
			const float vg = XMVectorGetX( XMVector3Dot( point, bases[g].up ) );
			const float dg = XMVectorGetX( XMVector3Dot( point, bases[g].direction ) );
			const int gx = std::min( std::max( static_cast<int>( ( ug * 0.5f + 0.5f ) * size ), 0 ), static_cast<int>( size ) - 1 );
			const int gy = std::min( std::max( static_cast<int>( ( 0.5f - vg * 0.5f ) * size ), 0 ), static_cast<int>( size ) - 1 );
			if( !coveredAt( g, gx, gy ) || depthAt( g, gx, gy ) <= dg + bias )
				++visible;
		}
		result[( static_cast<size_t>( f ) * size + ty ) * size + tx] = static_cast<uint16_t>( 65535.0f * visible / count + 0.5f );
	}
	for( uint32_t f = 0; f < count; ++f )
	{
		const Image& image = *offset.GetImage( 0, f, 0 );
		for( uint32_t ty = 0; ty < size; ++ty )
			for( uint32_t tx = 0; tx < size; ++tx )
				reinterpret_cast<uint16_t*>( image.pixels + ty * image.rowPitch )[tx * 2 + 1] = result[( static_cast<size_t>( f ) * size + ty ) * size + tx];
	}
}

}

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
	// Вершинные шейдеры: экземпляр расстановки, 1 — со сменой LOD дизерингом. Пиксельные: 0 — с отсечением по покрытию и
	// глубиной поверхности (без depth prepass), 1 — после prepass: видимость по глубине сцены (проход opaqueDepthRead),
	// 2 — как 0 с дизерингом, 3 — глубина (prepass и тени), 4 — глубина с дизерингом, 5 — как 1 с дизерингом
	const std::string placed = "INST_POS=1,INST_SCALE=1,INST_ROTATE=1";
	const std::string placedDither = placed + ",LOD_DITHER=1";
	if( !addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\impostor.vs", placed ) ||
		!addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\impostor.vs", placedDither ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps", "ALPHA_MASK=1" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps", "ALPHA_MASK=1,LOD_DITHER=1" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", "Shaders\\impostor.ps" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "mainDepth", "Shaders\\impostor.ps", "LOD_DITHER=1" ) ||
		!addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\impostor.ps", "LOD_DITHER=1" ) )
		return false;

	for( int dither = 0; dither < 2; ++dither )
	{
		m_colorPhases[dither][1][0] = createPhase( dither, 0 );
		m_colorPhases[dither][0][0] = createPhase( dither, 1 );
		m_colorPhases[dither][1][1] = dither ? createPhase( dither, 2 ) : m_colorPhases[dither][1][0];
		m_colorPhases[dither][0][1] = dither ? createPhase( dither, 5 ) : m_colorPhases[dither][0][0];
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
	// После depth prepass — вариант с видимостью по глубине сцены (проход opaqueDepthRead), покрытие и дизеринг в нём
	// отсекаются, как в prepass
	const bool clipAlpha = !options.depthFromPrepass;
	const bool clipDither = options.lodDither;
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
	d3d.setSRV( 2, System::textures().get( m_offsetTexture )->srv() );
	d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );
}

RenderView ImpostorMaterial::frameView( uint32_t x, uint32_t y ) const
{
	// Направление кадра — точка сетки на полуоктаэдре, базис — как impostorFrameBasis в Shaders/impostor.sh
	const FrameBasis basis = frameBasis( x, y, frames );
	const XMVECTOR direction = basis.direction;
	const float radius = m_params.bounds.w;
	const XMVECTOR center = XMVectorSet( m_params.bounds.x, m_params.bounds.y, m_params.bounds.z, 1.0f );
	const XMVECTOR eye = XMVectorAdd( center, XMVectorScale( direction, 2.0f * radius ) );

	RenderView view;
	view.view = XMMatrixLookAtLH( eye, center, basis.up );
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

bool ImpostorMaterial::bake( DMModel& model, const BakeContext& context, const BakeSettings& settings )
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
	m_params.extents = XMFLOAT4( box.Extents.x * 1.02f, box.Extents.y * 1.02f, box.Extents.z * 1.02f, 0.0f );
	m_params.transmission = XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );
	m_params.frames = frames;
	m_params.alphaCutoff = 0.5f;
	m_params.roughness = 0.8f;
	m_params.occlusion = std::min( std::max( settings.occlusion, 0.0f ), 1.0f );

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

	// Цели запекания: кадр — срез массива (цвет sRGB, нормаль и глубина поверхности — смещение глубины), буфер глубины
	// общий; сторона — со сверхвыборкой
	DMD3D& d3d = DMD3D::instance();
	const uint32_t count = frames * frames;
	const uint32_t bakeSize = frameSize * bakeSamples;
	TextureDesc colorDesc;
	colorDesc.width = colorDesc.height = bakeSize;
	colorDesc.arraySize = count;
	colorDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	colorDesc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;
	colorDesc.hasClearColor = true;
	TextureDesc normalDesc = colorDesc;
	normalDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	const float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 0.0f };
	std::copy( std::begin( clearNormal ), std::end( clearNormal ), normalDesc.clearColor );
	TextureDesc offsetDesc = colorDesc;
	offsetDesc.format = DXGI_FORMAT_R16_UNORM;
	const float clearOffset[4] = { 0.5f, 0.0f, 0.0f, 0.0f };	// плоскость через центр
	std::copy( std::begin( clearOffset ), std::end( clearOffset ), offsetDesc.clearColor );
	TextureDesc depthDesc;
	depthDesc.width = depthDesc.height = bakeSize;
	depthDesc.format = DXGI_FORMAT_D32_FLOAT;
	depthDesc.usage = TextureUsage::depthStencil;
	Texture color, normal, offset, depth;
	TargetView depthTarget;
	if( !d3d.createTexture( colorDesc, nullptr, color ) || !d3d.createTexture( normalDesc, nullptr, normal ) ||
		!d3d.createTexture( offsetDesc, nullptr, offset ) || !d3d.createTexture( depthDesc, nullptr, depth ) ||
		!d3d.createTargetView( depth, {}, depthTarget ) )
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
		TargetView offsetTarget;
		if( !d3d.createTargetView( color, target, colorTarget ) || !d3d.createTargetView( normal, target, normalTarget ) ||
			!d3d.createTargetView( offset, target, offsetTarget ) )
		{
			LOG( name() + ": can't create frame target views" );
			return false;
		}
		PassDesc pass;
		pass.name = "Impostor bake";
		pass.colors = { { &colorTarget, "impostor color" }, { &normalTarget, "impostor normal" }, { &offsetTarget, "impostor offset" } };
		pass.depth = { &depthTarget, "impostor depth" };
		pass.width = pass.height = bakeSize;
		d3d.beginPass( pass );
		d3d.clearTarget( colorTarget, clearColor );
		d3d.clearTarget( normalTarget, clearNormal );
		d3d.clearTarget( offsetTarget, clearOffset );
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

	// Кадры — на CPU (ждёт GPU): сверхвыборка — в кадр frameSize с долей покрытия (× плотность), затенение окружающего
	// света по глубине кадров, цвет в прозрачные тексели от соседних (без тёмной каймы на мипах), мипы с сохранением
	// покрытия при пороге отсечения, обратно на GPU текстурами хранилища
	ScratchImage bakedColor;
	ScratchImage bakedNormal;
	ScratchImage bakedOffset;
	if( !GpuImages::captureTexture( color, bakedColor ) || !GpuImages::captureTexture( normal, bakedNormal ) ||
		!GpuImages::captureTexture( offset, bakedOffset ) )
	{
		LOG( name() + ": can't read frames back" );
		return false;
	}
	ScratchImage colorImage;
	ScratchImage normalImage;
	ScratchImage offsetImage;
	if( !downsampleFrames( bakedColor, bakedNormal, bakedOffset, frameSize, bakeSamples, std::max( settings.density, 0.0f ),
						   colorImage, normalImage, offsetImage ) )
	{
		LOG( name() + ": can't downsample frames" );
		return false;
	}
	if( m_params.occlusion > 0.0f )
		bakeOcclusion( colorImage, offsetImage, frames, frameSize );
	ImageMips::dilateTransparent( normalImage, 8, &colorImage );
	ImageMips::dilateTransparent( offsetImage, 8, &colorImage );
	ImageMips::dilateTransparent( colorImage, 8 );
	if( !ImageMips::generate( colorImage, m_params.alphaCutoff ) || !ImageMips::generate( normalImage ) ||
		!ImageMips::generate( offsetImage ) )
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
	auto offsetTexture = std::make_unique<DMTexture>( textures.freeId(), name() + " depth" );
	if( !offsetTexture->create( offsetImage ) )
		return false;
	m_offsetTexture = offsetTexture->id();
	textures.insertResource( std::move( offsetTexture ) );

	BufferDesc constants;
	constants.size = sizeof( Params );
	constants.usage = BufferUsage::constant;
	return d3d.createBuffer( constants, &m_params, m_constantBuffer );
}

}
