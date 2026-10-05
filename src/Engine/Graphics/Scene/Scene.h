#pragma once

#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Sky\SkySphere.h"
#include "Sky\SkyAtmosphere.h"
#include "Sky\HDRIBackdrop.h"
#include "Terrain\CDLODTerrain.h"
#include "Model\ModelInstances.h"
#include "Scatterer\Scatterer.h"
#include "Particle\DMParticleSystem.h"
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
	bool initialize();

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
	// Текущие свет, небо и постобработку (правки в GUI) — в строки уровня в base.db3
	bool saveEnvironment( LibraryLoader& library, const PostProcessSettings& postProcess );

	CDLODTerrain& terrain();
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
	HDRIBackdrop m_hdri;		// панорама вместо атмосферы (Levels.hdri_backdrop)
	bool m_useHDRI = false;
	SkySphere m_sky;			// модель неба уровня (Levels.sky), если задана — вместо фона атмосферы
	CDLODTerrain m_terrain;
	WaterSimulation m_water;	// вода по рельефу (Levels.water_simulation), только с террейном
	ModelInstances m_models;
	std::vector<std::unique_ptr<Scatterer>> m_scatterers;
	DMParticleSystem m_particles;

	std::vector<SceneObject*> m_objects;
};

}
