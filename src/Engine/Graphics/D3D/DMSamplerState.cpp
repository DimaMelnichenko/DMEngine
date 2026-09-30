#include "DMSamplerState.h"

bool DMSamplerState::initialize()
{
	if( !m_samplers.empty() )
		return true;

	// Слот за слотом: фильтр и адресация (s0…s7 — как SamplerState в Shaders/samplers.sh)
	struct Description
	{
		D3D12_FILTER filter;
		D3D12_TEXTURE_ADDRESS_MODE address;
	};
	const Description descriptions[shadowCompare] = {
		{ D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
		{ D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP },
		{ D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER },
		{ D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
		{ D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP },
		{ D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER },
		{ D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP },
		{ D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP },
	};
	UINT slot = 0;
	for( const Description& description : descriptions )
	{
		D3D12_STATIC_SAMPLER_DESC desc = {};
		desc.Filter = description.filter;
		desc.AddressU = desc.AddressV = desc.AddressW = description.address;
		desc.MaxAnisotropy = description.filter == D3D12_FILTER_ANISOTROPIC ? 8 : 1;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;	// за краем — 0, как у сэмплеров D3D11 по умолчанию
		desc.MaxLOD = D3D12_FLOAT32_MAX;
		desc.ShaderRegister = slot++;
		desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		m_samplers.push_back( desc );
	}

	// Карта теней (Shaders/shadows.sh): линейный PCF с сравнением — «освещено», если глубина пикселя не дальше
	// от света, чем записанная (≥, глубина обратная); за краем карты — «освещено»: граница 0 — дальняя плоскость.
	// Читают только пиксельные шейдеры
	D3D12_STATIC_SAMPLER_DESC shadow = {};
	shadow.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadow.AddressU = shadow.AddressV = shadow.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadow.MaxAnisotropy = 1;
	shadow.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	shadow.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	shadow.MaxLOD = D3D12_FLOAT32_MAX;
	shadow.ShaderRegister = shadowCompare;
	shadow.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	m_samplers.push_back( shadow );
	return true;
}
