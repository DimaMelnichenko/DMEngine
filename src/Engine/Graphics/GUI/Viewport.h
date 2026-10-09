#pragma once

#include <string>
#include <vector>
#include "DirectX.h"

class DMCamera;
class GUI;

namespace GS
{
class ModelInstances;
class TerrainHeightSource;
class WaterSimulation;
}

// Вьюпорт редактора, как в UE: клик по сцене выбирает экземпляр модели (луч из камеры к повёрнутым границам LOD 0
// экземпляров, ближайший), выбранный обведён рамкой, его подокно раскрывается в Details (окно «Model instances»); у
// выбранного — гизмо перемещения, поворота и масштаба (ImGuizmo), правит те же свойства окна. Режим гизмо — кнопки над
// сценой и клавиши 5 / 6 / 7, оси — мира или экземпляра. Режим «Stream curves» (View): кривые русел ближе 250 м — линии и
// точки; клик по точке выбирает её (подокно кривой — в Details), гизмо двигает её по X и Z, Insert — новая точка после
// выбранной, Delete — удалить; русла перестраиваются, когда правка закончена (WaterSimulation::finishCurveEdit).
// Вызывается в кадре ImGui — между GUI::Begin и GUI::End
class Viewport
{
public:
	// water и terrain — для режима кривых русел (nullptr — у уровня нет воды)
	void update( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, GS::WaterSimulation* water = nullptr,
				 const GS::TerrainHeightSource* terrain = nullptr );
	bool streamMode() const { return m_streamMode; }
	void setStreamMode( bool enabled );
	// Клик по точке окна (мышь и команда pick): в режиме кривых — сначала точка кривой, мимо — экземпляр модели.
	// Возвращает описание выбранного («Stream 7, point 3» или имя экземпляра); пусто — мимо
	std::string clickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, GS::WaterSimulation* water,
						 const GS::TerrainHeightSource* terrain, float x, float y );
	// Правка выбранной точки кривой (клавиши и команда stream): перенос в x, z и конец правки; новая точка после выбранной;
	// удаление. false — точка не выбрана или правка невозможна
	bool moveStreamPoint( GS::WaterSimulation& water, float x, float z );
	bool insertStreamPoint( GS::WaterSimulation& water );
	bool removeStreamPoint( GS::WaterSimulation& water );
	// Мягкое выделение, как пропорциональное редактирование в Blender: соседние точки по длине кривой ближе radius, м,
	// сдвигаются вместе с выбранной, плавно меньше к краю (0 — только выбранная)
	float streamFalloff() const { return m_streamFalloff; }
	void setStreamFalloff( float radius ) { m_streamFalloff = radius < 0.0f ? 0.0f : radius; }
	// Выбранный экземпляр (номер в ModelInstances), −1 — ничего
	int selected() const { return m_selected; }
	void select( int instance ) { m_selected = instance; }
	// Выбор лучом через точку экрана (пиксели окна): клик и команда pick удалённого управления; −1 — мимо
	int pickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, float x, float y );

private:
	// Клик по сцене: нажатие не над окнами редактора и отпускание почти на месте (не поворот камеры, не перетаскивание)
	bool sceneClicked();
	void drawSelection( const DMCamera& camera, const GS::ModelInstances& models ) const;
	// Кнопки режима гизмо над сценой и клавиши 5 / 6 / 7
	void drawToolbar();
	void manipulate( const DMCamera& camera, GS::ModelInstances& models );
	// Кривые русел: точка на экране; выбор ближайшей к курсору; линии и точки; гизмо и клавиши выбранной
	bool pickStreamPoint( const DMCamera& camera, const GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain, float x, float y );
	void drawStreams( const DMCamera& camera, const GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain ) const;
	void editStreamPoint( const DMCamera& camera, GS::WaterSimulation& water, const GS::TerrainHeightSource& terrain, GUI& gui );
	// Сдвиг выбранной точки так, чтобы она стала в position, — от положений в начале правки (m_dragStart), с мягким выделением
	void dragStreamPoint( GS::WaterSimulation& water, DirectX::XMFLOAT2 position );

	enum class Gizmo
	{
		move,
		rotate,
		scale
	};

	int m_selected = -1;
	bool m_pressedOverScene = false;
	Gizmo m_gizmo = Gizmo::move;
	bool m_localAxes = false;	// оси экземпляра, а не мира (у масштаба — всегда свои)
	bool m_streamMode = false;
	int m_streamCurve = -1;		// выбранная кривая русла и точка (−1 — нет)
	int m_streamPoint = -1;
	bool m_draggingPoint = false;
	float m_streamFalloff = 8.0f;
	std::vector<DirectX::XMFLOAT2> m_dragStart;	// точки кривой в начале перетаскивания
};
