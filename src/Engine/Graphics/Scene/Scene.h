#pragma once

#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Sky\SkySphere.h"
#include "Sky\SkyAtmosphere.h"
#include "Sky\HDRIBackdrop.h"
#include "Sky\VolumetricCloud.h"
#include "Terrain\CDLODTerrain.h"
#include "Model\ModelInstances.h"
#include "Scatterer\Scatterer.h"
#include "Particle\ParticleSystem.h"
#include "Light\DMLightDriver.h"
#include "Wind\Wind.h"
#include "Water\WaterSimulation.h"
#include "Level\LevelDescription.h"

class LibraryLoader;

namespace GS
{

// Содержимое уровня: ресурсы из base.db3, объекты сцены и свет. Состав уровня — строка таблицы Levels
// (террейн, небо, трава, частицы) и его модели из LevelModels. Объекты рисуются в порядке objects() внутри
// своего прохода; объект, которого у уровня нет, остаётся неинициализированным и ничего не делает
class Scene
{
public:
	// Загружает материалы, состав уровня levelName (пустое имя — первый уровень), его модели и текстуры
	bool loadResources( LibraryLoader& library, const std::string& levelName );
	// Создаёт объекты сцены и свет; вызывается после loadResources() и Renderer::initialize()
	// particles — частицы уровня (-noparticles: нет, кадры с одной точки совпадают)
	bool initialize( bool particles = true );

	// Раз за кадр до update(): правки источников в GUI и время суток, затем свет солнца у земли — через атмосферу
	// seconds — время кадра: с ним идёт время суток
	void updateLights( float seconds = 0.0f );
	// Небо уровня — процедурная атмосфера (с воздушной перспективой) или HDRI-панорама
	bool hasAtmosphere() const;
	// Масштаб неба и освещения окружением (cb_skyLightScale): освещённость от солнца над атмосферой, лк, или
	// интенсивность панорамы
	float skyLightScale();
	// Масштаб фона неба (cb_skyScale): освещённость от солнца над атмосферой, лк, или интенсивность панорамы
	float skyScale();
	// Множитель объёма воздушной перспективы (cb_aerialPerspectiveScale): cb_skyScale × нормировка объёма
	float aerialPerspectiveScale();
	void update( const FrameContext& frame );

	const std::vector<SceneObject*>& objects() const;
	DMLightDriver& lights();
	// Ветер уровня (Levels.wind): константы кадра — Wind::parameters
	Wind& wind();
	const LevelDescription& level() const;
	// Правки GUI — в строки уровня в base.db3 («Save level»): свет, небо, облака, постобработка, туман, ветер, затем
	// террейн, расстановка, вода и частицы
	bool saveLevel( LibraryLoader& library, const PostProcessSettings& postProcess,
					const std::optional<HeightFogSettings>& heightFog );
	// Тень облаков для констант кадра (cb_cloudShadow); облаков нет — нули
	DirectX::XMFLOAT4 cloudShadow( const RenderView& view );

	CDLODTerrain& terrain();
	// Экземпляры моделей уровня (LevelModels)
	ModelInstances& models();
	// Вода по рельефу (Levels.water_simulation); без неё у уровня объект не инициализирован
	WaterSimulation& water();
	// Границы того, что может отбросить тень: террейн (мир × диапазон высот) и модели уровня
	DirectX::BoundingBox bounds() const;
	// Наборы расстановки уровня: трава, камешки
	const std::vector<std::unique_ptr<Scatterer>>& scatterers() const;

private:
	LevelDescription m_level;
	DMLightDriver m_lightDriver;
	Wind m_wind;

	SkyLight m_skyLight;		// освещение окружением: из неба атмосферы или панорамы
	SkyAtmosphere m_atmosphere;	// процедурное небо, от него — освещение окружением и воздушная перспектива
	VolumetricCloud m_clouds;	// облака (Levels.volumetric_cloud), только с атмосферой
	HDRIBackdrop m_hdri;		// панорама вместо атмосферы (Levels.hdri_backdrop)
	bool m_useHDRI = false;
	SkySphere m_sky;			// модель неба уровня (Levels.sky), если задана — вместо фона атмосферы
	CDLODTerrain m_terrain;
	WaterSimulation m_water;	// вода по рельефу (Levels.water_simulation), только с террейном
	ModelInstances m_models;
	std::vector<std::unique_ptr<Scatterer>> m_scatterers;
	ParticleSystem m_particles;	// частицы уровня (LevelParticleEmitters); -noparticles — нет

	std::vector<SceneObject*> m_objects;
};

}
