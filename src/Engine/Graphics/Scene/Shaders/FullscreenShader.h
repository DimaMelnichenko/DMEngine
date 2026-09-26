#pragma once

#include <string>
#include "DMShader.h"

namespace GS
{

// Проход на всю цель рендера: треугольник по SV_VertexID (Shaders/fullscreen.vs) и свой пиксельный шейдер.
// draw() сам ставит топологию, шейдеры и состояния (без глубины и отсечения граней) — не зависит от того, что
// оставил предыдущий проход. Цель рендера и ресурсы задаёт вызывающий: DMD3D::setRenderTarget, setSRV
class FullscreenShader
{
public:
	bool load( const std::string& pixelShader );
	void draw( BlendState blend = BlendState::opaque );

private:
	DMShader m_shader;
};

}
