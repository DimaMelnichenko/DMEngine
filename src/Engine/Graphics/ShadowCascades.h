#pragma once

#include "D3D\GpuResources.h"
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

	// Карта теней для объявлений проходов, которые её читают
	const ShaderView& shaderView() const { return m_shaderView; }
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

	Texture m_texture;
	TargetView m_depthViews[cascadeCount];
	ShaderView m_shaderView;
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
};

}
