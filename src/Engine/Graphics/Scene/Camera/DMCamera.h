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
	// Новый размер кадра: проекция под его соотношение сторон (поле зрения и плоскости — прежние)
	void setViewport( float width, float height );

	void SetPosition( float, float, float );
	void SetRotation( float, float, float );
	// Положение и поворот ровно такие (тангаж и рыскание в градусах), с учётом накопленного поворота мышью
	void setView( const XMFLOAT3& position, float pitch, float yaw );
	// Тангаж и рыскание в градусах вместе с поворотом мышью — как у камеры в кадре
	XMFLOAT2 rotation() const;
	void Update( float elapsedTime, bool cursorMode = false );

	const XMFLOAT3& position( ) const;
	void position( XMFLOAT3* ) const;
	void position( XMVECTOR& ) const;
	
	void viewMatrix( XMMATRIX* ) const;

	// Проекция с обратной глубиной (Reversed-Z): 1 у ближней плоскости, 0 у дальней
	void projectionMatrix( XMMATRIX* ) const;
	float nearPlane() const { return m_nearPlane; }
	float farPlane() const { return m_farPlane; }
	void viewDirection( XMFLOAT3* ) const;	

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
	float m_nearPlane = 0.1f;
	float m_farPlane = 1000.0f;
	float m_fieldOfView = 0.7853981f;
	XMFLOAT3 m_view_direction;
	XMMATRIX m_mCameraWorld;
	// Последнее положение мыши: в режиме курсора (I) камера сохраняет поворот
	double m_mouseX = 0.0;
	double m_mouseY = 0.0;
};

