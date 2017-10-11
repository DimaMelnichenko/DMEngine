
#include "DMLightDriver.h"


DMLightDriver::DMLightDriver()
{
	
}


DMLightDriver::~DMLightDriver(void)
{	
}

bool DMLightDriver::Initialize()
{	
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( LightBuffer ), m_light_buffer ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( SunCSMBuffer ), m_CSM_buffer ) )
		return false;

	return true;
}

void DMLightDriver::addLight( DMLight& light )
{
	m_light_list.push_back( light );
}

void DMLightDriver::setShaderBuffer( )
{
	if( !m_light_buffer )
		return;

	static LightBuffer lb;
	int light_count = 0;
	int cast_shadow_count = 0;

	for( auto& light : m_light_list )
	{	
		D3DXMATRIX mat = light.resultMatrix();
		lb.cb_lightPos[light_count]	= D3DXVECTOR4( mat._41, mat._42, mat._43, (float)light.type() );
		D3DXVECTOR3 color = light.color();
		lb.cb_lightColor[light_count] = D3DXVECTOR4( color.x, color.y, color.z, light.attenuation );
		D3DXVECTOR3 dir = light.direction();
		lb.cb_lightDir[light_count].x = dir.x;
		lb.cb_lightDir[light_count].y = dir.y;
		lb.cb_lightDir[light_count].z = dir.z;
		lb.cb_lightDir[light_count].w = light.spotAngle();
		++light_count;


		if( cast_shadow_count < 4 && light.castShadow() )
		{
			const DMCamera& shadow_camera = light.shadowCamera();
			shadow_camera.viewMatrix( &(lb.cb_lightView[cast_shadow_count]) );
			D3DXMatrixTranspose( &( lb.cb_lightView[cast_shadow_count] ), &( lb.cb_lightView[cast_shadow_count] ) );
			shadow_camera.projectionMatrix( &( lb.cb_lightProject[cast_shadow_count] ) );
			D3DXMatrixTranspose( &( lb.cb_lightProject[cast_shadow_count] ), &( lb.cb_lightProject[cast_shadow_count] ) );
			lb.cb_shadow_texel[cast_shadow_count] = light.texelSize();			
			
			ID3D11ShaderResourceView* shadow_rt = light.shadow_rt().depthShaderResourceView();
			DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 38 + cast_shadow_count, 1, &shadow_rt );
			if( light.proj_texture )
			{	
				DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 42 + cast_shadow_count, 1, &light.proj_texture );
			}

			++cast_shadow_count;
		}
	}

	if( cast_shadow_count )
		lb.cb_shadow_count = cast_shadow_count;

	if( light_count )
	{
 		lb.cb_light_count = light_count;
	}
	else // default light
	{
		lb.cb_lightPos[0]	= D3DXVECTOR4( 1.0f, 1.0f, -1.0f, 0.0 );
		lb.cb_lightColor[0]	= D3DXVECTOR4( 1.0f, 1.0f, 1.0f, 1000.0f );
		lb.cb_light_count = 1;
	}

	ID3D11Buffer* buffer = m_light_buffer.get();


	D3D11_MAPPED_SUBRESOURCE mappedResource;

	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	LightBuffer* dataPtr = reinterpret_cast<LightBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( dataPtr, &lb, sizeof( LightBuffer ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );

	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 3, 1, &buffer );


	static SunCSMBuffer csm_buffer;

	cast_shadow_count = 0;

	csm_buffer.cb_CSM_count = m_CSM_layers.size();

	for( const DMCamera& camera : m_CSM_layers )
	{
		camera.viewMatrix( &( csm_buffer.cb_CSM_View[cast_shadow_count] ) );
		D3DXMatrixTranspose( &( csm_buffer.cb_CSM_View[cast_shadow_count] ), &( csm_buffer.cb_CSM_View[cast_shadow_count] ) );
		camera.projectionMatrix( &( csm_buffer.cb_CSM_Project[cast_shadow_count] ) );
		D3DXMatrixTranspose( &( csm_buffer.cb_CSM_Project[cast_shadow_count] ), &( csm_buffer.cb_CSM_Project[cast_shadow_count] ) );
		++cast_shadow_count;
	}

	buffer = m_CSM_buffer.get();

	result = DMD3D::instance().GetDeviceContext()->Map( buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	SunCSMBuffer* csm_dataPtr = reinterpret_cast<SunCSMBuffer*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( csm_dataPtr, &csm_buffer, sizeof( SunCSMBuffer ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( buffer, 0 );

	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 4, 1, &buffer );
 }

void DMLightDriver::update( float time )
{
	for( auto& light : m_light_list )
	{
		light.update( time );
	}
}

void DMLightDriver::addCSMLayer( const DMCamera& camera )
{
	m_CSM_layers.push_back( camera );
}

DMLightDriver::LightList& DMLightDriver::lights()
{
	return m_light_list;
}