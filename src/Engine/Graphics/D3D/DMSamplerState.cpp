#include "DMSamplerState.h"



DMSamplerState::DMSamplerState( DMD3D* dmd3d ) : DM3DObject( dmd3d )
{	
}


DMSamplerState::~DMSamplerState()
{
}

bool DMSamplerState::initialize()
{
	if( m_sampler_states.size() )
		return true;

	ID3D11SamplerState* sampler_state;
	HRESULT result;

	D3D11_SAMPLER_DESC sampler_desc;
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );


	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );


	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 8;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );


	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	sampler_desc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 8;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampler_desc.BorderColor[0] = 0;
	sampler_desc.BorderColor[1] = 0;
	sampler_desc.BorderColor[2] = 0;
	sampler_desc.BorderColor[3] = 0;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	/////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////
	memset( &sampler_desc, 0, sizeof( D3D11_SAMPLER_DESC ) );
	//sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 0;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS;
	sampler_desc.BorderColor[0] = 1.0f;
	sampler_desc.BorderColor[1] = 1.0f;
	sampler_desc.BorderColor[2] = 1.0f;
	sampler_desc.BorderColor[3] = 1.0f;
	sampler_desc.MinLOD = 0;
	sampler_desc.MaxLOD = 0;


	result = m_dmd3d->GetDevice()->CreateSamplerState( &sampler_desc, &sampler_state );
	if( FAILED( result ) )
	{
		return false;
	}

	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler_state ) );

	return true;
}

ID3D11SamplerState* DMSamplerState::sampler( SamplerType type )
{
	return m_sampler_states[type].get();
}