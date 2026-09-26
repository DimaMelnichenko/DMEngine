#pragma once

#include "DirectX.h"
#include "DMAABB.h"
#include <vector>
#include <memory>


class DMTransformBuffer
{
	

public:
	DMTransformBuffer();	
	DMTransformBuffer( const DMTransformBuffer& );
	DMTransformBuffer( DMTransformBuffer&& );
	~DMTransformBuffer(void);

	DMTransformBuffer& operator=( const DMTransformBuffer& right );
	DMTransformBuffer& operator=( DMTransformBuffer&& );
	
public:
	void setPosition( const XMFLOAT3& );
	void setPosition( float, float, float );
	XMVECTOR position() const;
	const XMFLOAT3& posf3() const;
	void setScale( const XMFLOAT3& );
	const XMFLOAT3& scale() const;
	const XMMATRIX& resultMatrix() const;
	const XMMATRIX* resultMatrixPtr() const;

private:
	void recalcMatrix();

private:
	XMMATRIX m_result_matrix;
	XMMATRIX m_position_matrix;
	XMMATRIX m_rotate_matrix;
	XMMATRIX m_scale_matrix;

	DMTransformBuffer* m_transform_link;
	XMFLOAT3 m_position;
	XMFLOAT3 m_scale;

	DMAABB m_aabb;
};

