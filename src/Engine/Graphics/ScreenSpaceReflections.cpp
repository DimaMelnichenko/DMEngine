#include "ScreenSpaceReflections.h"
#include <algorithm>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "Shaders\slots.h"
#include "Shaders\screen_reflection_constants.h"

namespace GS
{

namespace
{

// Уровней пирамиды глубины: ячейка верхнего — 128 пикселей (луч через кадр 1920 — полтора десятка ячеек). Уровень 0 — сама
// глубина сцены, в текстуре — уровни 1…7 (мип L − 1); строят её два прохода: уровни 1…4 (группа — блок 16 × 16 пикселей)
// и 5…7 (screen_reflections.cs, mainHiZFirst и mainHiZRest)
constexpr uint32_t hizLevels = SCREEN_REFLECTION_LEVELS;
constexpr uint32_t hizAlignment = 1u << ( hizLevels - 1 );
constexpr uint32_t hizMips = hizLevels - 1;
constexpr uint32_t hizFirstMips = 4;

uint32_t groups( uint32_t size )
{
	return ( size + 7 ) / 8;
}

}

TargetFormats ScreenSpaceReflections::surfaceFormats()
{
	return TargetFormats::colorTarget( normalFormat, depthFormat );
}

bool ScreenSpaceReflections::initialize()
{
	if( !m_hizFirstShader.initialize( "Shaders\\screen_reflections.cs", "mainHiZFirst" ) ||
		!m_hizRestShader.initialize( "Shaders\\screen_reflections.cs", "mainHiZRest" ) ||
		!m_traceShader.initialize( "Shaders\\screen_reflections.cs", "main" ) )
	{
		LOG( "Screen reflections: shaders are not created" );
		return false;
	}
	return resize();
}

bool ScreenSpaceReflections::resize()
{
	DMD3D& d3d = DMD3D::instance();
	m_width = std::max( d3d.backBufferWidth(), 1u );
	m_height = std::max( d3d.backBufferHeight(), 1u );
	m_hizWidth = ( m_width + hizAlignment - 1 ) / hizAlignment * hizAlignment;
	m_hizHeight = ( m_height + hizAlignment - 1 ) / hizAlignment * hizAlignment;
	// Прежние ресурсы отпускаются отложенно, когда GPU закончит кадр, в котором они ещё читались
	m_hizLevelViews.clear();
	m_hizLevelUAVs.clear();

	TextureDesc depthDesc;
	depthDesc.width = m_width;
	depthDesc.height = m_height;
	depthDesc.format = DXGI_FORMAT_R32_TYPELESS;
	depthDesc.usage = TextureUsage::depthStencil | TextureUsage::shaderResource;
	TextureViewDesc depthViewDesc;
	depthViewDesc.format = DXGI_FORMAT_R32_FLOAT;
	TextureDesc normalDesc;
	normalDesc.width = m_width;
	normalDesc.height = m_height;
	normalDesc.format = normalFormat;
	normalDesc.usage = TextureUsage::renderTarget | TextureUsage::shaderResource;
	normalDesc.hasClearColor = true;
	TextureDesc hizDesc;
	hizDesc.width = m_hizWidth / 2;
	hizDesc.height = m_hizHeight / 2;
	hizDesc.mipCount = hizMips;
	hizDesc.format = DXGI_FORMAT_R32G32_FLOAT;
	hizDesc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	TextureDesc resultDesc;
	resultDesc.width = m_width;
	resultDesc.height = m_height;
	resultDesc.format = DXGI_FORMAT_R16G16B16A16_UNORM;
	resultDesc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	if( !d3d.createTexture( depthDesc, nullptr, m_surfaceDepth ) || !d3d.createTargetView( m_surfaceDepth, {}, m_surfaceDepthTarget ) ||
		!d3d.createShaderView( m_surfaceDepth, depthViewDesc, m_surfaceDepthView ) ||
		!d3d.createTexture( normalDesc, nullptr, m_surfaceNormal ) || !d3d.createTargetView( m_surfaceNormal, {}, m_surfaceNormalTarget ) ||
		!d3d.createShaderView( m_surfaceNormal, {}, m_surfaceNormalView ) ||
		!d3d.createTexture( hizDesc, nullptr, m_hiz ) || !d3d.createShaderView( m_hiz, {}, m_hizView ) ||
		!d3d.createTexture( resultDesc, nullptr, m_result ) || !d3d.createShaderView( m_result, {}, m_resultView ) ||
		!d3d.createStorageView( m_result, {}, m_resultUAV ) )
	{
		LOG( "Screen reflections: textures are not created" );
		return false;
	}
	m_hizLevelViews.resize( hizMips );
	m_hizLevelUAVs.resize( hizMips );
	for( uint32_t mip = 0; mip < hizMips; ++mip )
	{
		TextureViewDesc levelDesc;
		levelDesc.firstMip = mip;
		levelDesc.mipCount = 1;
		if( !d3d.createShaderView( m_hiz, levelDesc, m_hizLevelViews[mip] ) || !d3d.createStorageView( m_hiz, levelDesc, m_hizLevelUAVs[mip] ) )
		{
			LOG( "Screen reflections: depth pyramid views are not created" );
			return false;
		}
	}
	d3d.setName( m_surfaceDepth, "Reflection surface depth" );
	d3d.setName( m_surfaceNormal, "Reflection surface normal" );
	d3d.setName( m_hiz, "Scene depth pyramid" );
	d3d.setName( m_result, "Screen reflection" );
	return true;
}

void ScreenSpaceReflections::buildPyramid( const ShaderView& sceneDepth )
{
	DMD3D& d3d = DMD3D::instance();
	// Ближайшая и дальняя из 2 × 2 уровня ниже; уровни 1…4 (мипы 0…3) — один проход из глубины сцены
	PassDesc first{ "Scene depth pyramid", {}, {}, 0, 0, { { &sceneDepth, "scene depth" } }, {} };
	for( uint32_t mip = 0; mip < hizFirstMips; ++mip )
		first.writes.push_back( { &m_hizLevelUAVs[mip], "scene depth pyramid" } );
	d3d.beginPass( first );
	d3d.setSRV( 0, sceneDepth );
	for( uint32_t mip = 0; mip < hizFirstMips; ++mip )
		m_hizFirstShader.setUAVBuffer( 1 + mip, m_hizLevelUAVs[mip] );
	m_hizFirstShader.dispatchGroups( m_hizWidth / 16, m_hizHeight / 16, 1 );
	// Уровни 5…7 (мипы 4…6) из уровня 4: группа — 8 × 8 текселей уровня 5, их по стороне — сторона уровня 0 / 32
	PassDesc rest{ "Scene depth pyramid", {}, {}, 0, 0, { { &m_hizLevelViews[hizFirstMips - 1], "scene depth pyramid" } }, {} };
	for( uint32_t mip = hizFirstMips; mip < hizMips; ++mip )
		rest.writes.push_back( { &m_hizLevelUAVs[mip], "scene depth pyramid" } );
	d3d.beginPass( rest );
	d3d.setSRV( 1, m_hizLevelViews[hizFirstMips - 1] );
	for( uint32_t mip = hizFirstMips; mip < hizMips; ++mip )
		m_hizRestShader.setUAVBuffer( 1 + mip - hizFirstMips, m_hizLevelUAVs[mip] );
	m_hizRestShader.dispatchGroups( groups( m_hizWidth >> ( hizFirstMips + 1 ) ), groups( m_hizHeight >> ( hizFirstMips + 1 ) ), 1 );
	// Лучам (screen_reflection_trace.sh): привязка завершает проход над пирамидой
	d3d.setSRV( SLOT_SCENE_DEPTH_PYRAMID, m_hizView );
}

PassDesc ScreenSpaceReflections::surfacePass() const
{
	return PassDesc{ "Pass reflection prepass", { { &m_surfaceNormalTarget, "reflection surface normal" } },
					 { &m_surfaceDepthTarget, "reflection surface depth" }, m_width, m_height };
}

void ScreenSpaceReflections::clearSurfaces() const
{
	DMD3D& d3d = DMD3D::instance();
	PassDesc desc = surfacePass();
	desc.name = "Reflection surface clear";
	d3d.beginPass( desc );
	const float zero[4] = {};
	d3d.clearTarget( m_surfaceNormalTarget, zero );
	d3d.clearDepth( m_surfaceDepthTarget, 0.0f );
}

void ScreenSpaceReflections::trace( const ShaderView& sceneDepth )
{
	DMD3D& d3d = DMD3D::instance();
	d3d.beginPass( PassDesc{ "Screen reflections", {}, {}, 0, 0,
							 { { &sceneDepth, "scene depth" }, { &m_hizView, "scene depth pyramid" },
							   { &m_surfaceDepthView, "reflection surface depth" }, { &m_surfaceNormalView, "reflection surface normal" } },
							 { { &m_resultUAV, "screen reflection" } } } );
	d3d.setSRV( SLOT_SCENE_DEPTH, sceneDepth );
	d3d.setSRV( SLOT_SCENE_DEPTH_PYRAMID, m_hizView );
	d3d.setSRV( 3, m_surfaceDepthView );
	d3d.setSRV( 4, m_surfaceNormalView );
	m_traceShader.setUAVBuffer( 0, m_resultUAV );
	m_traceShader.dispatchGroups( groups( m_width ), groups( m_height ), 1 );
	// Проходу transparent (Shaders/water_shading.sh): привязка завершает проход над результатом
	d3d.setSRV( SLOT_SCREEN_REFLECTION, m_resultView );
}

}
