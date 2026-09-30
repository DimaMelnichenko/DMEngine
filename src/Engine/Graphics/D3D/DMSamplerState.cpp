#include "DMSamplerState.h"
#include "D3D\DMD3D.h"

DMSamplerState::DMSamplerState()
{
}

DMSamplerState::~DMSamplerState()
{
}

bool DMSamplerState::initialize()
{
	if( m_sampler_states.size() )
		return true;

	// Слот за слотом: фильтр и адресация (s0…s7 — как SamplerState в Shaders/samplers.sh)
	struct Description
	{
		D3D11_FILTER filter;
		D3D11_TEXTURE_ADDRESS_MODE address;
	};
	const Description descriptions[shadowCompare] = {
		{ D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP },
		{ D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP },
		{ D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER },
		{ D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP },
		{ D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP },
		{ D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_BORDER },
		{ D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_CLAMP },
		{ D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP },
	};

	ID3D11Device* device = DMD3D::instance().GetDevice();
	for( const Description& description : descriptions )
	{
		D3D11_SAMPLER_DESC desc = {};
		desc.Filter = description.filter;
		desc.AddressU = desc.AddressV = desc.AddressW = description.address;
		desc.MaxAnisotropy = description.filter == D3D11_FILTER_ANISOTROPIC ? 8 : 0;
		desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		ID3D11SamplerState* sampler = nullptr;
		if( FAILED( device->CreateSamplerState( &desc, &sampler ) ) )
			return false;
		m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler ) );
	}

	// Карта теней (Shaders/shadows.sh): линейный PCF с сравнением — «освещено», если глубина пикселя не дальше
	// от света, чем записанная (≥, глубина обратная); за краем карты — «освещено»: граница 0 — дальняя плоскость
	D3D11_SAMPLER_DESC shadow = {};
	shadow.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadow.AddressU = shadow.AddressV = shadow.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	shadow.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL;
	shadow.MaxLOD = D3D11_FLOAT32_MAX;
	ID3D11SamplerState* sampler = nullptr;
	if( FAILED( device->CreateSamplerState( &shadow, &sampler ) ) )
		return false;
	m_sampler_states.push_back( make_com_ptr<ID3D11SamplerState>( sampler ) );

	for( auto& state : m_sampler_states )
		m_samplerPointers.push_back( state.get() );

	return true;
}

void DMSamplerState::setDefaultSamplers()
{
	ID3D11DeviceContext* context = DMD3D::instance().GetDeviceContext();
	// Сэмплер теней читают только пиксельные шейдеры
	context->CSSetSamplers( 0, shadowCompare, &m_samplerPointers[0] );
	context->VSSetSamplers( 0, shadowCompare, &m_samplerPointers[0] );
	context->HSSetSamplers( 0, shadowCompare, &m_samplerPointers[0] );
	context->DSSetSamplers( 0, shadowCompare, &m_samplerPointers[0] );
	context->GSSetSamplers( 0, shadowCompare, &m_samplerPointers[0] );
	context->PSSetSamplers( 0, count, &m_samplerPointers[0] );
}
