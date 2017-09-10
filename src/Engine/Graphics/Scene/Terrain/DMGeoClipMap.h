#pragma once


#include <vector>

#include "DMClipMapShader.h"
#include "DMGeoLevel.h"
#include "..\DMSceneObject.h"

class DMGeoClipMap : public DMSceneObject
{
public:
	DMGeoClipMap( DMD3D* );
	virtual ~DMGeoClipMap();

	bool Initialize( int N, int levels, float scale, float height );

	void Render( DMClipMapShader* shader, DMCamera*, DMFrustum* );

	float scaleMultipler();
	float heightMultipler();

private:
	bool InitializeBuffers();
	bool inint_MxM_blocks();
	bool inint_Mx3_blocks();
	bool inint_2M1_blocks();

protected:
	void fill_L_indexes( D3DXVECTOR2* cam_pos, DMGeoLevel& current_level, DMGeoLevel& next_level );
	void fill_row_vert( int row, int start, int end, unsigned int* indexes, unsigned int& index );
	void fill_row_hor( int row, int start, int end, unsigned int* indexes, unsigned int& index );

	struct vertex
	{
		D3DXVECTOR3 pos;
		D3DXVECTOR2 tex;
	};
	
	
	com_unique_ptr<ID3D11Buffer> m_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_index_buffer_full;
	com_unique_ptr<ID3D11Buffer> m_index_buffer_cut;
	com_unique_ptr<ID3D11Buffer> m_MxM_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_MxM_index_buffer;
	com_unique_ptr<ID3D11Buffer> m_Mx3_horizontal_index_buffer;
	com_unique_ptr<ID3D11Buffer> m_Mx3_vertical_index_buffer;	
	com_unique_ptr<ID3D11Buffer> m_2M1_horizontal_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_2M1_vertical_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_2M1_horizontal_index_buffer;
	com_unique_ptr<ID3D11Buffer> m_2M1_vertical_index_buffer;
	unsigned long m_index_count_full;
	unsigned long m_index_count_cut;
	unsigned long m_index_count_L;
	unsigned long m_vertex_count;
	unsigned long m_MxM_vertex_count;
	unsigned long m_2M1_vertical_vertex_count;
	unsigned long m_2M1_horizontal_vertex_count;
	unsigned long m_MxM_index_count;
	unsigned long m_Mx3_index_count;
	unsigned long m_2M1_vertical_index_count;
	unsigned long m_2M1_horizontal_index_count;
	unsigned int m_N;
	unsigned int m_M;

	float m_scale_multipler;
	float m_height_multipler;

	
	std::vector<DMGeoLevel> m_levels;
	std::vector<unsigned long> m_indexes_cut;
	std::vector<D3DXVECTOR2> m_MxM_offsets;
	std::vector<D3DXVECTOR2> m_Mx3_offsets;
};

