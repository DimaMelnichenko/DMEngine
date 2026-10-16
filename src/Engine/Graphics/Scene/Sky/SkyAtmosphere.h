#pragma once

#include <string>
#include <vector>
#include "SceneObject.h"
#include "SkyLight.h"
#include "FullscreenShader.h"
#include "ComputeShader.h"
#include "D3D\CubeTarget.h"
#include "D3D\RenderTarget.h"
#include "Properties\PropertyContainer.h"
#include "Texture\DMTexture.h"
#include "Level\LevelSettings.h"
#include "Shaders\sky_atmosphere_constants.h"
#include "Shaders\night_sky_constants.h"
#include "Shaders\aerial_perspective_constants.h"

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
// Свой вызов в проходе sky рисует небо фоном кадра — из Sky-View (Shaders/sky_background.ps; ночью с Млечным Путём по
// карте уровня), ночью поверх него — звёзды каталога (таблица Stars, Shaders/sky_stars.vs). Пропускание к солнцу для его света у земли
// (Atmosphere Sun Light) считает на CPU по той же модели — sunTransmittance. Настройки — строка таблицы SkyAtmosphere,
// на которую ссылается уровень (Levels.atmosphere), и окно GUI «Sky atmosphere»
class SkyAtmosphere : public SceneObject
{
public:
	static constexpr uint32_t multipleScatteringSize = 32;

	// Строка SkyAtmosphere (Level/LevelSettings.h)
	using Settings = SkyAtmosphereSettings;

	// Имя карты луны в таблице Textures (NASA CGI Moon Kit); нет — ровный диск
	static constexpr const char* moonAlbedoTexture = "moon_albedo";

	SkyAtmosphere();

	// skyLight — освещение окружением сцены: атмосфера отдаёт в него cubemap неба; stars — звёзды фона (таблица Stars)
	bool initialize( const DMLightDriver& lights, const Settings& settings, const std::vector<StarRecord>& stars,
					 SkyLight& skyLight );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Настройки пресета окружения (на лету) — в окно
	void apply( const Settings& settings );
	// Фон не рисуется, если у уровня своя модель неба (SkySphere); освещение окружением остаётся
	void setBackgroundVisible( bool visible );
	// Пропускание атмосферы от наблюдателя к солнцу (toSun — нормированное направление на него): доля света солнца
	// над атмосферой, которая доходит до земли, по каналам RGB. Как transmittanceToTop в Shaders/atmosphere.sh
	// с дымкой из GUI; солнце под горизонтом — 0. Как GetTransmittanceAtGroundLevel в UE — на CPU, микросекунды
	DirectX::XMFLOAT3 sunTransmittance( const DirectX::XMFLOAT3& toSun ) const;
	// Нормировка того, что в половинной точности (объём воздушной перспективы, результаты SkyLight): оценка яркости неба
	// в единицах запекания — 1 днём, в сумерках падает с высотой солнца, ночью — свет луны и ночное свечение. Там
	// хранится свет, делённый на неё, шейдеры умножают на масштаб с ней (cb_aerialPerspectiveScale, cb_skyLightScale):
	// ночью значения ~10⁻⁷…10⁻⁹ не пропадают
	float skyNormalization() const;
	// Для облаков (VolumetricCloud): параметры неба кадра (b2) и таблицы Ψ и пропускания (t1, t2) — их compute-проходу
	void bindForClouds();
	const ShaderView& transmittanceLut() const { return m_transmittanceLut.srv(); }
	const ShaderView& multipleScatteringLut() const { return m_multipleScattering.srv(); }
	// Светило облаков кадра: солнце, пока оно выше −3°, иначе луна; свет над атмосферой — в единицах запекания неба
	void cloudLight( DirectX::XMFLOAT3& direction, DirectX::XMFLOAT3& color ) const;

	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	PropertyContainer* properties() override;
	bool environment() const override { return true; }

private:


	// Звезда в буфере GPU — Star в Shaders/sky_stars.vs
	struct StarData
	{
		DirectX::XMFLOAT3 direction;	// экваториальные координаты J2000
		float illuminance;				// над атмосферой, лк
		DirectX::XMFLOAT3 color;		// линейный, яркость 1
		float padding;
	};
	static_assert( sizeof( StarData ) == 32, "StarData must match Star in Shaders/sky_stars.vs" );

	bool createAerialPerspectiveVolume();
	// Буфер звёзд: направление, освещённость по звёздной величине и цвет по показателю B − V
	bool createStars( const std::vector<StarRecord>& stars );
	// Карта Млечного Пути (file от Textures\): нормированная на среднее по сфере 1, с мипами; сводка — в лог. Нет файла —
	// фон без него
	bool loadMilkyWay( const std::string& file );
	SkyParameters currentParameters() const;
	// Звёзды и диск луны для фона: экваториальный базис, размер, яркость, поворот диска и пепельный свет — по SunPosition
	// уровня
	NightSkyParameters currentNightSky( const SkyParameters& params ) const;
	// Таблицы, зависящие только от атмосферы: пропускание, затем Ψ по нему
	void updateAtmosphereLuts();
	// Небо вокруг камеры: таблица солнца, и луны — если она на небе
	void updateSkyView();
	// Cubemap неба для SkyLight из Sky-View, 6 граней и мипы
	void renderSkyCube();
	// Объём воздушной перспективы для главного вида кадра (матрицы — в константах кадра)
	void updateAerialPerspective();
	void setParameters( const SkyParameters& params );
	void bindEnvironment();

	const DMLightDriver* m_lights = nullptr;
	SkyLight* m_skyLight = nullptr;
	PropertyContainer m_properties;
	bool m_backgroundVisible = true;
	SkyParameters m_frameParams = {};		// солнце и настройки кадра: Sky-View, воздушная перспектива, фон
	bool m_lutsValid = false;
	SkyParameters m_lutsFor = {};			// для каких дымки и альбедо земли посчитаны пропускание и Ψ
	bool m_environmentValid = false;
	SkyParameters m_capturedFor = {};		// для чего начат последний пересчёт освещения окружением

	FullscreenShader m_transmittanceShader;
	FullscreenShader m_multipleScatteringShader;
	FullscreenShader m_skyViewShader;
	FullscreenShader m_cubeShader;
	FullscreenShader m_backgroundShader;
	ShaderProgram m_starShader;
	ComputeShader m_aerialPerspectiveShader;
	Buffer m_constantBuffer;
	Buffer m_aerialPerspectiveConstants;	// b4 compute-прохода, AerialPerspectiveBuffer
	Buffer m_nightSkyConstants;			// b4 фона, NightSkyParameters
	const DMTexture* m_moonAlbedo = nullptr;
	Buffer m_stars;
	ShaderView m_starsView;
	uint32_t m_starCount = 0;
	std::string m_milkyWayFile;			// для сохранения уровня; пусто — Млечного Пути нет
	Texture m_milkyWayTexture;
	ShaderView m_milkyWay;
	bool m_hasMilkyWay = false;

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
