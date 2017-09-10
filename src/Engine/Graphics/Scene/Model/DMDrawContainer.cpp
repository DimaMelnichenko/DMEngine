#include "DMDrawContainer.h"



DMDrawContainer::DMDrawContainer( DMShader* shader )
{
	m_shader = shader;
}


DMDrawContainer::~DMDrawContainer()
{

}

void DMDrawContainer::addModel( DMModel* model )
{
	m_models.push_back( model );
}

DMShader* DMDrawContainer::shader()
{
	return m_shader;
}

void DMDrawContainer::update( float time )
{
	for( DMModel* model : m_models )
	{
		model->update( time );
	}
}