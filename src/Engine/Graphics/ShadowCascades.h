#pragma once

#include "D3D\GpuResources.h"
#include "D3D\RenderState.h"
#include <string>
#include <DirectXCollision.h>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "RenderView.h"
#include "Light\DMLight.h"
#include "Properties/PropertyContainer.h"
#include "TerrainSunShadow.h"
#include "Shaders\shadow_constants.h"
#include "RenderFeature.h"

namespace GS
{

// Каскадные карты теней солнца — как Cascaded Shadow Maps у directional light в UE. Каскады делят дальность теней
// (Dynamic Shadow Distance) по формуле UE с Cascade Distribution Exponent; каждый — ортографический вид вдоль солнца
// на описанную сферу своей части frustum главного вида. Размер сферы постоянен, а её центр привязан к сетке текселей
// в осях света, поэтому тень не дрожит при движении и повороте камеры. Карта — массив срезов D32, приём тени —
// Shaders/shadows.sh (t104, сэмплер сравнения s8, константы b3). Настройки — у солнца (DMLight::ShadowSettings, колонки
// LevelLights, его окно в GUI «Lights»), размер карты — ShadowMapResolution в settings.ini; в окне «Shadows» —
// только отладочная подкраска каскадов.
// Дальние тени — дальше Dynamic Shadow Distance (как Far Shadow Cascades в UE; DMLight::ShadowSettings::farShadows): лес —
// дальним каскадом (срез карты после четырёх, вид на всю сцену вдоль света, в нём только импостеры расстановки —
// RenderView::farShadow), рельеф — картой края тени по карте высот (TerrainSunShadow). Приём — в той же sunShadow:
// в полосе конца дистанции тень каскадов переходит в дальнюю. Горы вокруг карты (FarTerrain) — своей картой края тени на
// всё кольцо, а их тень на карте — отдельной картой у тени карты: гор нет в каскадах, их тень берут и вблизи
class ShadowCascades : public RenderFeature
{
public:
	static constexpr uint32_t cascadeCount = SHADOW_CASCADE_COUNT;
	static constexpr uint32_t farViewIndex = cascadeCount + 1;	// RenderView::index дальнего каскада

	// resolution — размер среза карты, текселей
	bool initialize( uint32_t resolution );

	// Каскады для главного вида: settings — тени солнца, toSun — направление на него, sceneBounds — границы сцены (по ним
	// ближняя и дальняя плоскости вида света: тень отбрасывает и то, что вне frustum камеры). false — тени выключены
	// или солнце ниже горизонта
	bool update( const RenderView& mainView, const DMLight::ShadowSettings& settings, const DirectX::XMFLOAT3& toSun,
				 const DirectX::BoundingBox& sceneBounds );
	bool active() const { return m_active; }
	const RenderView& cascadeView( uint32_t cascade ) const { return m_views[cascade]; }
	// Дальний каскад есть в этом кадре (тени активны и дальние тени включены у источника)
	bool farActive() const { return m_active && m_settings.farShadows; }
	const RenderView& farView() const { return m_farView; }
	// Тень рельефа дальше каскадов — для источника теней этого кадра (пересчёт — только при его сдвиге или новом рельефе):
	// карта уровня и горы вокруг неё (farTerrain — без гор heightMap пуст). До проходов сцены: её читают туман и все приёмники тени
	void updateTerrainShadow( const TerrainHeight& terrain, const FarTerrainHeight& farTerrain, const DirectX::XMFLOAT3& toLight );
	// Состояние прохода глубины каскадов — часть пайплайнов теней: растеризатор теней (без отсечения граней и по
	// глубине) с наклонным смещением глубины солнца (Shadow Slope Bias, DMLight::ShadowSettings). Из настроек
	// последнего update() или из заданных (прогрев пайплайнов при загрузке — Renderer::warmPipelines)
	RenderState renderState() const { return renderState( m_settings ); }
	static RenderState renderState( const DMLight::ShadowSettings& settings );

	// Карта теней для объявлений проходов, которые её читают
	const ShaderView& shaderView() const { return m_shaderView; }
	// Цель — срез каскада, глубина очищена; дальний — срез cascadeCount
	void beginCascade( uint32_t cascade );
	void beginFarCascade() { beginCascade( cascadeCount ); }
	// После прохода теней, на цели сцены: карта, сэмплер и константы для приёма тени; sunLightIndex — индекс солнца
	// в буфере источников (−1 — без теней). cascades = false — у вида съёмки сцены с другой камеры: каскады подогнаны под
	// главную камеру (каскад выбирается по глубине её взгляда), поэтому только дальние тени — край тени рельефа, тень гор,
	// дальний каскад леса — и облака
	void bindForReceivers( int sunLightIndex, bool cascades = true );

	PropertyContainer* properties() override;

private:

	bool createResources();
	// Дальний каскад: вид вдоль света на всю сцену (её границы поперёк света), центр — на сетке текселей
	void updateFarView( const RenderView& mainView, const DirectX::XMVECTOR& forward, const DirectX::XMVECTOR& right,
						const DirectX::XMVECTOR& up, const DirectX::BoundingBox& sceneBounds, float sceneNear, float sceneFar );

	static constexpr float depthBiasClamp = 0.01f;	// предел наклонного смещения, единицы глубины

	uint32_t m_resolution = 2048;
	bool m_active = false;
	DMLight::ShadowSettings m_settings;	// из последнего update()
	RenderView m_views[cascadeCount];
	float m_splits[cascadeCount] = {};
	float m_texelSize[cascadeCount] = {};
	RenderView m_farView;
	float m_farTexelSize = 0.0f;
	TerrainSunShadow m_terrainShadow;	// карта уровня (и тень гор на ней — отдельно)
	TerrainSunShadow m_mountainShadow;	// всё кольцо гор

	Texture m_texture;
	TargetView m_depthViews[cascadeCount + 1];	// каскады и дальний
	ShaderView m_shaderView;
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
};

}
