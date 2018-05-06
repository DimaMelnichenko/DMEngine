#pragma once

#include "DirectX.h"
#include <vector>
#include <Utils\utilites.h>

class DMSamplerState
{
public:
	DMSamplerState();
	~DMSamplerState();

	bool initialize();

	enum SamplerType
	{
		st_point_wrap, st_point_clamp, st_point_border, st_linear_wrap, st_linear_clamp, st_linear_border, st_anisotrop_wrap, st_anisotrop_clamp,
		st_cmp_less
	};

	ID3D11SamplerState* sampler( SamplerType type );

	void setDefaultSmaplers();

private:
	std::vector<ID3D11SamplerState*> m_samplerPointers;
	std::vector<com_unique_ptr<ID3D11SamplerState>> m_sampler_states;
};

