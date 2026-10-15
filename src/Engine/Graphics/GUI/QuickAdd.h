#pragma once

#include "DirectX.h"
#include "Properties/PropertyContainer.h"
#include "Light\DMLight.h"

class DMCamera;
class GUI;
class Viewport;

namespace GS
{
class Scene;
}

// Добавление нового перед камерой, как Quick Add в UE: кнопки «Add … light» окна «Lights» (новый источник) и подокна
// «Add» окна «Model instances» (экземпляр модели палитры) — на месте, куда смотрит камера, выбрано во вьюпорте и раскрыто в
// Details; правка — в истории (Ctrl+Z убирает, Ctrl+Y возвращает)
class QuickAdd
{
public:
	// Кнопки окон; camera, scene, viewport и gui живут дольше кнопок
	void initialize( DMCamera& camera, GS::Scene& scene, Viewport& viewport, GUI& gui );

private:
	// Место перед камерой: там, где центр взгляда упирается в землю (ближе 200 м), иначе в 10 м по взгляду; true — на земле
	bool placementInFront( DirectX::XMFLOAT3& position ) const;
	// Новый источник перед камерой: точечный — не ниже 1,5 м над землёй, прожектор — 3 м и светит вниз; направленный — под
	// 45° вниз в сторону взгляда. Отмена — убрать его
	PropertyContainer::ButtonAction addLight( DMLight::LightType type );
	// Экземпляр модели палитры перед камерой низом границ LOD 0 на землю, как End; без поворота, масштаб 1
	PropertyContainer::ButtonAction addModelInstance( uint32_t model );

	DMCamera* m_camera = nullptr;
	GS::Scene* m_scene = nullptr;
	Viewport* m_viewport = nullptr;
	GUI* m_gui = nullptr;
};
