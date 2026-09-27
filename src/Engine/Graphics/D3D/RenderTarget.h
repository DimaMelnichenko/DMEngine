#pragma once

#include <d3d11.h>
#include "Utils\utilites.h"

// Промежуточная цель прохода: текстура 2D, в которую проход рисует (RTV) и из которой следующий читает (SRV).
// Проход ставит её целью через DMD3D::setRenderTarget( rtv(), width(), height() )
class RenderTarget
{
public:
	bool create( uint32_t width, uint32_t height, DXGI_FORMAT format );

	ID3D11RenderTargetView* rtv() const { return m_rtv.get(); }
	const com_unique_ptr<ID3D11ShaderResourceView>& srv() const { return m_srv; }
	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }

private:
	com_unique_ptr<ID3D11Texture2D> m_texture;
	com_unique_ptr<ID3D11RenderTargetView> m_rtv;
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
};
