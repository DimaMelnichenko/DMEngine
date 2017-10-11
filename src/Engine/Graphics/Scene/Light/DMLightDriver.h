#pragma once

#include <D3DX11.h>
#include <D3DX10math.h>
#include <vector>

#include "DMLight.h"

class DMLightDriver : public DMSceneObject
{
public:
	using LightList = std::vector<DMLight>;
public:
	DMLightDriver( );
	~DMLightDriver();
	bool Initialize();
	void addLight( DMLight& );
	void setShaderBuffer();
	void update( float );
	void addCSMLayer( const DMCamera& );
	LightList& lights();
	

private:
	LightList m_light_list;
	com_unique_ptr<ID3D11Buffer> m_light_buffer;
	com_unique_ptr<ID3D11Buffer> m_CSM_buffer;

	struct alignas( 16 ) LightBuffer
	{
		D3DXVECTOR4 cb_lightPos[10]; // w = type
		D3DXVECTOR4 cb_lightDir[10]; // w = spot angle
		D3DXVECTOR4 cb_lightColor[10]; // w = attenuation		
		D3DXMATRIX cb_lightView[4];
		D3DXMATRIX cb_lightProject[4];
		float cb_shadow_texel[4]; // shadow map texel size		
		unsigned int cb_light_count;
		unsigned int cb_shadow_count; // current light with shadow count
	};


	struct alignas( 16 ) SunCSMBuffer
	{	
		D3DXMATRIX cb_CSM_View[8];
		D3DXMATRIX cb_CSM_Project[8];		
		int cb_CSM_count; // current CSM count
	};

	std::vector<DMCamera> m_CSM_layers;
};

