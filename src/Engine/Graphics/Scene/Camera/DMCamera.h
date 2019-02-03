#pragma once

//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include "Properties/PropertyContainer.h"


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
	void SetPosition( const XMFLOAT3& );
	void SetRotation( float, float, float );
	void SetDirection( const XMFLOAT3& );
	void SetDirection( float, float, float );
	void Update( float elapsedTime, bool cursorMode = false );
	void RenderReflection( float heigh );

	const XMFLOAT3& position( ) const;
	void position( XMFLOAT3* ) const;
	void position( XMVECTOR& ) const;
	XMFLOAT3 GetRotation( ) const;
	
	void viewMatrix( XMMATRIX* ) const;
	const XMMATRIX& viewMatrix() const;

	void projectionMatrix( XMMATRIX* ) const;
	const XMMATRIX& projectionMatrix( ) const;
	
	void GetReflectionViewMatrix( XMMATRIX& ) const;
	void viewDirection( XMFLOAT3* ) const;	

	CameraType type() const;

	PropertyContainer m_properties;

private:
	void readKeyboard( XMFLOAT3& );
private:
	CameraType m_type;
	XMFLOAT3 m_Eye;
	XMVECTOR m_eyeVector;
	float m_rotationX, m_rotationY, m_rotationZ;
	XMMATRIX m_viewMatrix, m_reflectionViewMatrix;
	XMMATRIX m_projection_matrix;
	XMFLOAT3 m_view_direction;
	XMMATRIX m_mCameraWorld;

	
};

