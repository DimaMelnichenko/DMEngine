#include "FullscreenShader.h"
#include "D3D\DMD3D.h"

namespace GS
{

bool FullscreenShader::load( const std::string& pixelShader )
{
	// Без буферов и раскладки вершин: вершинный шейдер строит треугольник по номеру вершины
	m_shader.setDrawType( DMShader::by_vertex );
	return m_shader.addShaderPassFromFile( SRVType::vs, "main", "Shaders\\fullscreen.vs" ) &&
		   m_shader.addShaderPassFromFile( SRVType::ps, "main", pixelShader ) &&
		   m_shader.createPhase( 0, 0 );
}

void FullscreenShader::draw( BlendState blend )
{
	// Сплошная заливка и без глубины, даже если кадр рисуется каркасом (Q)
	ScopedRenderState state( RasterState::noCulling, DepthState::disabled, blend );
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_shader.setPass( 0 );
	m_shader.render( 3 );
}

}
