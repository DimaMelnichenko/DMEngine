#pragma once

#include "DMOceanShader.h"

class DMOcean :	public DMGeoClipMap
{
public:
	DMOcean(  );
	~DMOcean();

	void Render( std::unique_ptr<ID3D11DeviceContext>& _device_context, std::unique_ptr<DMOceanShader>& _shader, DMCamera*, float _time );
};

