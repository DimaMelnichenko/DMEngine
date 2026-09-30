#pragma once

#include <vector>
#include "GpuResources.h"

// Cubemap, в который рисуют по граням (цель на каждую грань каждого мипа) и который потом читают как куб (srv).
// Небо и панорама рендерят в него окружение, SkyLight строит из него освещение окружением. По желанию — один мип
// как массив из 6 граней: compute-шейдер читает тексели через Load (гармоники рассеянного света)
class CubeTarget
{
public:
	// mipCount 0 — полная цепочка мипов; generateMips — мипы потом строит DMD3D::generateMips
	bool create( uint32_t size, uint32_t mipCount, DXGI_FORMAT format, bool generateMips );
	// Мип mip как Texture2DArray из 6 граней — facesSRV()
	bool createFacesView( uint32_t mip );

	const TargetView& target( uint32_t mip, uint32_t face ) const { return m_targets[mip * 6 + face]; }
	const ShaderView& srv() const { return m_srv; }
	const ShaderView& facesSRV() const { return m_facesSRV; }
	uint32_t size() const { return m_texture.width(); }
	uint32_t mipSize( uint32_t mip ) const { return size() >> mip > 0 ? size() >> mip : 1; }
	uint32_t mipCount() const { return m_texture.mipCount(); }

private:
	Texture m_texture;
	std::vector<TargetView> m_targets;	// мип × 6 + грань
	ShaderView m_srv;
	ShaderView m_facesSRV;
};
