#include "DMSceneObject.h"



DMSceneObject::DMSceneObject( DMD3D* d3d ) : DM3DObject( d3d )
{
	
}

DMSceneObject::DMSceneObject( const DMSceneObject& source ) : DMTransformBuffer( source ), DM3DObject( source.m_dmd3d )
{

}

DMSceneObject::DMSceneObject( const DMSceneObject* source ) : DMTransformBuffer( source ), DM3DObject( source->m_dmd3d )
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

void DMSceneObject::setInFrustum( DMCamera* )
{

}

void DMSceneObject::Render()
{

}
