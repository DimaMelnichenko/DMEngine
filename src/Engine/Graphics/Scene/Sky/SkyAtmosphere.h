#pragma once

#include <string>
#include <vector>
#include "SceneObject.h"
#include "SkyLight.h"
#include "Shaders\FullscreenShader.h"
#include "Shaders\DMComputeShader.h"
#include "D3D\CubeTarget.h"
#include "D3D\RenderTarget.h"
#include "Properties\PropertyContainer.h"
#include "TextureObjects\DMTexture.h"

class DMLightDriver;

namespace GS
{

// Процедурное небо, освещение окружением и воздушная перспектива от него (как Sky Atmosphere в UE5, таблицы
// Hillaire 2020; Рэлей, Ми, озон, многократное рассеяние — Shaders/atmosphere.sh). compute():
// - при смене атмосферы (дымка, альбедо земли) — таблица пропускания до края атмосферы
//   (Shaders/sky_transmittance.ps) и таблица многократного рассеяния Ψ (Shaders/sky_multiscattering.ps);
// - каждый кадр — небо вокруг камеры, таблица Sky-View (Shaders/sky_view.ps), и объём воздушной перспективы над экраном
//   главного вида (Shaders/aerial_perspective.cs, Camera Aerial Perspective Volume в UE: свет, рассеянный воздухом
//   между камерой и точкой, и пропускание до неё; его читает общая функция освещения — Shaders/aerial_perspective.sh,
//   слот PS t106);
// - при смене солнца (первый включённый направленный источник уровня) или настроек — cubemap неба из Sky-View
//   (Shaders/sky_cube.ps) с мипами для SkyLight: гармоники рассеянного света и префильтр отражений (Shaders/ibl.sh,
//   слоты PS t101…t103) — по шагу за кадр, как Real Time Capture у Sky Light в UE.
// Свой вызов в проходе sky рисует небо фоном кадра — из Sky-View (Shaders/sky_background.ps). Пропускание к солнцу для его света у земли
// (Atmosphere Sun Light) считает на CPU по той же модели — sunTransmittance. Настройки — строка таблицы SkyAtmosphere,
// на которую ссылается уровень (Levels.atmosphere), и окно GUI «Sky atmosphere»
class SkyAtmosphere : public SceneObject
{
public:
	static constexpr uint32_t multipleScatteringSize = 32;

	// Строка SkyAtmosphere; без неё — значения по умолчанию
	struct Settings
	{
		float skyIntensity = 1.0f;	// множитель рассеянного света неба, 1 — по модели
		float haze = 1.0f;			// плотность дымки (аэрозоли Ми)
		float groundAlbedo = 0.25f;	// отражение земли под горизонтом
		// Во сколько раз воздух между камерой и точкой кажется толще (Aerial Perspective View Distance Scale в UE):
		// 1 — по модели (дымка на 1 км — несколько процентов), больше — небольшой мир выглядит как большой, 0 — выключено
		float aerialPerspectiveViewDistanceScale = 1.0f;
		// Свечение ночного неба в зените, кд/м²: собственное свечение атмосферы (airglow), суммарный свет звёзд и
		// зодиакальный свет — ~2·10⁻⁴ на тёмном небе; к горизонту ярче. Видно, когда солнце и луна не светят
		float nightSkyLuminance = 2e-4f;
	};

	// Имя карты луны в таблице Textures (NASA CGI Moon Kit); нет — ровный диск
	static constexpr const char* moonAlbedoTexture = "moon_albedo";

	SkyAtmosphere();

