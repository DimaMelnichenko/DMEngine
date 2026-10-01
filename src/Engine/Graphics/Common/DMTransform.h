#pragma once

#include "DirectX.h"

// Положение, поворот и масштаб объекта в мире — как FTransform в UE и узел glTF (translation, rotation, scale).
// Мировая матрица = масштаб, затем поворот, затем сдвиг; пересчитывается при изменении
class DMTransform
{
public:
	DMTransform();

	void setPosition( const DirectX::XMFLOAT3& );
	const DirectX::XMFLOAT3& position() const;

	// Кватернион x, y, z, w; хранится нормированным
	void setRotation( const DirectX::XMFLOAT4& );
	const DirectX::XMFLOAT4& rotation() const;

	void setScale( const DirectX::XMFLOAT3& );
	const DirectX::XMFLOAT3& scale() const;

	const DirectX::XMMATRIX& worldMatrix() const;

private:
	void recalcMatrix();

private:
	DirectX::XMMATRIX m_worldMatrix;
	DirectX::XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4 m_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	DirectX::XMFLOAT3 m_scale = { 1.0f, 1.0f, 1.0f };
};
