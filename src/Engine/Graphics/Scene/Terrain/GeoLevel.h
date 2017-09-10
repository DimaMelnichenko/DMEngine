#pragma once
#include <D3DX10math.h>

class GeoLevel
{
public:
	GeoLevel( int _dimension, int _level );
	~GeoLevel();

	D3DXVECTOR2 calcPos( D3DXVECTOR2 );	
	D3DXVECTOR2 calcPos( float, float );
	float scale();
	void setLevel( int );
	int level();
	void setDimension( int );

private:
	int m_level;
	float m_scale;
	int m_dimension;
	D3DXVECTOR2 m_current_pos;

};

