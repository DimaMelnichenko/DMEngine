#pragma once

#include "DirectX.h"
#include "Properties/PropertyContainer.h"
#include "Engine\Simulation\PhysicsWorld.h"

class DMCamera;

namespace GS
{

class World;

// Ходьба от первого лица, как Character Movement в UE: персонаж мира столкновений (PhysicsWorld, Jolt CharacterVirtual) —
// капсула на ногах, глаза на высоте человека. Модели уровня и другие персонажи (сущности мира) преграждают путь, ступенька
// ниже «Step height» — шаг вверх, обрыв глубже — падение, склон круче «Max slope» — не подняться и съезжаешь, вода глубже
// «Max wade depth» не пускает (вдоль берега идёшь). Своё — разгон и торможение (в воздухе почти без управления), прыжок,
// плавный подъём взгляда на ступеньке. Переключается с полёта клавишей F (Engine); положение камеры задаёт до её Update,
// камера сама в это время не двигается (DMCamera::setKeyboardMovement)
class WalkMode
{
public:
	WalkMode();

	bool enabled() const { return m_enabled; }
	// Включение — с места камеры (под землёй — на поверхность, в воздухе — падение до земли, за картой — у её края): свой
	// персонаж в мире world; выключение — персонаж убран, камера летает с того же места
	void setEnabled( bool enabled, DMCamera& camera, World* world );
	// Перенос идущего на место камеры (команда camera): ноги — на высоту глаз ниже, под землёй — на поверхность, за картой —
	// у её края, скорость — ноль
	void teleport( const DMCamera& camera );
	// Шаг ходьбы: seconds — время кадра; ставит положение камеры (направление взгляда — её, по мыши)
	void update( float seconds, DMCamera& camera, World& world );
	// Глаза над ногами, м (рост и запаздывание взгляда на ступеньке); не идём — 0. По нему LOD террейна (RenderView::lodEyeHeight)
	float eyeAboveFeet() const;

	// Окно «Walk»: скорости, рост, радиус, прыжок, гравитация, предел уклона, ступенька, брод
	PropertyContainer* properties() { return &m_properties; }

private:
	// Капсула по окну: высота — глаза + 0,1 м
	PhysicsWorld::CharacterSettings characterSettings() const;
	// Ноги под камерой: на высоту глаз ниже, внутри карты (за краем — стена границы, PhysicsWorld::buildTerrain), под землёй —
	// на поверхности
	DirectX::XMFLOAT3 feetUnder( const DMCamera& camera, const World& world ) const;

	PropertyContainer m_properties;
	bool m_enabled = false;
	World* m_world = nullptr;			// мир, где живёт персонаж
	PhysicsWorld::CharacterId m_character = 0;
	PhysicsWorld::CharacterSettings m_applied;	// настройки персонажа — по ним видно правку окна
	DirectX::XMFLOAT3 m_feet = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// ноги, м мира
	DirectX::XMFLOAT2 m_velocity = DirectX::XMFLOAT2( 0.0f, 0.0f );		// желаемая по горизонтали, м/с
	bool m_onGround = false;
	float m_eyeLift = 0.0f;			// запаздывание взгляда при подъёме на ступеньку, м (уходит к нулю)
	bool m_jumpHeld = false;		// прыжок — по нажатию, а не удержанию
};

}
