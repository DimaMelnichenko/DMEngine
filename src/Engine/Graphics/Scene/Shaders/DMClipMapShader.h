#pragma once

#include "..\Shaders\DMShader.h"
#include "..\Camera\DMFrustum.h"
#include "D3D\DMStructuredBuffer.h"

class DMClipMapShader :	public GS::DMShader
{
public:
	DMClipMapShader();
	~DMClipMapShader();

	struct alignas( 16 ) ParamBuffer
	{
		float levelScale;
		float mapSize;
		float mapWidthMultipler;
		float mapHeightMultippler;
		float map_N;
		float hightOffset;
		float mapScale;
		float mapOffsetSpeed;
		D3DXVECTOR2 mapOffset;
		D3DXVECTOR2 dump;
	};

	struct  InstanceOffset
	{
		D3DXVECTOR2 level_pos;
		D3DXVECTOR2 offset;
		float scale;
	};

	struct alignas( 16 ) PSParamBuffer
	{
		int materialsCount;
	};

	struct Material
	{
		D3DXVECTOR3 baseColors;
		float baseStartHeight;
		float baseBlends;
		float textureScale;
	};

	bool SetVSParameters( ParamBuffer* );
	bool SetPSParameters( PSParamBuffer* );
	bool setInstanceOffset( std::vector<InstanceOffset> & );
	void SetTextures( int count, ID3D11ShaderResourceView** _textures );

	void setParams( const GS::ParamSet & );
	void updateMaterials( std::vector<Material>& materials );

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
	DMStructuredBuffer m_blockOffsets;
	DMStructuredBuffer m_materials;
	
};


