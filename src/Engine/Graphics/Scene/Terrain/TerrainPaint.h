#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "Level\LevelSettings.h"

namespace GS
{

// Ручная правка слоёв земли и масок поверх правил (TerrainMasks) — кисть, как покраска слоёв Landscape в UE, но поправкой:
// у цели (слой земли «layer N» или маска по имени) — плитки tileSize² поправок int8 (−127…127 → −1…1) в текселях масок,
// хранятся только закрашенные (строки TerrainPaint, «Save level»). Правила пересчитали — мазки остаются поправкой к новому
// результату. Маска: итог = правила + поправка (0…1). Слой земли: поправка d > 0 тянет веса к «только этот слой»
// (w = (1 − d)·w + d·eₖ), d < 0 — вес слоя × (1 + d), освободившееся — остальным пропорционально; сумма весов — 1.
// Мазок считается за мазок, а не за время: карта покрытия (наибольший вес кисти в точке за мазок), поправка — от снимка
// до мазка и покрытия (value — итог к значению, step / unstep — ± значение, erase — к нулю), как непрозрачность мазка
// в Photoshop и Krita
class TerrainPaint
{
public:
	static constexpr uint32_t tileSize = 64;
	enum class Mode { value, step, unstep, erase };
	// Плитки цели: (tile x, tile z-строка) → поправки tileSize² — снимки для отмены мазка
	using TileMap = std::map<std::pair<int32_t, int32_t>, std::vector<int8_t>>;
	// Прямоугольник текселей [x0, x1) × [row0, row1); строка 0 — дальний край по z, как у масок
	struct Rect
	{
		uint32_t x0 = 0, row0 = 0, x1 = 0, row1 = 0;
		bool empty() const { return x0 >= x1 || row0 >= row1; }
		void add( const Rect& other );
	};
	// Цель — слой земли («layer N»): номер слоя, иначе −1 — маска
	static int32_t layerOf( const std::string& target );
	static std::string layerTarget( uint32_t layer ) { return "layer " + std::to_string( layer ); }

	// Карта масок: size² текселей по texel м; плитки из базы (другой размер плитки — пропускается, строка в лог)
	void initialize( uint32_t size, float texel, const std::vector<TerrainPaintTile>& tiles );
	bool initialized() const { return m_size != 0; }
	std::vector<TerrainPaintTile> tiles() const;
	// Цели с мазками и число их плиток
	std::vector<std::pair<std::string, size_t>> targets() const;
	bool has( const std::string& target ) const;

	// Мазок: rules — значения правил цели (у слоя земли — его вес, сумма слоёв — 1), по ним value считает поправку
	void beginStroke( const std::string& target, Mode mode, float value, const std::vector<float>* rules );
	// Касание кисти в точке мира (x, z): радиус, м, falloff — доля радиуса с мягким краем; изменённые тексели
	Rect dab( float x, float z, float radius, float falloff );
	// Конец мазка: затронутые тексели всего мазка
	Rect endStroke();
	bool stroking() const { return m_stroke.active; }
	void clear( const std::string& target );
	// Плитки цели целиком — снимок до и после мазка (отмена и повтор: EditHistory)
	TileMap targetTiles( const std::string& target ) const;
	void setTargetTiles( const std::string& target, const TileMap& tiles );

	// Поправка в текселе (−1…1)
	float delta( const std::string& target, uint32_t x, uint32_t row ) const;
	// Итог маски: clamp( правила + поправка ) в прямоугольнике (out — той же раскладки, что rules)
	void applyMask( const std::string& target, const std::vector<float>& rules, std::vector<float>& out, const Rect& rect ) const;
	// Итог слоёв земли: веса splat (по слою; пустой — слоя нет) в прямоугольнике — поправки слоёв по порядку номеров
	void applyLayers( const std::vector<float>* rules, std::vector<float>* out, uint32_t layers, const Rect& rect ) const;
	Rect whole() const { return Rect{ 0, 0, m_size, m_size }; }

	// Итог одной цели от правила и поправки, и обратное — поправка, при которой итог равен value
	static float result( bool layer, float rule, float delta );
	static float deltaFor( bool layer, float rule, float value );

private:
	using Tile = std::vector<int8_t>;
	using Tiles = TileMap;
	const Tile* findTile( const std::string& target, uint32_t x, uint32_t row ) const;
	static float decode( int8_t value ) { return value / 127.0f; }
	static int8_t encode( float value );

	uint32_t m_size = 0;
	float m_texel = 1.0f;
	std::map<std::string, Tiles> m_targets;

	struct Stroke
	{
		bool active = false;
		std::string target;
		Mode mode = Mode::value;
		float value = 0.0f;
		const std::vector<float>* rules = nullptr;
		Tiles before;					// плитки цели до мазка (нет плитки — поправка 0)
		std::vector<float> coverage;	// наибольший вес кисти за мазок, size²
		Rect touched;
	} m_stroke;
};

}
