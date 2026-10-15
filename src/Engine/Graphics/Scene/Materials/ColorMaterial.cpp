#include "ColorMaterial.h"
#include "Shaders\slots.h"

using namespace DirectX;

namespace GS
{

ColorMaterial::ColorMaterial( uint32_t id, const std::string& name ) : Material( id, name )
{
}

bool ColorMaterial::initialize()
{
	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ColorMaterialBuffer ), m_constantBuffer ) )
		return false;

	// Одна фаза на все вызовы: ключ без признаков
	setPermutation( 0, createPhase( 0, 0 ) );
	return permutation( 0 ) >= 0;
}

std::vector<VertexElement> ColorMaterial::initLayouts()
{
	return { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } };
}

void ColorMaterial::setParams( const PropertyContainer& params )
{
	Device::updateResource<ColorMaterialBuffer>( m_constantBuffer, [&]( ColorMaterialBuffer& data )
	{
		const XMFLOAT4* color = params.exists( "Color" ) ? params["Color"].dataPtr<XMFLOAT4>() : nullptr;
		data.objectColor = color ? *color : XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );
	} );

	DMD3D::instance().setConstantBuffer( SLOT_CB_MATERIAL, m_constantBuffer );
}

}
