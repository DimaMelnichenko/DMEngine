#pragma once

#include "DirectX.h"
#include <vector>
#include <Utils\utilites.h>

// Общие сэмплеры всех шейдеров: слоты — как в Shaders/samplers.sh и Shaders/slots.h. Ставятся раз за кадр
// (Renderer::preparePipeline); в D3D12 станут статическими сэмплерами root signature
class DMSamplerState
{
public:
	DMSamplerState();
	~DMSamplerState();

	bool initialize();

	// Порядок — слоты s0…s8
	enum SamplerType
	{
		pointClamp, pointWrap, pointBorder, linearClamp, linearWrap, linearBorder, anisotropicClamp, anisotropicWrap,
		shadowCompare,	// s8 (SLOT_SAMPLER_SHADOW): сравнение GREATER_EQUAL для карты теней с обратной глубиной, за краем — 0
		count
	};

	void setDefaultSamplers();

private:
	std::vector<ID3D11SamplerState*> m_samplerPointers;
	std::vector<com_unique_ptr<ID3D11SamplerState>> m_sampler_states;
};
