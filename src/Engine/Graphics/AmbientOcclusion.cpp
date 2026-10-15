#include "AmbientOcclusion.h"
#include <algorithm>
#include "ConstantBuffers.h"
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "Shaders\slots.h"

using namespace DirectX;

namespace GS
{

bool AmbientOcclusion::initialize()
{
	if( !m_shader.initialize( "Shaders\\gtao.cs", "main" ) || !m_denoiseShader.initialize( "Shaders\\gtao.cs", "mainDenoise" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( AmbientOcclusionBuffer ), m_constants ) )
	{
		LOG( "Ambient occlusion: shaders or constants are not created" );
		return false;
	}
	return resize();
}

bool AmbientOcclusion::resize()
{
	DMD3D& d3d = DMD3D::instance();
	m_width = std::max( ( d3d.backBufferWidth() + 1 ) / 2, 1u );
	m_height = std::max( ( d3d.backBufferHeight() + 1 ) / 2, 1u );
	auto create = [&]( Texture& texture, ShaderView& srv, StorageView& uav, const char* name )
	{
		TextureDesc desc;
		desc.width = m_width;
		desc.height = m_height;
		desc.format = DXGI_FORMAT_R16G16_FLOAT;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createShaderView( texture, {}, srv ) ||
			!d3d.createStorageView( texture, {}, uav ) )
			return false;
		d3d.setName( texture, name );
		return true;
	};
	if( !create( m_noisy, m_noisySRV, m_noisyUAV, "Ambient occlusion (noisy)" ) ||
		!create( m_result, m_resultSRV, m_resultUAV, "Ambient occlusion" ) )
	{
		LOG( "Ambient occlusion: textures are not created" );
		return false;
	}
	return true;
}

void AmbientOcclusion::render( const RenderView& view, const ShaderView& sceneDepth )
{
	const Settings& settings = m_settings;
	DMD3D& d3d = DMD3D::instance();
	XMFLOAT4X4 projection;
	XMStoreFloat4x4( &projection, view.projection );

	AmbientOcclusionBuffer params = {};
	params.tanHalfFov = XMFLOAT2( 1.0f / projection._11, 1.0f / projection._22 );
	params.depthLinearize = XMFLOAT2( projection._33, projection._43 );
	params.fullSize = XMUINT2( d3d.backBufferWidth(), d3d.backBufferHeight() );
	params.halfSize = XMUINT2( m_width, m_height );
	params.radius = std::max( settings.radius, 0.01f );
	// Спад к краю радиуса — как в XeGTAO: последние 61,5 % радиуса вес точки падает до нуля
	const float falloffRange = 0.615f * params.radius;
	const float falloffFrom = params.radius - falloffRange;
	params.falloffMul = -1.0f / falloffRange;
	params.falloffAdd = falloffFrom / falloffRange + 1.0f;
	params.pixelsPerMeter = 0.5f * static_cast<float>( params.fullSize.y ) * projection._22;
	params.finalPower = std::max( settings.power, 0.1f );
	params.maxPixelRadius = 0.125f * static_cast<float>( params.fullSize.y );
	params.depthTolerance = 0.03f;
	Device::updateResourceData<AmbientOcclusionBuffer>( m_constants, params );

	const uint32_t groupsX = ( m_width + 7 ) / 8;
	const uint32_t groupsY = ( m_height + 7 ) / 8;
	d3d.beginPass( PassDesc{ "Ambient occlusion", {}, {}, 0, 0, { { &sceneDepth, "scene depth" } }, { { &m_noisyUAV, "ambient occlusion (noisy)" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 0, sceneDepth );
	m_shader.setUAVBuffer( 0, m_noisyUAV );
	m_shader.dispatchGroups( groupsX, groupsY, 1 );

	d3d.beginPass( PassDesc{ "Ambient occlusion denoise", {}, {}, 0, 0, { { &m_noisySRV, "ambient occlusion (noisy)" } },
							 { { &m_resultUAV, "ambient occlusion" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 1, m_noisySRV );
	m_denoiseShader.setUAVBuffer( 0, m_resultUAV );
	m_denoiseShader.dispatchGroups( groupsX, groupsY, 1 );
	// Материалам (Shaders/ambient_occlusion.sh): привязка завершает проход над результатом
	d3d.setSRV( SLOT_AMBIENT_OCCLUSION, m_resultSRV );
}

void AmbientOcclusion::setSettings( const Settings& settings, bool show )
{
	m_settings = settings;
	m_show = show;
}

void AmbientOcclusion::beginFrame( const RenderFrame& frame, FrameParameters& constants )
{
	m_active = frame.depthPrepass && m_settings.intensity > 0.0f;
	constants.ambientOcclusion = m_active ? XMFLOAT4( std::clamp( m_settings.intensity, 0.0f, 1.0f ), static_cast<float>( m_width ),
													  static_cast<float>( m_height ), m_show ? 1.0f : 0.0f ) :
											XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
}

}
