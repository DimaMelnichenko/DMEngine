#pragma once

#include "DirectX.h"

// Положение, поворот и масштаб объекта в мире — как FTransform в UE и узел glTF (translation, rotation, scale).
// Мировая матрица = масштаб, затем поворот, затем сдвиг; пересчитывается при изменении
class DMTransform
{
public:
	DMTransform();

	void setPosition( const XMFLOAT3& );
	const XMFLOAT3& position() const;

	// Кватернион x, y, z, w; хранится нормированным
	void setRotation( const XMFLOAT4& );
	const XMFLOAT4& rotation() const;

	void setScale( const XMFLOAT3& );
	const XMFLOAT3& scale() const;

	const XMMATRIX& worldMatrix() const;

private:
	void recalcMatrix();

private:
	XMMATRIX m_worldMatrix;
	XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4 m_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT3 m_scale = { 1.0f, 1.0f, 1.0f };
};
