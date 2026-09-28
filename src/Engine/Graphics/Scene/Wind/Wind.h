#pragma once
#include <optional>
#include "DirectX.h"
#include "Properties\PropertyContainer.h"
#include "Shaders\ConstantBuffers.h"

namespace GS
{

// Ветер уровня — как Wind Directional Source в UE: одно поле ветра для всей растительности — направление, сила
// и порывы, бегущие по миру волнами (волны по лугу). Изгиб считает вершинный шейдер (Shaders/wind.sh) у материалов
// с WindWeight > 0; константы — в кадре (b0, ConstantBuffers::FrameParameters::wind). Настройки — строка Wind,
// на которую ссылается уровень (Levels.wind), окно GUI «Wind»; без строки — ветра нет
class Wind
{
public:
	struct Settings
	{
		XMFLOAT3 direction = XMFLOAT3( 0.0f, 0.0f, 1.0f );	// куда дует, горизонтально (y не учитывается)
		float strength = 0.0f;			// сила изгиба: сдвиг верха растения высотой h — strength · WindWeight · порыв · h²
		float speed = 4.0f;				// скорость волн порывов, м/с
		float minGustAmount = 0.3f;		// порыв между волнами и на гребне волны — доли силы (Min / Max Gust Amount в UE)
		float maxGustAmount = 1.0f;
		float gustSize = 25.0f;			// длина волны порыва, м
	};

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
