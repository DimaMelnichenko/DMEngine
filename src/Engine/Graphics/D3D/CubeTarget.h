#pragma once

#include <d3d11.h>
#include <vector>
#include "Utils\utilites.h"

// Cubemap, в который рисуют по граням (цель на каждую грань каждого мипа) и который потом читают как куб (SRV).
// Небо и панорама рендерят в него окружение, SkyLight строит из него освещение окружением. По желанию — один мип
// как массив из 6 граней: compute-шейдер читает тексели через Load (гармоники рассеянного света)
class CubeTarget
{
public:
	// mipCount 0 — полная цепочка мипов; generateMips — мипы потом строит ID3D11DeviceContext::GenerateMips
	bool create( uint32_t size, uint32_t mipCount, DXGI_FORMAT format, bool generateMips );
	// Мип mip как Texture2DArray из 6 граней — facesSRV()
	bool createFacesView( uint32_t mip );

	ID3D11RenderTargetView* rtv( uint32_t mip, uint32_t face ) const { return m_targets[mip * 6 + face].get(); }
	const com_unique_ptr<ID3D11ShaderResourceView>& srv() const { return m_srv; }
	const com_unique_ptr<ID3D11ShaderResourceView>& facesSRV() const { return m_facesSRV; }
	uint32_t size() const { return m_size; }
	uint32_t mipSize( uint32_t mip ) const { return m_size >> mip > 0 ? m_size >> mip : 1; }
	uint32_t mipCount() const { return m_mipCount; }

private:
	com_unique_ptr<ID3D11Texture2D> m_texture;
	std::vector<com_unique_ptr<ID3D11RenderTargetView>> m_targets;	// мип × 6 + грань
	com_unique_ptr<ID3D11ShaderResourceView> m_srv;
	com_unique_ptr<ID3D11ShaderResourceView> m_facesSRV;
	DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
	uint32_t m_size = 0;
	uint32_t m_mipCount = 0;
};
