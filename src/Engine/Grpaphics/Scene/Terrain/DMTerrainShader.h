#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include <d3dx11async.h>
#include <fstream>
#include "DMShader.h"
#include "DMLight.h"
#include <vector>


class DMTerrainShader : public DMShader
{
private:	
	struct LightBufferType
	{	
		D3DXVECTOR3 lightDirection;
		float padding;
	};

	struct TopologyType
	{
		D3DXVECTOR4 param_set;
	};

	struct TexParamType
	{
		D3DXVECTOR4 param_set;
	};

	struct TerrainType
	{
		D3DXVECTOR4 texOffset;
		D3DXVECTOR4 scale;
		D3DXVECTOR4 neighbors;
	};

public:
	DMTerrainShader( DMD3D* );
	~DMTerrainShader();
	
	virtual void Update( float );
	bool SetParameters( ID3D11DeviceContext*, DMLight*, float _level, ID3D11ShaderResourceView**, int _num_resource );	
	bool SetTextureParams( ID3D11DeviceContext* _deviceContext, float _repeat, float _speed_translate );
	bool setTextureCoordOffset( ID3D11DeviceContext* deviceContext, float start_x, float start_y, float end_x, float end_y, 
								D3DXVECTOR4 terrain_params, D3DXVECTOR4* neighbors );

private:
	virtual void prepareRender(  );
	virtual void Shutdown( );	
	void ShutdownShader( );
	virtual std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );

private:			
	ID3D11SamplerState* m_sampleState;	
	ID3D11SamplerState* m_sampleHM;
	ID3D11Buffer* m_lightBuffer;
	ID3D11Buffer* m_terrainBuffer;
	ID3D11Buffer* m_topologyBuffer;
	ID3D11Buffer* m_textureBuffer;
	std::vector<IUnknown*> m_interfaces;
	float m_current_tex_offset;
	float m_tex_offset_speed;
	float m_tex_repeat;
	float m_elapsedTime;
	float m_sum_time;
};

