#include "ShadowCascades.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

bool ShadowCascades::initialize( uint32_t resolution )
{
	m_resolution = std::max( resolution, 256u );

	// Настройки теней — у солнца; здесь только отладка, как Show → Visualize → Shadow Cascades в UE
	m_properties.setName( "Shadows" );
	m_properties.insert( "Show cascades", false );

	return createResources() && DMD3D::instance().setShadowSlopeBias( m_slopeBias );
}

bool ShadowCascades::createResources()
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = m_resolution;
	textureDesc.Height = m_resolution;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = cascadeCount;
	textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	ID3D11Texture2D* texture = nullptr;
	if( FAILED( device->CreateTexture2D( &textureDesc, nullptr, &texture ) ) )
	{
		LOG( "Shadow map texture is not created" );
		return false;
	}
	m_texture = make_com_ptr<ID3D11Texture2D>( texture );

	for( uint32_t cascade = 0; cascade < cascadeCount; ++cascade )
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC depthDesc = {};
		depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		depthDesc.Texture2DArray.FirstArraySlice = cascade;
		depthDesc.Texture2DArray.ArraySize = 1;
		ID3D11DepthStencilView* depthView = nullptr;
		if( FAILED( device->CreateDepthStencilView( texture, &depthDesc, &depthView ) ) )
			return false;
		m_depthViews[cascade] = make_com_ptr<ID3D11DepthStencilView>( depthView );
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	viewDesc.Texture2DArray.MipLevels = 1;
	viewDesc.Texture2DArray.ArraySize = cascadeCount;
	ID3D11ShaderResourceView* shaderView = nullptr;
	if( FAILED( device->CreateShaderResourceView( texture, &viewDesc, &shaderView ) ) )
		return false;
	m_shaderView = make_com_ptr<ID3D11ShaderResourceView>( shaderView );

	// Сравнение с билинейной выборкой — аппаратный PCF 2×2. Глубина обратная: точка освещена, если она не дальше
	// от света, чем записанная (≥); за краем карты — «освещено»: граница 0 — дальняя плоскость
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1] = samplerDesc.BorderColor[2] = samplerDesc.BorderColor[3] = 0.0f;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	ID3D11SamplerState* sampler = nullptr;
	if( FAILED( device->CreateSamplerState( &samplerDesc, &sampler ) ) )
		return false;
	m_sampler = make_com_ptr<ID3D11SamplerState>( sampler );

	return DMD3D::instance().createShaderConstantBuffer( sizeof( ShaderShadowConstants ), m_constantBuffer );
}

