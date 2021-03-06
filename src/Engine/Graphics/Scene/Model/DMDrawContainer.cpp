#include "DMDrawContainer.h"



DMDrawContainer::DMDrawContainer( DMShader* shader ) : m_shader( shader )
{
	
}

DMDrawContainer::~DMDrawContainer()
{

}

void DMDrawContainer::setShader( DMShader* shader )
{
	m_shader = shader;
}

void DMDrawContainer::addModel( DMModel* model )
{
	m_models.push_back( model );
}

DMShader* DMDrawContainer::shader() const
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