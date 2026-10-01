#pragma once
#include <optional>
#include "DirectX.h"
#include "Properties\PropertyContainer.h"
#include "ConstantBuffers.h"
#include "Level\LevelSettings.h"

namespace GS
{

// Ветер уровня — как Wind Directional Source в UE: одно поле ветра для всей растительности — направление, сила
// и порывы, бегущие по миру волнами (волны по лугу). Изгиб считает вершинный шейдер (Shaders/wind.sh) у материалов
// с WindWeight > 0; константы — в кадре (b0, ConstantBuffers::FrameParameters::wind). Настройки — строка Wind,
// на которую ссылается уровень (Levels.wind), окно GUI «Wind»; без строки — ветра нет
class Wind
{
public:
	// Строка Wind (Level/LevelSettings.h)
	using Settings = WindSettings;

	Wind();
	void initialize( const std::optional<Settings>& settings );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Константы кадра по текущим значениям GUI; сила 0 — растения неподвижны
	WindParameters parameters();
	// Для сравнения кадров с одной точки (-nowind): растения неподвижны
	void disable();
	PropertyContainer* properties();

private:
	PropertyContainer m_properties;
};

}
