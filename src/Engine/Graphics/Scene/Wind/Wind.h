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
	// Настройки пресета окружения (на лету) — в окно: направление — Yaw, поле ветра — подокно «Field»
	void apply( const Settings& settings );
	// Константы кадра по текущим значениям GUI; сила 0 — растения неподвижны
	WindParameters parameters();
	// Порыв в точке мира (x, z) в момент time, с: доля силы между gustMin и gustMax — тот же расчёт, что windGust в
	// Shaders/wind_field.sh (звук ветра, AmbientSound, слышит те же волны, что видны на траве)
	static float gust( const WindParameters& parameters, float x, float z, float time );
	// Для сравнения кадров с одной точки (-nowind): растения неподвижны
	void disable();
	PropertyContainer* properties();
	// Флажок «Use field» подокна «Field»: трава, деревья, рябь и частицы берут поле ветра по рельефу (не сохраняется)
	bool fieldEnabled();

private:
	PropertyContainer m_properties;
	PropertyContainer m_fieldProperties;	// подокно «Field» — поле ветра по рельефу (WindFieldSettings, TerrainWind)
};

}
