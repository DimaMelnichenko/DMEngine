#pragma once

#include "..\Shaders\DMShader.h"
#include "..\Camera\DMFrustum.h"

class DMClipMapShader :	public GS::DMShader
{
public:
	DMClipMapShader();
	~DMClipMapShader();

	struct alignas( 16 ) ParamBuffer
	{
		D3DXVECTOR2 map_offset;		
		float map_scale;
		float map_N;
		float map_height_multippler;
		float map_texture_scale;
	};

	struct  InstanceOffset
	{
		D3DXVECTOR2 level_pos;
		D3DXVECTOR2 offset;
		float scale;
	};

	struct alignas( 16 ) PSParamBuffer
	{
		float texture_tile[8];
		float base_normal_multipler;
		D3DXVECTOR3 camera_position;
		float app_time;
	};

	bool SetVSParameters( ParamBuffer* );
	bool SetPSParameters( PSParamBuffer* );
	bool setInstanceOffset( std::vector<InstanceOffset> & );
	void SetTextures( int count, ID3D11ShaderResourceView** _textures );

	void setParams( const GS::ParamSet & );

private:
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

	bool innerInitialize();

private:
	com_unique_ptr<ID3D11Buffer> m_shader_param;
	com_unique_ptr<ID3D11Buffer> m_offset_sbuffer;
	com_unique_ptr<ID3D11Buffer> m_ps_shader_param;
	com_unique_ptr<ID3D11ShaderResourceView> m_srv_buffer;
	unsigned int m_max_MxM_instance_count;
	
};


