#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "SceneObject.h"
#include "Properties\PropertyContainer.h"
#include "Engine\Simulation\World.h"
#include "Engine\Animation\AnimationSystem.h"
#include "Model\SkinningPalette.h"

namespace GS
{

struct ModelVisual;
struct Transform;

// Сущности мира симуляции (World, ядро) на экране — объект сцены без своих данных: за каждый вид сущности с местом и
// моделью (Transform, ModelVisual) отдают секции модели в список отрисовки, как экземпляры моделей уровня (общая функция
// collectModelMeshes, Model/ModelDraw.h); место — смешанное между прежним и новым по доле шага мира (World::interpolation).
// Сущность со скелетом (AnimationState) — в позе кадра: её собирает compute (evaluatePose по доле шага) в матрицы скиннинга
// (SkinningPalette). Рендерер о сущностях не знает: одинаковые меши без скелета подряд он сам рисует инстансами. В съёмку
// сцены (карты отражений) они не попадают — движутся, как трава и частицы. Окно «World» — частота шагов, пауза мира и
// отладочные виды столкновений и костей («Show collision», «Show skeleton», рисует вьюпорт; с уровнем не сохраняется);
// команда world — сущности, шаги, бродяги для проверки (docs/simulation.md)
class WorldEntities : public SceneObject
{
public:
	WorldEntities();

	void initialize( World& world );
	bool initialized() const { return m_world != nullptr; }

	// Правки окна — в мир (частота шагов, пауза)
	void update( const FrameContext& frame ) override;
	// Позы сущностей со скелетом — в матрицы скиннинга кадра
	void compute( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override { return &m_properties; }

	// Бродяги (WanderSystem, MovementSystem): count сущностей модели model (имя из загруженных) в круге radius вокруг (x, z) —
	// на земле, каждая со своим зерном; с землёй столкновений — персонажами (капсула по границам модели: не проходят сквозь
	// модели уровня, друг друга и глубокую воду); у модели со скелетом — анимация (шаг по скорости, AnimationSystem). false —
	// модели нет среди загруженных (error — почему)
	bool spawnWanderers( const std::string& model, float x, float z, uint32_t count, float radius, std::string& error );
	// Скорость блуждания всех сущностей, м/с (команда world speed); сколько сущностей
	size_t setWanderSpeed( float speed );
	// Цепочка клипов всем сущностям со скелетом (имена клипов их скелета; пустое имя — шаг по скорости): первый — сейчас, с
	// переходом, остальные — когда разовый дойдёт до конца (playSequence). Сколько сущностей её получили
	size_t playClips( const std::vector<std::string>& clips );
	// Кости поз кадра отрезками в мире (отладочный вид «Skeleton»)
	const std::vector<BoneLine>& boneLines() const { return m_boneLines; }

private:
	// Мировая матрица модели сущности на месте между прежним и новым шагом; origin — точка модели (низ на земле)
	DirectX::XMMATRIX placeMatrix( entt::entity entity, const Transform& current, const ModelVisual& visual, float t,
								   DirectX::XMFLOAT3& origin ) const;

	World* m_world = nullptr;
	PropertyContainer m_properties;
	uint32_t m_spawned = 0;		// сущностей создано за сеанс: зерно следующей
	SkinningPalette m_skinning;
	std::unordered_map<entt::entity, uint32_t> m_boneOffsets;	// поза кадра: первая матрица скиннинга сущности
	PoseScratch m_poseScratch;
	std::vector<JointTransform> m_pose;
	std::vector<DirectX::XMFLOAT4X4> m_jointMatrices;
	std::vector<BoneLine> m_boneLines;
};

}
