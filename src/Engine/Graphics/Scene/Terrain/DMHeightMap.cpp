
#include "DMHeightMap.h"
#include "D3D\DMD3D.h"

#include <assert.h>
#include <iostream>
#include <fstream>

DMHeightMap::DMHeightMap()
{

}
DMHeightMap::~DMHeightMap()
{
	
}

void DMHeightMap::setHeightMapTexture( GS::DMTexture* texture )
{
	m_heightMap = texture;
}

XMFLOAT2 DMHeightMap::size() const
{
	return XMFLOAT2( m_heightMap->image().GetMetadata().height, m_heightMap->image().GetMetadata().width );
}

float DMHeightMap::height( float x, float y ) const
{
	/*if( x < 0 || y < 0 || x > size().x || y > size().y )
		return 0.0;

	y = size().y - y;

	XMFLOAT3 abc_1[3];
	XMFLOAT3 abc_2[3];
	XMFLOAT3 point;
	
	point.x = x;
	point.z = y;

	float control_x = floor( x );
	float control_z = floor( y );
	
	abc_1[0] = XMFLOAT3( control_x, pixel( control_x, control_z ), control_z );
	abc_1[1] = XMFLOAT3( control_x + 1, pixel( control_x + 1, control_z ), control_z );
	abc_1[2] = XMFLOAT3( control_x + 1, pixel( control_x + 1, control_z + 1 ), control_z + 1 );

	abc_2[0] = XMFLOAT3( control_x, pixel( control_x, control_z ), control_z );
	abc_2[1] = XMFLOAT3( control_x + 1, pixel( control_x + 1, control_z + 1 ), control_z + 1 );
	abc_2[2] = XMFLOAT3( control_x, pixel( control_x, control_z + 1 ), control_z + 1 );

	if( checkHeightOfTriangle( &point, abc_1 ) )
	{
		return point.y;
	}
	else if( checkHeightOfTriangle( &point, abc_2 ) )
	{
		return point.y;
	}
	*/
	return 0.0;
}

bool DMHeightMap::in_triangle( XMFLOAT2* d, XMFLOAT2* abc )
{
	double xa, ya, xb, yb, xc, yc, xd, yd;

	xa = abc[0].x;
	ya = abc[0].y;
	xb = abc[1].x;
	yb = abc[1].y;
	xc = abc[2].x;
	yc = abc[2].y;
	xd = d->x;
	yd = d->y;

	return  ( ( ( xd - xa )*( yb - ya ) - ( yd - ya )*( xb - xa ) )*( ( xc - xa )*( yb - ya ) - ( yc - ya )*( xb - xa ) ) >= 0 ) &&
		( ( ( xd - xb )*( yc - yb ) - ( yd - yb )*( xc - xb ) )*( ( xa - xb )*( yc - yb ) - ( ya - yb )*( xc - xb ) ) >= 0 ) &&
		( ( ( xd - xc )*( ya - yc ) - ( yd - yc )*( xa - xc ) )*( ( xb - xc )*( ya - yc ) - ( yb - yc )*( xa - xc ) ) >= 0 );
}

