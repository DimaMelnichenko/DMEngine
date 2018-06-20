#pragma once

#include "DirectX.h"


class DMGeoLevel
{
public:
	DMGeoLevel( int _dimension, int _level );
	~DMGeoLevel();

	XMFLOAT2 calcPos( XMFLOAT2 );
	XMFLOAT2 calcPos( float, float );
	float scale( );
	void setLevel( int );
	int level( );
	void setDimension( int );

private:
	int m_level;
	float m_scale;
	int m_dimension;
	XMFLOAT2 m_current_pos;

};

