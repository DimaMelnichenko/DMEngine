#pragma once

#include "DMShader.h"
#include "DMFrustum.h"

class DMClipMapShader :	public DMShader
{
public:
	DMClipMapShader( DMD3D* );
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


private:
	void prepareRender(  );
	std::vector<D3D11_INPUT_ELEMENT_DESC> initLayouts(  );
	void Update( float );

private:
	std::shared_ptr<ID3D11Buffer> m_shader_param;
	std::shared_ptr<ID3D11Buffer> m_offset_sbuffer;
	std::shared_ptr<ID3D11Buffer> m_ps_shader_param;
	std::shared_ptr<ID3D11ShaderResourceView> m_srv_buffer;
	unsigned int m_max_MxM_instance_count;
	
};


