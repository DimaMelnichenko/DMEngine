#pragma once

#include "GpuResources.h"

// Промежуточная цель прохода: текстура 2D, в которую проход рисует (target) и из которой следующий читает (srv).
// Проход объявляет её целью: DMD3D::beginPass( PassDesc{ имя, { { &target(), … } }, {}, width(), height() } )
class RenderTarget
{
public:
	// name — имя текстуры в захвате PIX и сообщениях debug-слоя
	bool create( uint32_t width, uint32_t height, DXGI_FORMAT format, const char* name = nullptr );

	const TargetView& target() const { return m_target; }
	const ShaderView& srv() const { return m_srv; }
	uint32_t width() const { return m_texture.width(); }
	uint32_t height() const { return m_texture.height(); }

private:
	Texture m_texture;
	TargetView m_target;
	ShaderView m_srv;
};
