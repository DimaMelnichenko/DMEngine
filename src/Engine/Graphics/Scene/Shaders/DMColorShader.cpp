#include "DMColorShader.h"
#include "Shaders\slots.h"

namespace GS
{

DMColorShader::DMColorShader()
{

}

DMColorShader::~DMColorShader()
{

}


bool DMColorShader::innerInitialize()
{
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_constantBuffer ) )
		return false;

	return createPhase( 0, 0 ) >= 0;
}

std::vector<VertexElement> DMColorShader::initLayouts()
{
	return { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } };
}

void DMColorShader::setParams( const PropertyContainer& params )
{
	Device::updateResource<XMFLOAT4>( m_constantBuffer, [&]( XMFLOAT4& v )
	{
		const XMFLOAT4* color = params.exists( "Color" ) ? params["Color"].dataPtr<XMFLOAT4>() : nullptr;
		v = color ? *color : XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );
	} );

	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_MATERIAL, m_constantBuffer );
}

}