bool DMHeightMap::checkHeightOfTriangle( XMFLOAT3* point, XMFLOAT3* abc )
{
	float startVector[3], directionVector[3], edge1[3], edge2[3], normal[3];
	float Q[3], e1[3], e2[3], e3[3], edgeNormal[3], temp[3];
	float magnitude, D, denominator, numerator, t, determinant;
	float v0[3], v1[3], v2[3];
	v0[0] = abc[0].x;
	v0[1] = abc[0].y;
	v0[2] = abc[0].z;
	v1[0] = abc[1].x;
	v1[1] = abc[1].y;
	v1[2] = abc[1].z;
	v2[0] = abc[2].x;
	v2[1] = abc[2].y;
	v2[2] = abc[2].z;

	// Starting position of the ray that is being cast.
	startVector[0] = point->x;
	startVector[1] = 0.0f;
	startVector[2] = point->z;

	// The direction the ray is being cast.
	directionVector[0] = 0.0f;
	directionVector[1] = -1.0f;
	directionVector[2] = 0.0f;

	// Calculate the two edges from the three points given.
	edge1[0] = v1[0] - v0[0];
	edge1[1] = v1[1] - v0[1];
	edge1[2] = v1[2] - v0[2];

	edge2[0] = v2[0] - v0[0];
	edge2[1] = v2[1] - v0[1];
	edge2[2] = v2[2] - v0[2];

	// Calculate the normal of the triangle from the two edges.
	normal[0] = ( edge1[1] * edge2[2] ) - ( edge1[2] * edge2[1] );
	normal[1] = ( edge1[2] * edge2[0] ) - ( edge1[0] * edge2[2] );
	normal[2] = ( edge1[0] * edge2[1] ) - ( edge1[1] * edge2[0] );

	magnitude = (float)sqrt( ( normal[0] * normal[0] ) + ( normal[1] * normal[1] ) + ( normal[2] * normal[2] ) );
	normal[0] = normal[0] / magnitude;
	normal[1] = normal[1] / magnitude;
	normal[2] = normal[2] / magnitude;

	// Find the distance from the origin to the plane.
	D = ( ( -normal[0] * v0[0] ) + ( -normal[1] * v0[1] ) + ( -normal[2] * v0[2] ) );

	// Get the denominator of the equation.
	denominator = ( ( normal[0] * directionVector[0] ) + ( normal[1] * directionVector[1] ) + ( normal[2] * directionVector[2] ) );

	// Make sure the result doesn't get too close to zero to prevent divide by zero.
	if( fabs( denominator ) < 0.0001f )
	{
		return false;
	}

	// Get the numerator of the equation.
	numerator = -1.0f * ( ( ( normal[0] * startVector[0] ) + ( normal[1] * startVector[1] ) + ( normal[2] * startVector[2] ) ) + D );

	// Calculate where we intersect the triangle.
	t = numerator / denominator;

	// Find the intersection vector.
	Q[0] = startVector[0] + ( directionVector[0] * t );
	Q[1] = startVector[1] + ( directionVector[1] * t );
	Q[2] = startVector[2] + ( directionVector[2] * t );

	// Find the three edges of the triangle.
	e1[0] = v1[0] - v0[0];
	e1[1] = v1[1] - v0[1];
	e1[2] = v1[2] - v0[2];

	e2[0] = v2[0] - v1[0];
	e2[1] = v2[1] - v1[1];
	e2[2] = v2[2] - v1[2];

	e3[0] = v0[0] - v2[0];
	e3[1] = v0[1] - v2[1];
	e3[2] = v0[2] - v2[2];

	// Calculate the normal for the first edge.
	edgeNormal[0] = ( e1[1] * normal[2] ) - ( e1[2] * normal[1] );
	edgeNormal[1] = ( e1[2] * normal[0] ) - ( e1[0] * normal[2] );
	edgeNormal[2] = ( e1[0] * normal[1] ) - ( e1[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v0[0];
	temp[1] = Q[1] - v0[1];
	temp[2] = Q[2] - v0[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Calculate the normal for the second edge.
	edgeNormal[0] = ( e2[1] * normal[2] ) - ( e2[2] * normal[1] );
	edgeNormal[1] = ( e2[2] * normal[0] ) - ( e2[0] * normal[2] );
	edgeNormal[2] = ( e2[0] * normal[1] ) - ( e2[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v1[0];
	temp[1] = Q[1] - v1[1];
	temp[2] = Q[2] - v1[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Calculate the normal for the third edge.
	edgeNormal[0] = ( e3[1] * normal[2] ) - ( e3[2] * normal[1] );
	edgeNormal[1] = ( e3[2] * normal[0] ) - ( e3[0] * normal[2] );
	edgeNormal[2] = ( e3[0] * normal[1] ) - ( e3[1] * normal[0] );

	// Calculate the determinant to see if it is on the inside, outside, or directly on the edge.
	temp[0] = Q[0] - v2[0];
	temp[1] = Q[1] - v2[1];
	temp[2] = Q[2] - v2[2];

	determinant = ( ( edgeNormal[0] * temp[0] ) + ( edgeNormal[1] * temp[1] ) + ( edgeNormal[2] * temp[2] ) );

	// Check if it is outside.
	if( determinant > 0.001f )
	{
		return false;
	}

	// Now we have our height.
	point->y = Q[1];

	return true;
}