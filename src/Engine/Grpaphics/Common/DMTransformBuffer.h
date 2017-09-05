#pragma once

#include <d3d11.h>
#include <d3dx11.h>
#include <d3dx10math.h>
#include "DMAABB.h"
#include "DMObject.h"
#include <vector>
#include <memory>


class DMTransformBuffer : public DMObject
{
	

public:
	DMTransformBuffer(  );
	~DMTransformBuffer(void);

	DMTransformBuffer( const DMTransformBuffer* );
	DMTransformBuffer( const DMTransformBuffer& );
	

	void setPosition( const D3DXVECTOR3& );
	void setPosition( float, float, float );
	void position( D3DXVECTOR3* );
	D3DXVECTOR3 position();
	void setRotationAxis( const D3DXVECTOR3&, float degree );	
	void setRotationAxis( float x, float y, float z, float degree );
	void setScale( const D3DXVECTOR3& );
	void setScale( float );
	void setLink( DMTransformBuffer* );
	void unlink();

	void resultMatrix( D3DXMATRIX* );
	D3DXMATRIX resultMatrix();

	DMTransformBuffer& operator=(const DMTransformBuffer& right);	

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

