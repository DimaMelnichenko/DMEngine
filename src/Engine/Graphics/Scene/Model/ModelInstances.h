#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Model\DMModel.h"
#include "Model\SkinningPalette.h"
#include "Common\DMTransform.h"
#include "Level\LevelDescription.h"
#include "Engine\Animation\AnimationSystem.h"

namespace GS
{

class TerrainHeightSource;
class ModelCollision;
class AnimationLibrary;

// Модели уровня (таблица LevelModels): у каждого экземпляра свой трансформ, модель — общий ресурс. За каждый вид
// выбирает LOD экземпляра по расстоянию от точки LOD вида, отсекает его по frustum вида (границы меша в мировых
// координатах) и отдаёт его меш (MeshBatch) — общей функцией collectModelMeshes (Model/ModelDraw.h, ею же рисуются
// сущности мира); в какой проход он попадёт (непрозрачные, полупрозрачные), в каком порядке и не одним ли инстансным
// вызовом с одинаковыми будет нарисован, решает рендерер. В полосе вокруг дальности LOD (Shaders/lod_transition.h)
// экземпляр отдаёт оба LOD с долями дизеринга, если материал секции это умеет (DitheredLODTransition, как Dithered LOD
// Transition в UE); за последним LOD он так же исчезает.
// Окно «Models» — свойства моделей (параметры материалов секций, не сохраняются); окно «Model instances» — подокно на
// экземпляр: положение, поворот углами (Pitch, Yaw, Roll, как Rotation в Details UE) и масштаб, сохраняются в LevelModels.
// Экземпляры добавляются и убираются в редакторе, как источники света (DMLightDriver): подокно «Add» — кнопка на модель
// палитры, «Delete» у подокна экземпляра и Delete во вьюпорте. У экземпляра — тело в мире столкновений по простой
// коллизии модели (Models.collision, ModelCollision.h): правка места, добавление, удаление и отмена его переставляют.
// Экземпляр модели со скелетом с клипом (LevelModels.animation_id) играет его по кругу по времени игры, как Skeletal Mesh
// Actor с Animation Asset в UE: поза — в compute, матрицы скиннинга кадра — SkinningPalette
class ModelInstances : public SceneObject
{
public:
	ModelInstances();

	// Вызывается после загрузки моделей: экземпляры уровня и свойства их моделей для GUI; animations — скелеты и клипы
	// мира (экземпляры с клипом), nullptr — все в позе покоя
	void initialize( const std::vector<LevelDescription::ModelInstance>& instances, const AnimationLibrary* animations = nullptr );
	// Мир столкновений (формы моделей — ModelCollision): тела всех экземпляров — сразу, дальше — по правкам
	void setCollision( ModelCollision* collision );

	// Правки окна экземпляров — в трансформы и границы
	void update( const FrameContext& frame ) override;
	// Позы экземпляров с клипом — в матрицы скиннинга кадра
	void compute( const FrameContext& frame ) override;
	// Кости поз кадра отрезками в мире (отладочный вид «Skeleton»)
	const std::vector<BoneLine>& boneLines() const { return m_boneLines; }
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override;
	// Окно экземпляров — отдельная запись Outliner, сохраняется с уровнем (в отличие от свойств моделей)
	PropertyContainer* instanceProperties() { return &m_instanceProperties; }
	// Экземпляры с правками окна — для сохранения уровня (строки LevelModels по id)
	std::vector<LevelDescription::ModelInstance> instances() const;

	// Выбор во вьюпорте редактора (distance — до пересечения; −1 — ни один). С миром столкновений — лучом Jolt: попал в
	// тело экземпляра — он (тонкая плоскость не закрывает то, что за ней); иначе — экземпляр, чьи повёрнутые границы LOD 0
	// луч пересекает ближе всех, но ближе точки попадания (земля и лес закрывают то, что за ними; крона ели рощи, у которой
	// тело — только ствол, выбирается по границам)
	int pick( DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float& distance ) const;
	uint32_t instanceCount() const { return static_cast<uint32_t>( m_instances.size() ); }
	// Границы LOD 0 экземпляра в мире (рамка выбранного); false — у модели нет LOD
	bool instanceBox( int instance, DirectX::BoundingOrientedBox& box ) const;
	// Пересадка на новую землю (эрозия пересчитана): y экземпляра сдвигается на shift( x, z, радиус под моделью ) — через
	// свойства окна, правка видна как изменённая. Возвращает, сколько экземпляров сдвинуто
	size_t reseat( const std::function<float( float x, float z, float radius )>& shift );
	// На землю (End, как в UE): низ границ LOD 0 — на самую высокую точку земли под ними. false — земли под ним нет
	bool snapToTerrain( int instance, const TerrainHeightSource& terrain );
	// Мировая матрица экземпляра и её запись в свойства окна (положение, поворот углами, масштаб) — гизмо вьюпорта;
	// трансформ меняется в update, как после правки в Details
	DirectX::XMMATRIX instanceMatrix( int instance ) const { return m_instances[instance].transform.worldMatrix(); }
	void setInstanceMatrix( int instance, DirectX::FXMMATRIX matrix );
	// Подокно экземпляра в окне «Model instances» и его имя («id: модель»)
	PropertyContainer* instanceProperties( int instance ) const { return m_instances[instance].properties.get(); }
	const std::string& instanceName( int instance ) const { return m_instances[instance].name; }
	// Границы всех экземпляров (меш LOD 0 в мировых координатах); false — экземпляров нет
	bool bounds( DirectX::BoundingBox& bounds ) const;
	// Границы в мире (LOD0) экземпляров не ниже minHeight, м: ели рощи, постройки — препятствия ветру (поле ветра)
	std::vector<DirectX::BoundingBox> tallBounds( float minHeight ) const;

