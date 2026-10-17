#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <DirectXMath.h>

namespace GS
{

class TerrainHeightSource;

// Столкновения мира симуляции на Jolt Physics (как Chaos у World в UE; docs/physics.md) — служба мира (World::physics):
// неподвижные тела (рельеф, модели уровня) и персонажи (JPH::CharacterVirtual: капсула, ступеньки, предел уклона).
// Фасад без типов Jolt — заголовки Jolt только в PhysicsWorld.cpp, как D3D12 за DMD3D. Динамических тел пока нет, поэтому
// шага PhysicsSystem::Update нет: персонажи двигаются запросами к неподвижному миру (moveCharacter)
class PhysicsWorld
{
public:
	using BodyId = uint32_t;		// 0 — нет тела
	using CharacterId = uint32_t;	// 0 — нет персонажа

	PhysicsWorld();
	~PhysicsWorld();
	PhysicsWorld( const PhysicsWorld& ) = delete;
	PhysicsWorld& operator=( const PhysicsWorld& ) = delete;

	// Земля — те же треугольники, что видит surfaceHeight: главная карта высот — выборки в узлах текселя карты
	// (TerrainHeight::worldSize / mapSize), под квадратами детальной земли (fineRegions: плитки у русел) — дыры, сами квадраты —
	// свои карты высот с их шагом. Заново — после новой итоговой карты (TerrainHeight::revision). Вода (waterLevel) — оттуда же.
	// false — у рельефа нет высот на CPU
	bool buildTerrain( const TerrainHeightSource& terrain );
	// Номер итоговой карты, по которой построена земля; ~0 — земли нет
	uint32_t terrainRevision() const;

	// Неподвижные тела: форма — в пространстве модели, world — мировая матрица (масштаб, в том числе неравномерный,
	// запекается в форму). 0 — форма не построилась (вырожденная: плоская оболочка — коробкой)
	BodyId addStaticBox( const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& halfExtents, DirectX::FXMMATRIX world );
	BodyId addStaticHull( const std::vector<DirectX::XMFLOAT3>& points, DirectX::FXMMATRIX world );
	BodyId addStaticMesh( const std::vector<DirectX::XMFLOAT3>& vertices, const std::vector<uint32_t>& indices, DirectX::FXMMATRIX world );
	// Вертикальная капсула (ствол): низ — base, высота height (с полусферами), радиус radius
	BodyId addStaticCapsule( const DirectX::XMFLOAT3& base, float height, float radius, DirectX::FXMMATRIX world );
	void removeBody( BodyId body );
	// Неподвижных тел, без земли
	size_t bodyCount() const;
	// После загрузки многих тел — перестроить дерево широкой фазы (быстрее запросы)
	void optimize();

	// Луч от origin на direction (длина — дальность): доля пути до ближайшего попадания; false — мимо
	bool castRay( const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float& fraction ) const;

	// Персонаж — капсула, стоящая на точке ног
	struct CharacterSettings
	{
		float radius = 0.3f;		// м
		float height = 1.8f;		// м, с полусферами
		float maxSlope = 40.0f;		// градусы: круче — не подняться, съезжаешь
		float stepHeight = 0.4f;	// м: выше — стена, ниже — шагнуть вверх; обрыв глубже — падение
		float maxWadeDepth = 1.0f;	// м: глубже вода не пускает (0 — вода не мешает)
	};
	struct CharacterMove
	{
		DirectX::XMFLOAT2 velocity = DirectX::XMFLOAT2( 0.0f, 0.0f );	// желаемая скорость по горизонтали (x, z), м/с
		float jumpSpeed = 0.0f;		// > 0 — прыжок, если стоит на земле
		float gravity = 9.81f;		// м/с²
	};
	struct CharacterState
	{
		DirectX::XMFLOAT3 feet = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
		DirectX::XMFLOAT3 velocity = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
		bool onGround = false;		// стоит на пологом (не круче предела)
	};
	CharacterId createCharacter( const CharacterSettings& settings, const DirectX::XMFLOAT3& feet );
	// Новые настройки (окно «Walk»): капсула заново на том же месте
	void setCharacterSettings( CharacterId character, const CharacterSettings& settings );
	// Шаг персонажа: на земле — скорость земли + желаемая, в воздухе — своя вертикаль, гравитация; ступеньки вверх и вниз до
	// stepHeight, склон круче предела съезжаешь, к воде глубже maxWadeDepth не идёшь (вдоль берега — можно)
	CharacterState moveCharacter( CharacterId character, const CharacterMove& move, float seconds );
	// Перенести (телепорт): скорость — ноль
	void setCharacterPosition( CharacterId character, const DirectX::XMFLOAT3& feet );
	CharacterState characterState( CharacterId character ) const;
	void removeCharacter( CharacterId character );
	size_t characterCount() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

}
