#pragma once

#include <string>
#include <DirectXCollision.h>
#include "DirectX.h"
#include "DM3DUtils.h"
#include "Utils\utilites.h"
#include "RenderView.h"
#include "Light\DMLight.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Каскадные карты теней солнца — как Cascaded Shadow Maps у directional light в UE. Каскады делят дальность теней
// (Dynamic Shadow Distance) по формуле UE с Cascade Distribution Exponent; каждый — ортографический вид вдоль солнца
// на описанную сферу своей части frustum главного вида. Размер сферы постоянен, а её центр привязан к сетке текселей
// в осях света, поэтому тень не дрожит при движении и повороте камеры. Карта — массив срезов D32, приём тени —
// Shaders/shadows.sh (t104, сэмплер сравнения s8, константы b3). Настройки — у солнца (DMLight::ShadowSettings, колонки
// LevelLights, его окно в GUI «Lights»), размер карты — ShadowMapResolution в settings.ini; в окне «Shadows» —
// только отладочная подкраска каскадов
class ShadowCascades
{
public:
	static constexpr uint32_t cascadeCount = 4;

	// resolution — размер среза карты, текселей
	bool initialize( uint32_t resolution );

	// Каскады для главного вида: settings — тени солнца, toSun — направление на него, sceneBounds — границы сцены (по ним
	// ближняя и дальняя плоскости вида света: тень отбрасывает и то, что вне frustum камеры). false — тени выключены
	// или солнце ниже горизонта
	bool update( const RenderView& mainView, const DMLight::ShadowSettings& settings, const XMFLOAT3& toSun,
				 const DirectX::BoundingBox& sceneBounds );
	bool active() const { return m_active; }
	const RenderView& cascadeView( uint32_t cascade ) const { return m_views[cascade]; }

	// Перед рисованием в карту: отвязать её от пиксельных шейдеров (иначе D3D11 отвяжет цель сам)
	void unbindShadowMap();
	// Цель — срез каскада, глубина очищена
	void beginCascade( uint32_t cascade );
	// После прохода теней, на цели сцены: карта, сэмплер и константы для приёма тени; sunLightIndex — индекс солнца
	// в буфере источников (−1 — без теней)
	void bindForReceivers( int sunLightIndex );

	PropertyContainer* properties();

private:
	// Раскладка — cbuffer ShadowConstants в Shaders/shadows.sh
	struct alignas( 16 ) ShaderShadowConstants
	{
		XMMATRIX cascadeViewProjection[cascadeCount];
		XMFLOAT4 cascadeSplits;		// дальняя граница каскада по глубине взгляда, м
		XMFLOAT4 cascadeTexelSize;	// размер текселя каскада в мире, м
		float cascadeTransition;
		float fadeStart;
		float shadowDistance;
		float normalBias;
		int32_t sunLightIndex;
		int32_t showCascades;
		float mapSize;
		float depthBias;
	};
	static_assert( sizeof( ShaderShadowConstants ) == 320, "ShaderShadowConstants must match cbuffer ShadowConstants in Shaders/shadows.sh" );

	bool createResources();

	uint32_t m_resolution = 2048;
	bool m_active = false;
	DMLight::ShadowSettings m_settings;	// из последнего update()
	float m_slopeBias = 2.0f;			// наклонное смещение, с которым создан растеризатор теней
	RenderView m_views[cascadeCount];
	float m_splits[cascadeCount] = {};
	float m_texelSize[cascadeCount] = {};

	com_unique_ptr<ID3D11Texture2D> m_texture;
	com_unique_ptr<ID3D11DepthStencilView> m_depthViews[cascadeCount];
	com_unique_ptr<ID3D11ShaderResourceView> m_shaderView;
	com_unique_ptr<ID3D11SamplerState> m_sampler;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	PropertyContainer m_properties;
};

}
