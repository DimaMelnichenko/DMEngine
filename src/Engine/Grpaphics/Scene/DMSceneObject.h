#pragma once

#include "DMTransformBuffer.h"
#include "DM3DObject.h"
#include "DMAABB.h"
#include "DMD3D.h"
#include "utilites.h"
#include "DMCamera.h"

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

