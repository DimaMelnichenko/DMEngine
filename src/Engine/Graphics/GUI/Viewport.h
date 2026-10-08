#pragma once

class DMCamera;
class GUI;

namespace GS
{
class ModelInstances;
}

// Вьюпорт редактора, как в UE: клик по сцене выбирает экземпляр модели (луч из камеры к повёрнутым границам LOD 0
// экземпляров, ближайший), выбранный обведён рамкой, его подокно раскрывается в Details (окно «Model instances»); у
// выбранного — гизмо перемещения, поворота и масштаба (ImGuizmo), правит те же свойства окна. Режим гизмо — кнопки над
// сценой и клавиши 5 / 6 / 7, оси — мира или экземпляра. Вызывается в кадре ImGui — между GUI::Begin и GUI::End
class Viewport
{
public:
	void update( const DMCamera& camera, GS::ModelInstances& models, GUI& gui );
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
};
