#include "DMGeoLevel.h"



DMGeoLevel::DMGeoLevel( int _dimension, int _level )
{
	m_current_pos = XMFLOAT2( 0.0, 0.0 );
	m_dimension = _dimension;
	setLevel( _level );
}


DMGeoLevel::~DMGeoLevel( )
{
}

XMFLOAT2 DMGeoLevel::calcPos( XMFLOAT2 _pos )
{
	return calcPos( _pos.x, _pos.y );
}

XMFLOAT2 DMGeoLevel::calcPos( float _x, float _y )
{
	XMFLOAT2 result;

	// Store the old position to be able to recover it if needed 
	int oldX = m_current_pos.x;
	int oldZ = m_current_pos.y;

	// Calculate the new position 
	m_current_pos.x = floor( _x - ( m_dimension + 1 ) * 0.5f * m_scale );
	m_current_pos.y = floor( _y - ( m_dimension + 1 ) * 0.5f * m_scale );
	// Calculate the modulo to G2 of the new position. 
	// This makes sure that the current level always fits in the hole of the 
	// coarser level. The gridspacing of the coarser level is G * 2, so here G2.
	int scale2 = ( m_scale * 2 );
	int mod_x = (int)( m_current_pos.x ) % scale2;
	int mod_y = (int)( m_current_pos.y ) % scale2;
	mod_x += mod_x < 0 ? scale2 : 0;
	mod_y += mod_y < 0 ? scale2 : 0;
	m_current_pos.x += scale2 - mod_x;
	m_current_pos.y += scale2 - mod_y;

	return m_current_pos;
}


float DMGeoLevel::scale( )
{
	return m_scale;
}

void DMGeoLevel::setLevel( int _level )
{
	m_level = _level;
	m_scale = pow( 2.0, m_level );
}

int DMGeoLevel::level( )
{
	return m_level;
}

void DMGeoLevel::setDimension( int _dim )
{
	m_dimension = _dim;
}