	// skyLight — освещение окружением сцены: атмосфера отдаёт в него cubemap неба
	bool initialize( const DMLightDriver& lights, const Settings& settings, SkyLight& skyLight );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Фон не рисуется, если у уровня своя модель неба (SkySphere); освещение окружением остаётся
	void setBackgroundVisible( bool visible );
	// Пропускание атмосферы от наблюдателя к солнцу (toSun — нормированное направление на него): доля света солнца
	// над атмосферой, которая доходит до земли, по каналам RGB. Как transmittanceToTop в Shaders/atmosphere.sh
	// с дымкой из GUI; солнце под горизонтом — 0. Как GetTransmittanceAtGroundLevel в UE — на CPU, микросекунды
	XMFLOAT3 sunTransmittance( const XMFLOAT3& toSun ) const;
	// Нормировка того, что в половинной точности (объём воздушной перспективы, результаты SkyLight): оценка яркости неба
	// в единицах запекания — 1 днём, в сумерках падает с высотой солнца, ночью — свет луны и ночное свечение. Там
	// хранится свет, делённый на неё, шейдеры умножают на масштаб с ней (cb_aerialPerspectiveScale, cb_skyLightScale):
	// ночью значения ~10⁻⁷…10⁻⁹ не пропадают
	float skyNormalization() const;

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override;

private:
	// Константный буфер PS b2, раскладка как у SkyParameters в Shaders/atmosphere.sh
	struct alignas( 16 ) Parameters
	{
		XMFLOAT3 sunDirection;
		float skyIntensity;
		XMFLOAT3 sunColor;
		float haze;
		XMFLOAT3 groundAlbedo;
		int32_t face;
		XMFLOAT3 moonDirection;
		float nightSkyLuminance;	// в единицах запекания: кд/м², делённые на освещённость от солнца
		XMFLOAT3 moonColor;			// в единицах запекания; 0 — луны нет или она ниже −10° (её свет в небе — 0)
		int32_t skyViewLight;		// проход Sky-View: 0 — таблица солнца, 1 — луны
	};

	// Константный буфер b4 фона — NightSkyParameters в Shaders/sky_background.ps
	struct alignas( 16 ) NightSkyParameters
	{
		XMFLOAT3 equatorialX;
		float moonAngularRadius;
		XMFLOAT3 equatorialY;
		float starIlluminanceScale;
		XMFLOAT3 equatorialZ;
		float padding;
		XMFLOAT3 moonDiskLuminance;
		float moonDiskBrightnessLimit;	// предел яркости полного диска после экспозиции; 0 — без предела
	};

	bool createAerialPerspectiveVolume();
	Parameters currentParameters() const;
	// Звёзды и диск луны для фона: экваториальный базис, размер и яркость диска — по SunPosition уровня
	NightSkyParameters currentNightSky( const Parameters& params ) const;
	// Таблицы, зависящие только от атмосферы: пропускание, затем Ψ по нему
	void updateAtmosphereLuts();
	// Небо вокруг камеры: таблица солнца, и луны — если она на небе
	void updateSkyView();
	// Cubemap неба для SkyLight из Sky-View, 6 граней и мипы
	void renderSkyCube();
	// Объём воздушной перспективы для главного вида кадра (матрицы — в константах кадра)
	void updateAerialPerspective();
	void setParameters( const Parameters& params );
	void bindEnvironment();

	const DMLightDriver* m_lights = nullptr;
	SkyLight* m_skyLight = nullptr;
	PropertyContainer m_properties;
	bool m_backgroundVisible = true;
	Parameters m_frameParams = {};		// солнце и настройки кадра: Sky-View, воздушная перспектива, фон
	bool m_lutsValid = false;
	Parameters m_lutsFor = {};			// для каких дымки и альбедо земли посчитаны пропускание и Ψ
	bool m_environmentValid = false;
	Parameters m_capturedFor = {};		// для чего начат последний пересчёт освещения окружением

	FullscreenShader m_transmittanceShader;
	FullscreenShader m_multipleScatteringShader;
	FullscreenShader m_skyViewShader;
	FullscreenShader m_cubeShader;
	FullscreenShader m_backgroundShader;
	DMComputeShader m_aerialPerspectiveShader;
	Buffer m_constantBuffer;
	Buffer m_aerialPerspectiveConstants;	// b4 compute-прохода, AerialPerspectiveBuffer
	Buffer m_nightSkyConstants;			// b4 фона, NightSkyParameters
	const DMTexture* m_moonAlbedo = nullptr;

	// Таблицы проходов неба: t1 — Ψ, t2 — пропускание, t3 — Sky-View солнца, t4 — луны
	RenderTarget m_transmittanceLut;
	RenderTarget m_multipleScattering;
	RenderTarget m_skyViewLut;
	RenderTarget m_skyViewMoonLut;
	CubeTarget m_skyCube;	// источник SkyLight

	// RGB — рассеянный свет солнца и луны в единицах запекания, A — среднее пропускание; слой — расстояние
	Texture m_aerialPerspective;
	StorageView m_aerialPerspectiveUAV;
	ShaderView m_aerialPerspectiveSRV;
};

}
