#include "FullscreenShader.h"
#include "D3D\DMD3D.h"

namespace GS
{

bool FullscreenShader::load( const std::string& pixelShader )
{
	// Без буферов и раскладки вершин: вершинный шейдер строит треугольник по номеру вершины
	m_shader.setDrawType( DMShader::by_vertex );
	if( !m_shader.addShaderPassFromFile( SRVType::vs, "main", "Shaders\\fullscreen.vs" ) ||
		!m_shader.addShaderPassFromFile( SRVType::ps, "main", pixelShader ) ||
		m_shader.createPhase( 0, 0 ) < 0 )
		return false;
	// Состояния draw(): без глубины и с накоплением (bloom), фон неба на дальней плоскости
	m_shader.warmPipelines( { { RasterState::noCulling, DepthState::disabled, BlendState::opaque },
							  { RasterState::noCulling, DepthState::disabled, BlendState::additive },
							  { RasterState::noCulling, DepthState::readOnlyNearOrEqual, BlendState::opaque } } );
	return true;
}

void FullscreenShader::draw( BlendState blend, DepthState depth )
{
	// Сплошная заливка, даже если кадр рисуется каркасом (Q)
	ScopedRenderState state( RasterState::noCulling, depth, blend );
	m_shader.setPass( 0 );
	m_shader.render( 3 );
}

}
