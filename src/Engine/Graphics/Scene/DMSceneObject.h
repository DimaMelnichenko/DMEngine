#pragma once

#include "..\Common\DMTransformBuffer.h"

#include "..\Common\DMAABB.h"
#include "Camera\DMCamera.h"

class DMSceneObject : public DMTransformBuffer
{
public:
	DMSceneObject();
	DMSceneObject( const DMSceneObject& );
	DMSceneObject( const DMSceneObject* );
	~DMSceneObject();
	virtual void Render();

	void update( float );

	virtual void setInFrustum( float );
	virtual void setInFrustum( const DMCamera& );

protected:	
	bool m_in_frustum;


};

