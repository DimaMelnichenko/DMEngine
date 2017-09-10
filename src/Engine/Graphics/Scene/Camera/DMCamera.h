#pragma once

//////////////
// INCLUDES //
//////////////
#include <d3d11.h>
#include <d3dx10math.h>
#include "..\..\Common\DMObject.h"



class DMCamera : public DMObject
{
public:
	DMCamera(  );	
	~DMCamera();

	enum CameraType
	{
		CT_PERSPECTIVE, CT_ORTHO
	};

	void Initialize( CameraType, float width, float height, float _near, float depth, float fieldOfView = 0.7853981f/*PI/4*/ );

	void SetPosition( float, float, float );
	void SetPosition( const D3DXVECTOR3& );
	void SetRotation( float, float, float );
	void SetDirection( const D3DXVECTOR3& );
	void SetDirection( float, float, float );

	D3DXVECTOR3 position( );
	void position( D3DXVECTOR3* );
	D3DXVECTOR3 GetRotation( );

	void Render( );
	void viewMatrix( D3DXMATRIX* );
	D3DXMATRIX viewMatrix();
	void projectionMatrix( D3DXMATRIX* );
	D3DXMATRIX projectionMatrix( );

	void RenderReflection( float heigh );
	void GetReflectionViewMatrix( D3DXMATRIX& );
	void viewDirection( D3DXVECTOR3* );	

	CameraType type();

private:
	CameraType m_type;
	float m_positionX, m_positionY, m_positionZ;
	float m_rotationX, m_rotationY, m_rotationZ;
	D3DXMATRIX m_viewMatrix, m_reflectionViewMatrix;
	D3DXMATRIX m_projection_matrix;
	D3DXVECTOR3 m_view_direction;
};

