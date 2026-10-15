#pragma once

#include <string>
#include "TerrainPaint.h"
#include "Properties\PropertyContainer.h"

class ShaderView;

namespace GS
{

class TerrainPipeline;

// Кисть слоёв земли и масок — инструмент редактора над данными мазков конвейера рельефа (TerrainPipeline: плитки поправок,
// маски и слои на GPU, раскладка леса после мазка). Подокно «Paint» окна террейна (цель, режим, значение, радиус, мягкость,
// показ цели; с уровнем не сохраняется), мазок со снимками плиток цели до и после — для отмены (вьюпорт, EditHistory),
// мазок из одного касания (команда terrain paint) и цель кисти поверх земли для отрисовки рельефа (режим «Terrain paint»)
class TerrainPaintTool
{
public:
	// После TerrainPipeline::initialize: цели — по его слоям и маскам
	void initialize( TerrainPipeline& pipeline );
	// Подокно «Paint» — часть окна террейна (CDLODTerrain::initialize)
	PropertyContainer* properties() { return &m_properties; }

	// Кисть подокна «Paint»: цель, режим (Shift — убавить в режиме «Step», Ctrl — стереть), значение, радиус, мягкость
	struct Brush
	{
		std::string target;
		TerrainPaint::Mode mode = TerrainPaint::Mode::value;
		float value = 1.0f;
		float radius = 10.0f;
		float falloff = 0.5f;
	};
	Brush brush( bool shift, bool ctrl ) const;
	// Режим кисти во вьюпорте (View → «Terrain paint»): пока включён, цель кисти видна поверх земли, если в подокне «Paint»
	// отмечено «Show target»
	void setPreview( bool enabled ) { m_preview = enabled; }
	// Цель кисти поверх земли для отрисовки: −1 — нет, 0…7 — слой земли, 8 — мокрость, 9 — маска (mask — её текстура)
	int32_t previewTarget( const ShaderView*& mask ) const;

	// Мазок по частям (вьюпорт): начало, касания (TerrainPipeline::paintDab), конец — снимки плиток цели до и после
	bool beginStroke( const std::string& target, TerrainPaint::Mode mode, float value, std::string& reason );
	void dab( float x, float z, float radius, float falloff );
	struct Edit
	{
		std::string target;
		TerrainPaint::TileMap before;
		TerrainPaint::TileMap after;
		bool changed = false;
	};
	Edit endStroke();
	bool painting() const;
	// Отмена и повтор мазка: плитки цели целиком
	void setTiles( const std::string& target, const TerrainPaint::TileMap& tiles );
	// Мазок из одного касания (команда terrain paint): цель — «layer N», «wetness» или имя маски; false — нет цели или кисть
	// мимо карты (reason)
	bool stroke( const std::string& target, TerrainPaint::Mode mode, float value, float x, float z, float radius, float falloff,
				 std::string& reason );

private:
	TerrainPipeline* m_pipeline = nullptr;
	PropertyContainer m_properties;
	bool m_preview = false;			// режим кисти во вьюпорте: цель кисти поверх земли
	std::string m_target;			// цель идущего мазка
	TerrainPaint::TileMap m_before;	// её плитки до мазка
};

}
