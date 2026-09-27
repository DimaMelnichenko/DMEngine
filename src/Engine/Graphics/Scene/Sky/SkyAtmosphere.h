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

class DMLightDriver;

namespace GS
{

// Процедурное небо, освещение окружением и воздушная перспектива от него (как Sky Atmosphere в UE).
// Когда меняется солнце (первый включённый направленный источник уровня) или настройки неба, compute() заново:
// - считает таблицу многократного рассеяния (Shaders/sky_multiscattering.ps, модель Hillaire 2020 как в UE5);
// - рендерит cubemap неба (Рэлей, Ми, озон, многократное рассеяние — Shaders/atmosphere.sh) и строит его мипы;
// - отдаёт его в SkyLight — гармоники рассеянного света и префильтр отражений (Shaders/ibl.sh, слоты PS t101…t103).
// Каждый кадр — объём воздушной перспективы над экраном главного вида (Shaders/aerial_perspective.cs,
// Hillaire 2020 — Camera Aerial Perspective Volume в UE): свет, рассеянный воздухом между камерой и точкой, и пропускание
// до неё; его читает общая функция освещения (Shaders/aerial_perspective.sh, слот PS t106).
// Свой вызов в проходе sky рисует небо фоном кадра. Пропускание к солнцу для его света у земли
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
	};

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
	};

	bool createAerialPerspectiveVolume();
	Parameters currentParameters() const;
	void updateEnvironment( const Parameters& params );
	// Объём воздушной перспективы для главного вида кадра (матрицы — в константах кадра)
	void updateAerialPerspective();
	void setParameters( const Parameters& params );
	void bindEnvironment();

	const DMLightDriver* m_lights = nullptr;
	SkyLight* m_skyLight = nullptr;
	PropertyContainer m_properties;
	bool m_backgroundVisible = true;
	bool m_environmentValid = false;
	Parameters m_computedFor = {};

	FullscreenShader m_multipleScatteringShader;
	FullscreenShader m_cubeShader;
	FullscreenShader m_backgroundShader;
	DMComputeShader m_aerialPerspectiveShader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	com_unique_ptr<ID3D11Buffer> m_aerialPerspectiveConstants;	// b4 compute-прохода, AerialPerspectiveBuffer

	RenderTarget m_multipleScattering;
	CubeTarget m_skyCube;	// источник SkyLight и фон

	// RGB — рассеянный свет на единицу освещённости от солнца, A — среднее пропускание; слой — расстояние
	com_unique_ptr<ID3D11Texture3D> m_aerialPerspective;
	com_unique_ptr<ID3D11UnorderedAccessView> m_aerialPerspectiveUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_aerialPerspectiveSRV;
};

}
