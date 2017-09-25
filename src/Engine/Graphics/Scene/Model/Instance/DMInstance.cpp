#include "DMInstance.h"


DMInstance::DMInstance( DMD3D* dmd3d ) : DM3DObject( dmd3d )
{

}

DMInstance::~DMInstance()
{

}

DMModel* DMInstance::model()
{
	return m_model.get();
}

bool DMInstance::initialize( DMModel&& model, INSTANCE_TYPE type, unsigned int max_count )
{
	std::unique_ptr<DMModel> model_ptr( &model );
	return initialize( model_ptr, type, max_count );
}

bool DMInstance::initialize( std::unique_ptr<DMModel>& model, INSTANCE_TYPE type, unsigned int max_count )
{
	m_model = std::move( model );

	m_max_count = max_count;
	m_current_count = 0;

	m_vsStrcturedBuffer = std::unique_ptr<DMStructuredBuffer>( new DMStructuredBuffer( m_dmd3d ) );
	m_vsStrcturedBuffer->CreateBuffer( sizeof( VSInstanceDataType ), m_max_count );


	std::vector<PSInstanceDataType> data;
	for( int i = 0; i < 7; ++i )
	{	
		float offset_gloss = i * ( 1.0f / 7.0f ) + ( 1.0f / 7.0f );
		PSInstanceDataType item;
		item.albedo = D3DXVECTOR4( 0.856, 0.168, 0.0, 1.0f );
		item.fresnel_gloss = D3DXVECTOR4( 0.04f, 0.04, 0.04, offset_gloss );
		data.push_back( item );
	}

	m_psStrcturedBuffer = std::unique_ptr<DMStructuredBuffer>( new DMStructuredBuffer( m_dmd3d ) );
	m_psStrcturedBuffer->CreateBuffer( sizeof( PSInstanceDataType ), data.size() );
	m_psStrcturedBuffer->UpdateData( &data[0], sizeof( PSInstanceDataType ) * data.size() );

	return true;
}

unsigned int DMInstance::count()
{
	return m_current_count;
}

void DMInstance::set_data( const std::vector<DMInstance::VSInstanceDataType>& new_data )
{
	m_current_count = new_data.size();
	m_vsStrcturedBuffer->UpdateData( (void*)&new_data[0], new_data.size() * sizeof( VSInstanceDataType ) );
}

void DMInstance::set_to_draw()
{
	m_vsStrcturedBuffer->setToSlot( 16, SRVType::vs );
	m_psStrcturedBuffer->setToSlot( 16, SRVType::ps );
}