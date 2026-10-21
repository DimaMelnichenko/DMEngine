#pragma once

#include <cstdint>
#include <vector>

namespace GS
{

// Мелкая вода на сетке высот — эталон на CPU для окна у камеры (docs/water.md, «Мелкая вода у камеры»; на GPU — тот же
// шаг в шейдере). Уравнения мелкой воды: высота воды h переменна, давление гидростатическое (его даёт наклон поверхности),
// поэтому решателя давления нет — волны, бугор перед камнем, гидравлический прыжок и отражение от бортов получаются сами.
// Сетка MAC, как у Chentanez, Müller 2010 («Real-Time Simulation of Large Bodies of Water with Small Scale Details»): h и дно
// b в центрах ячеек, скорость u по X — на вертикальных гранях, w по Z — на горизонтальных; схема — явная, сохраняющая импульс
// (Stelling, Duinmeijer 2003, «A staggered conservative scheme for every Froude number in rapidly varied shallow water
// flows»: прыжок встаёт на сопряжённых глубинах, фронт по сухому дну бежит со своей скоростью); шаг:
//   1. расход через грани h̄ · u, h̄ — глубина ячейки, откуда течёт вода; поток из ячейки — не больше воды в ней (h не уходит
//      ниже нуля, объём сохраняется);
//   2. высота — по расходам;
//   3. скорость — перенос импульса по расходам вокруг грани, сила тяжести по наклону поверхности h + b (озеро на неровном
//      дне стоит: наклон поверхности — 0), трение по Маннингу неявно; грань, откуда течь нечему (сухая ячейка выше по
//      течению), — без скорости.
// Шаг кадра делится на подшаги по скорости волн √(g h) + |u| (maxCourant ячейки за подшаг). Ячейки и грани можно закрепить —
// их h и скорость шаг не меняет (вход и выход ручья, край окна)
class ShallowWater
{
public:
	struct Settings
	{
		float cell = 0.0625f;		// сторона ячейки, м
		float gravity = 9.81f;		// м/с²
		float manning = 0.0f;		// шероховатость дна, с/м^(1/3); 0 — без трения
		float minDepth = 1e-4f;		// мельче — сухо: течь неоткуда, м
		float maxCourant = 0.4f;	// волна и течение за подшаг — не дальше этой доли ячейки
		float maxSpeed = 8.0f;		// скорость грани не больше, м/с
		uint32_t maxSubsteps = 32;
	};

	ShallowWater( uint32_t width, uint32_t height, const Settings& settings );

	uint32_t width() const { return m_width; }
	uint32_t height() const { return m_height; }
	const Settings& settings() const { return m_settings; }

	// Ячейка (x, z): дно, м, глубина, м, закреплена ли (h не меняется)
	float& bed( uint32_t x, uint32_t z ) { return m_bed[cellIndex( x, z )]; }
	float bed( uint32_t x, uint32_t z ) const { return m_bed[cellIndex( x, z )]; }
	float& depth( uint32_t x, uint32_t z ) { return m_depth[cellIndex( x, z )]; }
	float depth( uint32_t x, uint32_t z ) const { return m_depth[cellIndex( x, z )]; }
	float surface( uint32_t x, uint32_t z ) const { return bed( x, z ) + depth( x, z ); }
	void setCellFixed( uint32_t x, uint32_t z, bool fixed ) { m_cellFixed[cellIndex( x, z )] = fixed ? 1 : 0; }

	// Грань x ∈ 0…width слева от ячейки x (строка z) — скорость по X; грань z ∈ 0…height снизу от ячейки z (столбец x) —
	// скорость по Z, м/с. Внешние грани — стенки (скорость 0), если не закреплены
	float& velocityX( uint32_t x, uint32_t z ) { return m_u[faceXIndex( x, z )]; }
	float velocityX( uint32_t x, uint32_t z ) const { return m_u[faceXIndex( x, z )]; }
	float& velocityZ( uint32_t x, uint32_t z ) { return m_w[faceZIndex( x, z )]; }
	float velocityZ( uint32_t x, uint32_t z ) const { return m_w[faceZIndex( x, z )]; }
	void setFaceXFixed( uint32_t x, uint32_t z, bool fixed ) { m_uFixed[faceXIndex( x, z )] = fixed ? 1 : 0; }
	void setFaceZFixed( uint32_t x, uint32_t z, bool fixed ) { m_wFixed[faceZIndex( x, z )] = fixed ? 1 : 0; }

	// Шаг на время dt, с, подшагами по устойчивости; ответ — число подшагов
	uint32_t step( float dt );
	// Наибольший устойчивый подшаг сейчас, с
	float stableTimeStep() const;
	// Объём воды в незакреплённых ячейках, м³
	double volume() const;

private:
	size_t cellIndex( uint32_t x, uint32_t z ) const { return static_cast<size_t>( z ) * m_width + x; }
	size_t faceXIndex( uint32_t x, uint32_t z ) const { return static_cast<size_t>( z ) * ( m_width + 1 ) + x; }
	size_t faceZIndex( uint32_t x, uint32_t z ) const { return static_cast<size_t>( z ) * m_width + x; }

	void substep( float dt );
	void computeFluxes( float dt );
	void updateDepth( float dt );
	void updateVelocity( float dt );

	uint32_t m_width;
	uint32_t m_height;
	Settings m_settings;
	std::vector<float> m_bed, m_depth;
	std::vector<float> m_u, m_w;
	std::vector<float> m_uNext, m_wNext;
	std::vector<float> m_qx, m_qz;		// расход через грани за подшаг, м²/с
	std::vector<uint8_t> m_cellFixed, m_uFixed, m_wFixed;
	std::vector<float> m_outflowScale;
};

}
