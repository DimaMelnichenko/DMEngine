#pragma once

#include <string>
#include <vector>
#include "DirectX.h"
#include "Properties/Property.h"
#include "imgui.h"

class DMCamera;
class DMLightDriver;
class GUI;

namespace GS
{
class TerrainPipeline;
class TerrainPaintTool;
class ModelInstances;
class ParticleSystem;
class Scene;
class TerrainHeightSource;
class WaterSimulation;
class StreamCurves;
}

// Вьюпорт редактора, как в UE: клик по сцене выбирает экземпляр модели (луч из камеры к повёрнутым границам LOD 0
// экземпляров, ближайший), выбранный обведён рамкой, его подокно раскрывается в Details (окно «Model instances»); у
// выбранного — гизмо перемещения, поворота и масштаба (ImGuizmo), правит те же свойства окна. Источники света —
// значками (точечный и прожектор — на месте, направленный — в небе, откуда идёт свет); клик выбирает источник (его
// подокно — в Details), гизмо двигает точечный и прожектор и поворачивает прожектор, значок направленного тянут по небу
// (у солнца и луны при SunPosition — временем суток). Delete убирает выбранный экземпляр или источник (кнопка Delete его
// подокна, с отменой). Эмиттеры частиц с местом (в точке и в шаре) — тоже значками: клик выбирает (подокно в окне «Particles»), гизмо двигает
// (свойство Position), у шара видна его граница. Режим гизмо — кнопки над
// сценой и клавиши 5 / 6 / 7, оси — мира или экземпляра. Режим «Stream curves» (View): кривые русел ближе 250 м — линии и
// точки; клик по точке выбирает её (подокно кривой — в Details), гизмо двигает её по X и Z, Insert — новая точка после
// выбранной, Delete — удалить; русла перестраиваются, когда правка закончена (StreamCurves::finishCurveEdit).
// Режим «Terrain paint» (View): кисть слоёв земли и масок поверх правил (TerrainPaintTool, подокно «Paint» окна террейна) —
// круг кисти на рельефе под курсором, ЛКМ — мазок (Shift — убавить в режиме «Step», Ctrl — стереть мазок), мазок — одна
// правка истории (Ctrl+Z); клик по сцене в этом режиме ничего не выбирает.
// Вызывается в кадре ImGui — между GUI::Begin и GUI::End
class Viewport
{
public:
	// Кадр вьюпорта: scene — что показывать и править (экземпляры моделей, кривые русел, источники света, эмиттеры)
	void update( const DMCamera& camera, GS::Scene& scene, GUI& gui );
	bool streamMode() const { return m_streamMode; }
	bool paintMode() const { return m_paintMode; }
	void setPaintMode( bool enabled );
	// Мазок кистью подокна «Paint» через точки окна (пиксели) — как ведут мышью с зажатой ЛКМ (команда brush): луч к рельефу,
	// промежуточные касания, правка в истории. Ответ — что закрашено; «error: …» — мимо рельефа или нет цели
	std::string brushStroke( const DMCamera& camera, GS::TerrainPaintTool& paint, const GS::TerrainPipeline& terrain, GUI& gui,
							 const std::vector<ImVec2>& points, bool shift, bool ctrl );
	void setStreamMode( bool enabled );
	// Клик по точке окна (мышь и команда pick): в режиме кривых — сначала точка кривой, затем ближайший значок источника
	// света или эмиттера, мимо — экземпляр модели. Возвращает описание выбранного («Stream 7, point 3», подокно источника
	// или эмиттера, имя экземпляра); пусто — мимо
	std::string clickAt( const DMCamera& camera, GS::Scene& scene, GUI& gui, float x, float y );
	// Выбранный источник света (номер в DMLightDriver::lights), −1 — ничего. Выбор хранится ключом источника
	// (DMLightDriver::key): номер сдвигается, когда источники добавляют и убирают; убрали выбранный — выбора нет
	int selectedLight() const { return m_selectedLight; }
	// Выбрать источник по ключу (новый источник — кнопки окна «Lights»); экземпляр и точка кривой — не выбраны
	void selectLight( const DMLightDriver& lights, uint32_t key );
	// Выбранный эмиттер частиц (номер в ParticleSystem), −1 — ничего
	int selectedEmitter() const { return m_selectedEmitter; }
	// Правка выбранной точки кривой (клавиши и команда stream): перенос в x, z и конец правки; новая точка после выбранной;
	// удаление. false — точка не выбрана или правка невозможна
	bool moveStreamPoint( GS::StreamCurves& streams, float x, float z );
	bool insertStreamPoint( GS::StreamCurves& streams );
	bool removeStreamPoint( GS::StreamCurves& streams );
	// Мягкое выделение, как пропорциональное редактирование в Blender: соседние точки по длине кривой ближе radius, м,
	// сдвигаются вместе с выбранной, плавно меньше к краю (0 — только выбранная)
	float streamFalloff() const { return m_streamFalloff; }
	void setStreamFalloff( float radius ) { m_streamFalloff = radius < 0.0f ? 0.0f : radius; }
	// Выбранный экземпляр (номер в ModelInstances), −1 — ничего. Выбор хранится ключом экземпляра (ModelInstances::key),
	// как у источников; selectInstance — выбрать по ключу (новый экземпляр — кнопки «Add»)
	int selected() const { return m_selected; }
	void selectInstance( const GS::ModelInstances& models, uint32_t key );
	// Выбор лучом через точку экрана (пиксели окна): клик и команда pick удалённого управления; −1 — мимо
	int pickAt( const DMCamera& camera, GS::ModelInstances& models, GUI& gui, float x, float y );

private:
	// Клик по сцене: нажатие не над окнами редактора и отпускание почти на месте (не поворот камеры, не перетаскивание)
	bool sceneClicked();
	// Ничего не выбрано: экземпляр, источник, эмиттер, точка кривой
	void clearSelection();
	void drawSelection( const DMCamera& camera, const GS::ModelInstances& models ) const;
	// Кнопки режима гизмо над сценой и клавиши 5 / 6 / 7; rotate / scale — есть ли такие режимы у выбранного (у
	// источников света масштаба нет, у точечного — и поворота)
	void drawToolbar( bool rotate = true, bool scale = true, bool localAxes = true );
	void manipulate( const DMCamera& camera, GS::ModelInstances& models, GUI& gui );
	// Перетаскивание гизмо — одна правка истории (Ctrl+Z): «до» — значения свойств до кадра, в котором его взяли
	// (snapshotGizmo каждый кадр, пока гизмо не держат)
	void snapshotGizmo( const std::vector<Property*>& properties );
	void trackGizmo( bool using_, GUI& gui, const std::vector<Property*>& properties, const std::string& name );
	// Источники света: место значка на экране (направленный — в небе, откуда идёт свет); false — за камерой
	bool lightScreen( const DMCamera& camera, const DMLightDriver& lights, size_t index, DirectX::XMFLOAT2& screen ) const;
	// Ближайший к точке значок ближе lightPickPixels (−1 — нет); distance — до него, пиксели
	int pickLight( const DMCamera& camera, const DMLightDriver& lights, float x, float y, float& distance ) const;
	void drawLights( const DMCamera& camera, const DMLightDriver& lights ) const;
	// Правка выбранного источника: гизмо у точечного и прожектора, перетаскивание значка направленного по небу
	void editLight( const DMCamera& camera, DMLightDriver& lights, GUI& gui );
	void dragDirectionalLight( const DMCamera& camera, DMLightDriver& lights, GUI& gui );
	// Эмиттеры частиц с местом: значок на экране (false — за камерой или места нет), выбор ближайшего к точке (расстояние
	// в пикселях — в distance), значки и граница шара выбранного, гизмо перемещения
	bool emitterScreen( const DMCamera& camera, const GS::ParticleSystem& particles, size_t index, DirectX::XMFLOAT2& screen ) const;
	int pickEmitter( const DMCamera& camera, const GS::ParticleSystem& particles, float x, float y, float& distance ) const;
	void drawEmitters( const DMCamera& camera, GS::ParticleSystem& particles ) const;
	void editEmitter( const DMCamera& camera, GS::ParticleSystem& particles, GUI& gui );
	// Гизмо источника и эмиттера над матрицей world (строки DirectXMath: оси и место): перенос по осям мира или поворот по
	// своим осям (rotate); true — гизмо изменил матрицу в этом кадре
	bool manipulatePoint( const DMCamera& camera, DirectX::XMFLOAT4X4& world, bool rotate );
	// Кривые русел: точка на экране; выбор ближайшей к курсору; линии и точки; гизмо и клавиши выбранной
	bool pickStreamPoint( const DMCamera& camera, const GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain, float x, float y );
	void drawStreams( const DMCamera& camera, const GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain ) const;
	// Кисть: точка под курсором, круг, мазок мышью; конец мазка — в историю правок
	void paintTerrain( const DMCamera& camera, GS::TerrainPaintTool& paint, const GS::TerrainPipeline& terrain, GUI& gui );
	void finishPaintStroke( GS::TerrainPaintTool& paint, GUI& gui );
	bool terrainPoint( const DMCamera& camera, const GS::TerrainPipeline& terrain, float x, float y, DirectX::XMFLOAT3& point ) const;
	void dabTo( GS::TerrainPaintTool& paint, float radius, float falloff, const DirectX::XMFLOAT3& point );
	void editStreamPoint( const DMCamera& camera, GS::StreamCurves& streams, const GS::TerrainHeightSource& terrain, GUI& gui );
	// Сдвиг выбранной точки так, чтобы она стала в position, — от положений в начале правки (m_dragStart), с мягким выделением
	void dragStreamPoint( GS::StreamCurves& streams, DirectX::XMFLOAT2 position );

