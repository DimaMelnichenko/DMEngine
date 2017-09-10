#pragma once

#include "XRootDraw.h"
#include <D3DX10math.h>
#include <vector>
#include "GeoLevel.h"

class Geoclipmap : public XRootDraw
{
public:
	Geoclipmap( ID3D11Device* );
	~Geoclipmap();

	virtual void elapsedTime( float );
	virtual bool init( );
	virtual bool PrepareRender( ID3D11DeviceContext* pImmediateContext );
	virtual void Render( ID3D11DeviceContext* );
	virtual void Release( void );

private:
	void fill_L_indexes( ID3D11DeviceContext* pImmediateContext );
	struct vertex
	{
		D3DXVECTOR3 pos;
		D3DXVECTOR2 tex;
	};

	struct ConstantBuffer
	{
		XMFLOAT2 offset;
		float scale;
		float dumb;
	};

	ID3D11Buffer* m_vertex_buffer;
	ID3D11Buffer* m_index_buffer_full;
	ID3D11Buffer* m_index_buffer_cut;
	ID3D11Buffer* m_constant_buffer;
	unsigned long m_index_count_full;
	unsigned long m_index_count_cut;
	unsigned long m_index_count_L;
	unsigned long m_vertex_count;

	std::vector<GeoLevel> m_levels;
};

