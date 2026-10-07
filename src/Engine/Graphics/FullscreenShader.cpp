#include "FullscreenShader.h"
#include "D3D\DMD3D.h"

namespace GS
{

bool FullscreenShader::load( const std::string& pixelShader, const TargetFormats& formats )
{
	// Без буферов и раскладки вершин: вершинный шейдер строит треугольник по номеру вершины
	if( !m_shader.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\fullscreen.vs" ) ||
		!m_shader.addShaderPassFromFile( ShaderStageType::pixel, "main", pixelShader ) ||
		m_shader.createPhase( 0, 0 ) < 0 )
		return false;
	// Состояния draw(): без глубины и с накоплением (bloom); с буфером глубины — фон неба на дальней плоскости и облака
	// поверх него (смешивание по непрозрачности)
	if( formats.colorCount )
	{
		std::vector<RenderState> states = { { RasterState::noCulling, DepthState::disabled, BlendState::opaque },
											{ RasterState::noCulling, DepthState::disabled, BlendState::additive } };
		if( formats.depth != DXGI_FORMAT_UNKNOWN )
		{
			states.push_back( { RasterState::noCulling, DepthState::readOnlyNearOrEqual, BlendState::opaque } );
			states.push_back( { RasterState::noCulling, DepthState::readOnlyNearOrEqual, BlendState::alpha } );
		}
		m_shader.warmPipelines( states, formats );
	}
	return true;
}

void FullscreenShader::draw( BlendState blend, DepthState depth )
{
	// Сплошная заливка, даже если кадр рисуется каркасом (Q)
	ScopedRenderState state( RasterState::noCulling, depth, blend );
	m_shader.setPass( 0 );
	DMD3D::instance().draw( 3, 0 );
}

}
