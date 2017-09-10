#pragma once

#include "..\Common\DMTransformBuffer.h"
#include "..\Common\DM3DObject.h"
#include "..\Common\DMAABB.h"
#include "Camera\DMCamera.h"

class DMSceneObject : public DM3DObject, public DMTransformBuffer
{
public:
	DMSceneObject( DMD3D* );
	DMSceneObject( const DMSceneObject& );
	DMSceneObject( const DMSceneObject* );
	~DMSceneObject();
	virtual void Render();

	void update( float );

	virtual void setInFrustum( float );
	virtual void setInFrustum( DMCamera* );

protected:	
	bool m_in_frustum;


};

