#pragma once

#include "DirectX.h"
#include "Properties/PropertyContainer.h"

class DMCamera;

namespace GS
{

class TerrainHeightSource;

// Ходьба по рельефу от первого лица, как Character Movement в UE, но без физики: глаза на высоте человека над
// поверхностью террейна (TerrainHeightSource::surfaceHeight — те же треугольники, что рисует LOD 0), шаг и бег, прыжок и
// падение с гравитацией (включение в воздухе — падение до земли), склон круче предела вверх не пускает и спускает вниз,
// обрыв глубже «Step height» — падение, подъём взгляда на склоне сглажен. Препятствий нет: модели, деревья и камни
// проходятся насквозь (столкновения — Jolt, этап 14), вода не мешает — идёшь по дну. Переключается с полёта клавишей F
// (DMGraphics); положение камеры задаёт до её Update, камера сама в это время не двигается (DMCamera::setKeyboardMovement)
class WalkMode
{
public:
	WalkMode();

	bool enabled() const { return m_enabled; }
	// Включение — с места камеры, в воздухе — падение до земли; выключение — камера летает с того же места
	void setEnabled( bool enabled, DMCamera& camera, const TerrainHeightSource* terrain );
	// Шаг ходьбы: seconds — время кадра; ставит положение камеры (направление взгляда — её, по мыши)
	void update( float seconds, DMCamera& camera, const TerrainHeightSource& terrain );

	// Окно «Walk»: скорости, рост, прыжок, гравитация, предел уклона, ступенька
	PropertyContainer* properties() { return &m_properties; }

private:
	// Можно ли шагнуть с высоты from на точку to (по высоте на CPU): не круче предела вверх
	bool canStep( float fromHeight, const DirectX::XMFLOAT2& to, float distance, const TerrainHeightSource& terrain, float& toHeight ) const;
	// Нормаль поверхности по разностям высот через шаг сетки
	DirectX::XMFLOAT3 surfaceNormal( float x, float z, const TerrainHeightSource& terrain ) const;

	PropertyContainer m_properties;
	bool m_enabled = false;
	DirectX::XMFLOAT3 m_feet = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// ноги, м мира
	DirectX::XMFLOAT2 m_velocity = DirectX::XMFLOAT2( 0.0f, 0.0f );		// по горизонтали, м/с
	float m_verticalSpeed = 0.0f;
	bool m_onGround = true;
	float m_eyeLift = 0.0f;			// запаздывание взгляда при подъёме на ступеньку, м (уходит к нулю)
	bool m_jumpHeld = false;		// прыжок — по нажатию, а не удержанию
};

}