	enum class Gizmo
	{
		move,
		rotate,
		scale
	};

	uint32_t m_selectedKey = 0;			// 0 — экземпляр не выбран
	int m_selected = -1;				// его номер в этом кадре
	uint32_t m_selectedLightKey = 0;	// 0 — источник не выбран
	int m_selectedLight = -1;			// его номер в этом кадре
	int m_selectedEmitter = -1;			// эмиттеры не добавляются и не убираются: номер постоянен
	bool m_draggingLight = false;	// значок направленного источника тянут по небу
	bool m_pressedOverScene = false;
	Gizmo m_gizmo = Gizmo::move;
	bool m_localAxes = false;	// оси экземпляра, а не мира (у масштаба — всегда свои)
	bool m_streamMode = false;
	int m_streamCurve = -1;		// выбранная кривая русла и точка (−1 — нет)
	int m_streamPoint = -1;
	bool m_draggingPoint = false;
	// Гизмо экземпляра тянут; свойства экземпляра до кадра, в котором его взяли, — «до» правки в истории (Ctrl+Z)
	bool m_usingGizmo = false;
	std::vector<Property::Value> m_gizmoBefore;
	float m_streamFalloff = 8.0f;
	std::vector<DirectX::XMFLOAT2> m_dragStart;	// точки кривой в начале перетаскивания
	bool m_paintMode = false;
	bool m_painting = false;				// мазок идёт: ЛКМ зажата с начала над сценой
	DirectX::XMFLOAT2 m_lastDab = {};		// последнее касание мазка (x, z): между касаниями — промежуточные с шагом в ¼ радиуса
};
