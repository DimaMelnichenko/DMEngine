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
	void setPosition( const D3DXVECTOR3& );
	void setPosition( float, float, float );
	void position( D3DXVECTOR3* ) const;
	D3DXVECTOR3 position() const;
	void setRotationAxis( const D3DXVECTOR3&, float degree );	
	void setRotationAxis( float x, float y, float z, float degree );
	void setScale( const D3DXVECTOR3& );
	void setScale( float );
	void setLink( DMTransformBuffer* );
	void unlink();

	void resultMatrix( D3DXMATRIX* );
	const D3DXMATRIX& resultMatrix() const;
	const D3DXMATRIX* resultMatrixPtr() const;
	DMAABB& aabb();

private:
	void recalcMatrix();

private:
	D3DXMATRIX m_result_matrix;
	D3DXMATRIX m_position_matrix;
	D3DXMATRIX m_rotate_matrix;
	D3DXMATRIX m_scale_matrix;

	DMTransformBuffer* m_transform_link;

	DMAABB m_aabb;
};