	// Добавление и удаление в редакторе. У экземпляра — ключ, постоянный в сеансе (по нему его находят выбор вьюпорта и
	// отмена: номер после добавления и удаления сдвигается). Новый — в конец списка, без поворота, масштаб 1; убранный
	// хранится с подокном до выхода — restoreInstance возвращает его на прежнее место, история правок его свойств остаётся
	// верной. Строки LevelModels — «Save level»: новые (id 0) вставляются, убранные удаляются (removedIds, markSaved).
	// terrain — поставить новый на землю низом границ LOD 0, как End (snapToTerrain)
	uint32_t addInstance( uint32_t model, const DirectX::XMFLOAT3& position, const TerrainHeightSource* terrain = nullptr );
	bool removeInstance( uint32_t key );
	bool restoreInstance( uint32_t key );
	// Ключ экземпляра (0 — нет такого) и номер по ключу (−1 — убран или нет такого)
	uint32_t key( int instance ) const;
	int indexOf( uint32_t key ) const;
	// Модели, экземпляры которых можно добавить: модели экземпляров уровня при загрузке (у моделей расстановки материал
	// пула — экземпляром уровня их не нарисовать; новые модели — Tools/import_gltf.py). Подокно «Add» окна экземпляров —
	// кнопки моделей палитры ставит редактор (место — перед камерой)
	const std::vector<uint32_t>& palette() const { return m_palette; }
	const std::string& modelName( uint32_t model ) const;
	PropertyContainer* addProperties() { return &m_addProperties; }
	// Строки LevelModels убранных экземпляров, которые ещё есть в базе
	std::vector<uint32_t> removedIds() const;
	// Уровень сохранён: saved — instances() с id вставленных строк; убранных в базе больше нет
	void markSaved( const std::vector<LevelDescription::ModelInstance>& saved );
	// Кнопка подокна экземпляра, которая его убирает (с отменой); её же нажимает Delete во вьюпорте
	static constexpr const char* deleteButton = "Delete";

private:
	struct Instance
	{
		DMModel* model = nullptr;
		uint32_t modelId = 0;
		uint32_t id = 0;	// строка LevelModels; 0 — новый, в базе ещё нет
		uint32_t key = 0;
		DMTransform transform;
		DirectX::XMFLOAT3 angles = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// применённый поворот: pitch, yaw, roll, градусы
		std::unique_ptr<PropertyContainer> properties;	// «Position», «Rotation», «Scale»
		std::string name;	// «id: модель» — имя подокна
		uint32_t body = 0;			// тело в мире столкновений (0 — нет)
		bool bodyStale = true;		// место сменилось после постройки тела
		uint32_t animation = 0;		// клип модели со скелетом (LevelModels.animation_id); 0 — поза покоя
		uint32_t boneOffset = 0;	// первая матрица скиннинга экземпляра в кадре
		bool posed = false;			// поза кадра посчитана (compute)
	};

	// Границы всех экземпляров: при загрузке и после правки
	void updateBounds();
	// Тела экземпляров, чьё место сменилось (или новых), — заново
	void syncBodies();
	// Подокно экземпляра: положение, поворот, масштаб и кнопка Delete
	void createProperties( Instance& instance );
	// После добавления и удаления: подокна по порядку с именами («id: модель», у нового — «new ключ: модель»), отметка
	// несохранённого (новый экземпляр, убранный из базы), границы
	void updateStructure();

	std::vector<Instance> m_instances;
	std::vector<Instance> m_removed;	// убранные — до выхода (отмена удаления)
	std::vector<uint32_t> m_palette;
	uint32_t m_nextKey = 1;
	DirectX::BoundingBox m_bounds;
	ModelCollision* m_collision = nullptr;
	const AnimationLibrary* m_animations = nullptr;
	SkinningPalette m_skinning;
	PoseScratch m_poseScratch;
	std::vector<JointTransform> m_pose;
	std::vector<DirectX::XMFLOAT4X4> m_jointMatrices;
	std::vector<BoneLine> m_boneLines;
	PropertyContainer m_properties;
	PropertyContainer m_instanceProperties;
	PropertyContainer m_addProperties;
};

}
