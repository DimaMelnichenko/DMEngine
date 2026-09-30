#pragma once

#include <string>
#include "DMShader.h"

namespace GS
{

// Проход на всю цель рендера: треугольник по SV_VertexID на дальней плоскости (Shaders/fullscreen.vs) и свой
// пиксельный шейдер. draw() сам ставит топологию, шейдеры и состояния (без отсечения граней, по умолчанию и без
// глубины) — не зависит от того, что оставил предыдущий проход. Цель рендера и ресурсы задаёт вызывающий:
// DMD3D::setRenderTarget, setSRV
class FullscreenShader
{
public:
	// formats — цели, в которые проход рисует (для прогрева пайплайнов; с глубиной — ещё вариант фона неба)
	bool load( const std::string& pixelShader, const TargetFormats& formats = {} );
	// depth = DepthState::readOnlyNearOrEqual — только там, где ничего не нарисовано (фон неба на глубине 0)
	void draw( BlendState blend = BlendState::opaque, DepthState depth = DepthState::disabled );

private:
	DMShader m_shader;
};

}