bool ShadowCascades::update( const RenderView& mainView, const DMLight::ShadowSettings& settings, const XMFLOAT3& toSun,
							 const DirectX::BoundingBox& sceneBounds )
{
	m_settings = settings;
	if( settings.shadowSlopeBias != m_slopeBias && DMD3D::instance().setShadowSlopeBias( settings.shadowSlopeBias ) )
		m_slopeBias = settings.shadowSlopeBias;

	m_active = settings.castShadows && toSun.y > 0.0f;
	if( !m_active )
		return false;

	// Границы каскадов по формуле UE: dᵢ = D · (Eⁱ − 1) / (Eᴺ − 1); при E = 1 — равномерно
	const float distance = settings.dynamicShadowDistance;
	const float exponent = settings.cascadeDistributionExponent;
	for( uint32_t cascade = 0; cascade < cascadeCount; ++cascade )
	{
		const float i = static_cast<float>( cascade + 1 );
		m_splits[cascade] = exponent > 1.001f ?
			distance * ( std::pow( exponent, i ) - 1.0f ) / ( std::pow( exponent, static_cast<float>( cascadeCount ) ) - 1.0f ) :
			distance * i / cascadeCount;
	}

	// Угол обзора — из проекции главного вида (LH: _11 = ctg(x/2), _22 = ctg(y/2)), ближняя плоскость — из вида
	XMFLOAT4X4 projection;
	XMStoreFloat4x4( &projection, mainView.projection );
	const float tanX = 1.0f / projection._11;
	const float tanY = 1.0f / projection._22;
	const float cornerK2 = tanX * tanX + tanY * tanY;
	const float nearPlane = mainView.nearPlane;

	// Базис света: взгляд против направления на солнце, «верх» — по +Z (при солнце у этой оси — по +X); мировой верх
	// не годится — при солнце в зените базис вырождается
	const XMVECTOR forward = XMVector3Normalize( -XMLoadFloat3( &toSun ) );
	const XMVECTOR upHint = std::fabs( XMVectorGetZ( forward ) ) > 0.99f ? XMVectorSet( 1.0f, 0.0f, 0.0f, 0.0f ) :
																		   XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f );
	const XMVECTOR right = XMVector3Normalize( XMVector3Cross( upHint, forward ) );
	const XMVECTOR up = XMVector3Cross( forward, right );

	// Границы сцены вдоль взгляда света: ближняя и дальняя плоскости охватывают всё, что может отбросить тень
	XMFLOAT3 corners[DirectX::BoundingBox::CORNER_COUNT];
	sceneBounds.GetCorners( corners );
	float sceneNear = FLT_MAX;
	float sceneFar = -FLT_MAX;
	for( const XMFLOAT3& corner : corners )
	{
		const float depth = XMVectorGetX( XMVector3Dot( XMLoadFloat3( &corner ), forward ) );
		sceneNear = std::min( sceneNear, depth );
		sceneFar = std::max( sceneFar, depth );
	}

	const XMVECTOR cameraPosition = XMLoadFloat3( &mainView.position );
	const XMVECTOR cameraDirection = XMVector3Normalize( XMLoadFloat3( &mainView.direction ) );
	const float resolution = static_cast<float>( m_resolution );
	for( uint32_t cascade = 0; cascade < cascadeCount; ++cascade )
	{
		const float n = cascade == 0 ? nearPlane : m_splits[cascade - 1];
		const float f = m_splits[cascade];

		// Описанная сфера части frustum [n, f]: центр на оси взгляда, радиус — до дальнего (и ближнего) угла.
		// Зависит только от n, f и угла обзора — постоянна при движении камеры
		const float center = std::min( ( n + f ) * 0.5f * ( 1.0f + cornerK2 ), f );
		float radius = std::max( std::sqrt( ( f - center ) * ( f - center ) + f * f * cornerK2 ),
								 std::sqrt( ( center - n ) * ( center - n ) + n * n * cornerK2 ) );
		// Запас на ядро PCF и смещение по нормали (8 текселей), округление вверх до 1/16 м
		radius = std::ceil( radius * resolution / ( resolution - 8.0f ) * 16.0f ) / 16.0f;

		// Центр привязан к сетке текселей в осях света, отсчитанной от начала мира
		const float texel = 2.0f * radius / resolution;
		const XMVECTOR sphereCenter = cameraPosition + cameraDirection * center;
		const float x = std::floor( XMVectorGetX( XMVector3Dot( sphereCenter, right ) ) / texel ) * texel;
		const float y = std::floor( XMVectorGetX( XMVector3Dot( sphereCenter, up ) ) / texel ) * texel;
		const float z = XMVectorGetX( XMVector3Dot( sphereCenter, forward ) );
		const XMVECTOR snapped = right * x + up * y + forward * z;

		const float depthNear = std::min( sceneNear, z - radius ) - 1.0f;
		const float depthFar = std::max( sceneFar, z + radius ) + 1.0f;
		const XMVECTOR eye = snapped + forward * ( depthNear - z );

		RenderView& view = m_views[cascade];
		view.view = XMMatrixLookToLH( eye, forward, up );
		// Обратная глубина, как у камеры: 1 у ближней к свету плоскости, 0 у дальней
		view.projection = XMMatrixOrthographicLH( 2.0f * radius, 2.0f * radius, depthFar - depthNear, 0.0f );
		view.viewProjection = XMMatrixMultiply( view.view, view.projection );
		view.viewInverse = XMMatrixInverse( nullptr, view.view );
		XMStoreFloat3( &view.position, eye );
		XMStoreFloat3( &view.direction, forward );
		view.lodOrigin = mainView.lodOrigin;	// LOD и морфинг — как у главного вида
		view.nearPlane = 0.0f;
		view.farPlane = depthFar - depthNear;
		view.frustum = DMFrustum( view.viewProjection );
		view.index = cascade + 1;
		view.cascadeNear = cascade == 0 ? 0.0f : n;
		view.cascadeFar = f * std::sqrt( 1.0f + cornerK2 );

		m_texelSize[cascade] = texel;
	}

	return true;
}

void ShadowCascades::unbindShadowMap()
{
	ID3D11ShaderResourceView* none = nullptr;
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( SLOT_SHADOW_MAP, 1, &none );
}

void ShadowCascades::beginCascade( uint32_t cascade )
{
	DMD3D::instance().setDepthTarget( m_depthViews[cascade].get(), m_resolution, m_resolution );
	// Обратная глубина: «пусто» — 0, дальше всего от света
	DMD3D::instance().GetDeviceContext()->ClearDepthStencilView( m_depthViews[cascade].get(), D3D11_CLEAR_DEPTH, 0.0f, 0 );
}

void ShadowCascades::bindForReceivers( int sunLightIndex )
{
	const float distance = m_settings.dynamicShadowDistance;
	const float fadeout = m_settings.shadowDistanceFadeoutFraction;

	ShaderShadowConstants constants = {};
	for( uint32_t cascade = 0; cascade < cascadeCount; ++cascade )
		constants.cascadeViewProjection[cascade] = XMMatrixTranspose( m_views[cascade].viewProjection );
	constants.cascadeSplits = XMFLOAT4( m_splits[0], m_splits[1], m_splits[2], m_splits[3] );
	constants.cascadeTexelSize = XMFLOAT4( m_texelSize[0], m_texelSize[1], m_texelSize[2], m_texelSize[3] );
	constants.cascadeTransition = m_settings.cascadeTransitionFraction;
	constants.fadeStart = distance * ( 1.0f - fadeout );
	constants.shadowDistance = distance;
	constants.normalBias = m_settings.normalBias;
	constants.sunLightIndex = m_active ? sunLightIndex : -1;
	constants.showCascades = m_properties["Show cascades"].data<bool>() ? 1 : 0;
	constants.mapSize = static_cast<float>( m_resolution );
	constants.depthBias = m_settings.shadowBias;
	Device::updateResourceData<ShaderShadowConstants>( m_constantBuffer.get(), constants );

	DMD3D& d3d = DMD3D::instance();
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_SHADOW, m_constantBuffer );
	d3d.setSRV( SRVType::ps, SLOT_SHADOW_MAP, m_shaderView );
	ID3D11SamplerState* sampler = m_sampler.get();
	d3d.GetDeviceContext()->PSSetSamplers( SLOT_SAMPLER_SHADOW, 1, &sampler );
}

PropertyContainer* ShadowCascades::properties()
{
	return &m_properties;
}

}
