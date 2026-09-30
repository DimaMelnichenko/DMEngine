#pragma once

#include "DirectX.h"
#include <vector>

// Общие сэмплеры всех шейдеров: слоты — как в Shaders/samplers.sh и Shaders/slots.h. В D3D12 это статические
// сэмплеры root signature (DMD3D собирает её из этой таблицы), привязки на кадр нет
class DMSamplerState
{
public:
	bool initialize();

	// Порядок — слоты s0…s8
	enum SamplerType
	{
		pointClamp, pointWrap, pointBorder, linearClamp, linearWrap, linearBorder, anisotropicClamp, anisotropicWrap,
		shadowCompare,	// s8 (SLOT_SAMPLER_SHADOW): сравнение GREATER_EQUAL для карты теней с обратной глубиной, за краем — 0
		count
	};

	// Осталось от D3D11 (привязка раз за кадр); в D3D12 сэмплеры в root signature — ничего не делает
	void setDefaultSamplers() {}

	// Описания статических сэмплеров root signature, слоты s0…s8
	const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers() const { return m_samplers; }

private:
	std::vector<D3D12_STATIC_SAMPLER_DESC> m_samplers;
};
