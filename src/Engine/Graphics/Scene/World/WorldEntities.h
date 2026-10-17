#pragma once

#include <string>
#include "SceneObject.h"
#include "Properties\PropertyContainer.h"
#include "Engine\Simulation\World.h"

namespace GS
{

// Сущности мира симуляции (World, ядро) на экране — объект сцены без своих данных: за каждый вид сущности с местом и
// моделью (Transform, ModelVisual) отдают секции модели в список отрисовки, как экземпляры моделей уровня (общая функция
// collectModelMeshes, Model/ModelDraw.h); место — смешанное между прежним и новым по доле шага мира (World::interpolation).
// Рендерер о сущностях не знает: одинаковые меши подряд он сам рисует инстансами. В съёмку сцены (карты отражений) они не
// попадают — движутся, как трава и частицы. Окно «World» — частота шагов и пауза мира (с уровнем не сохраняется);
// команда world — сущности, шаги, бродяги для проверки (docs/simulation.md)
class WorldEntities : public SceneObject
{
public:
	WorldEntities();

	void initialize( World& world );
	bool initialized() const { return m_world != nullptr; }

	// Правки окна — в мир (частота шагов, пауза)
	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override { return &m_properties; }

	// Бродяги (WanderSystem): count сущностей модели model (имя из загруженных) в круге radius вокруг (x, z) — на земле,
	// каждая со своим зерном; false — модели нет среди загруженных (error — почему)
	bool spawnWanderers( const std::string& model, float x, float z, uint32_t count, float radius, std::string& error );

private:
	World* m_world = nullptr;
	PropertyContainer m_properties;
	uint32_t m_spawned = 0;		// сущностей создано за сеанс: зерно следующей
};

}
