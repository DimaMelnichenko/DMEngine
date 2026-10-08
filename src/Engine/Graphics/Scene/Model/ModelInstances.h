#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <DirectXCollision.h>
#include "SceneObject.h"
#include "Model\DMModel.h"
#include "Common\DMTransform.h"
#include "Level\LevelDescription.h"

namespace GS
{

class TerrainHeightSource;

// Модели уровня (таблица LevelModels): у каждого экземпляра свой трансформ, модель — общий ресурс. За каждый вид
// выбирает LOD экземпляра по расстоянию от точки LOD вида, отсекает его по frustum вида (границы меша в мировых
// координатах) и отдаёт его меш (MeshBatch); в какой проход он попадёт (непрозрачные, полупрозрачные), в каком
// порядке и не одним ли инстансным вызовом с одинаковыми будет нарисован, решает рендерер. В полосе вокруг дальности
// LOD (Shaders/lod_transition.h) экземпляр отдаёт оба LOD с долями дизеринга, если материал секции это умеет
// (DitheredLODTransition, как Dithered LOD Transition в UE); за последним LOD он так же исчезает.
// Окно «Models» — свойства моделей (параметры материалов секций, не сохраняются); окно «Model instances» — подокно на
// экземпляр: положение, поворот углами (Pitch, Yaw, Roll, как Rotation в Details UE) и масштаб, сохраняются в LevelModels
class ModelInstances : public SceneObject
{
public:
	ModelInstances();

	// Вызывается после загрузки моделей: экземпляры уровня и свойства их моделей для GUI
	void initialize( const std::vector<LevelDescription::ModelInstance>& instances );

	// Правки окна экземпляров — в трансформы и границы
	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	PropertyContainer* properties() override;
	// Окно экземпляров — отдельная запись Outliner, сохраняется с уровнем (в отличие от свойств моделей)
	PropertyContainer* instanceProperties() { return &m_instanceProperties; }
	// Экземпляры с правками окна — для сохранения уровня (строки LevelModels по id)
	std::vector<LevelDescription::ModelInstance> instances() const;

	// Выбор во вьюпорте редактора: экземпляр, чьи повёрнутые границы LOD 0 луч пересекает ближе всех (distance — до
	// пересечения); −1 — ни один
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

private:
	struct Instance
	{
		DMModel* model = nullptr;
		uint32_t modelId = 0;
		uint32_t id = 0;	// строка LevelModels
		DMTransform transform;
		DirectX::XMFLOAT3 angles = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// применённый поворот: pitch, yaw, roll, градусы
		std::unique_ptr<PropertyContainer> properties;	// «Position», «Rotation», «Scale»
		std::string name;	// «id: модель» — имя подокна
	};

	// Секции LOD экземпляра в список вида. lodDither — доля перехода (MeshBatch::lodDither): (0; 1) — уходящий LOD,
	// (−1; 0) — приходящий. Секция без дизеринга рисуется, только пока её сторона перехода — не меньше половины: LOD
	// сменяется мгновенно на дальности модели, как без полосы
	void addLod( const Instance& instance, uint16_t lodIndex, float lodDither, float distance, const RenderView& view,
				 MeshCollector& collector ) const;

	// Границы всех экземпляров: при загрузке и после правки
	void updateBounds();

	std::vector<Instance> m_instances;
	DirectX::BoundingBox m_bounds;
	PropertyContainer m_properties;
	PropertyContainer m_instanceProperties;
};

}
