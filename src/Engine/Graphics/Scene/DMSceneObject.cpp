#include "DMSceneObject.h"



DMSceneObject::DMSceneObject()
{
	
}

DMSceneObject::DMSceneObject( const DMSceneObject& source ) : DMTransformBuffer( source )
{

}


DMSceneObject::~DMSceneObject()
{
}

void DMSceneObject::update( float time )
{
	D3DXMATRIX matrix = this->resultMatrix();

//	setPosition( matrix._41, matrix._42, matrix._43 );
}

void DMSceneObject::setInFrustum( float lod_range )
{
	
}

void DMSceneObject::setInFrustum( const DMCamera& )
{

}

void DMSceneObject::Render()
{

}
