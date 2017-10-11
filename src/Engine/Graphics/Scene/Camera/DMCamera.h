#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"


class DMCamera
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
	void Render();
	void RenderReflection( float heigh );

	D3DXVECTOR3 position( ) const;
	void position( D3DXVECTOR3* ) const;
	D3DXVECTOR3 GetRotation( ) const;
	
	void viewMatrix( D3DXMATRIX* ) const;
	D3DXMATRIX viewMatrix() const;

	void projectionMatrix( D3DXMATRIX* ) const;
	D3DXMATRIX projectionMatrix( ) const;
	
	void GetReflectionViewMatrix( D3DXMATRIX& ) const;
	void viewDirection( D3DXVECTOR3* ) const;	

	CameraType type() const;

private:
	CameraType m_type;
	float m_positionX, m_positionY, m_positionZ;
	float m_rotationX, m_rotationY, m_rotationZ;
	D3DXMATRIX m_viewMatrix, m_reflectionViewMatrix;
	D3DXMATRIX m_projection_matrix;
	D3DXVECTOR3 m_view_direction;
};

