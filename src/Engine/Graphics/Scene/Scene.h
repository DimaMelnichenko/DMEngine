#pragma once

#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Sky\SkySphere.h"
#include "Sky\SkyAtmosphere.h"
#include "Terrain\CDLODTerrain.h"
#include "Model\ModelInstances.h"
#include "Scatterer\Scatterer.h"
#include "Particle\DMParticleSystem.h"
#include "Light\DMLightDriver.h"
#include "ObjectLibrary\LibraryLoader.h"

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
	// Создаёт объекты сцены и свет; вызывается после loadResources() и pipeline().init()
	bool initialize();

	void update( const FrameContext& frame );

	const std::vector<SceneObject*>& objects() const;
	DMLightDriver& lights();
	const LevelDescription& level() const;
	// Текущие свет, небо и постобработку (правки в GUI) — в строки уровня в base.db3
	bool saveEnvironment( LibraryLoader& library, const PostProcess::Settings& postProcess );

	CDLODTerrain& terrain();
	// Границы того, что может отбросить тень: террейн (мир × диапазон высот) и модели уровня
	DirectX::BoundingBox bounds() const;
	// Наборы расстановки уровня: трава, камешки
	const std::vector<std::unique_ptr<Scatterer>>& scatterers() const;

private:
	LevelDescription m_level;
	DMLightDriver m_lightDriver;

	SkyAtmosphere m_atmosphere;	// процедурное небо и освещение окружением от него
	SkySphere m_sky;			// модель неба уровня (Levels.sky), если задана — вместо фона атмосферы
	CDLODTerrain m_terrain;
	ModelInstances m_models;
	std::vector<std::unique_ptr<Scatterer>> m_scatterers;
	DMParticleSystem m_particles;

	std::vector<SceneObject*> m_objects;
};

}
