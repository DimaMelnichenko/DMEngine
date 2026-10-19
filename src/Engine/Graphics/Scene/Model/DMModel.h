#pragma once
//////////////
// INCLUDES //
//////////////
#include "DirectX.h"
#include <vector>
#include <memory>
#include <unordered_set>

#include "..\Texture\DMTextureStorage.h"
#include "Mesh\DMMesh.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

// Модель — ресурс: LOD из секций и дальности переключения. Где стоит модель, задают её экземпляры (ModelInstances,
// SkySphere): одна модель может стоять на уровне в нескольких местах
class DMModel : public DMResource
{
public:
	// Секция LOD — меш со своим материалом, как section / material slot у Static Mesh в UE и primitive у меша glTF:
	// у дерева непрозрачный ствол и Masked хвоя, у постройки несколько материалов. Строка ModelProperties
	struct Section
	{
		uint32_t mesh = 0;
		uint32_t material = 0;
		// Экземпляр материала (MaterialInstance.id_instance, Assets::materialInstances()): его параметры — общие у всех
		// секций с ним; 0 — параметры материала со значениями по умолчанию, свои у секции (ownParams)
		uint32_t instance = 0;
		// Параметры материала: экземпляра или свои; адрес постоянен — на него ссылаются MeshBatch, GUI, расстановка
		PropertyContainer* params = nullptr;
		std::unique_ptr<PropertyContainer> ownParams;
	};

	struct LodBlock
	{
		std::vector<std::unique_ptr<Section>> sections;
		bool isRender = true;
		DirectX::BoundingBox bounds;	// объединение границ мешей секций, в пространстве модели
		DirectX::BoundingBox baseBounds;	// то же для границ основания (AbstractMesh::baseBounds)
	};
public:
	DMModel( uint32_t id, const std::string& name = "" );
	DMModel( DMModel&& );
	DMModel& operator=( DMModel&& );
	~DMModel();

	void addLod( float range, std::unique_ptr<LodBlock>&& lod );
	// LOD для расстояния до камеры: первый, чья дальность не меньше; дальше последнего — nullptr
	const LodBlock* getLod( float distance ) const;
	// Номер LOD для расстояния или −1, если дальше последнего
	int lodIndex( float distance ) const;
	LodBlock* getLodById( uint16_t index );
	const LodBlock* getLodById( uint16_t index ) const;
	// Дальность LOD index, м: до неё включительно рисуется этот LOD (ModelProperties.range)
	float lodRange( uint16_t index ) const;
	uint16_t lodCount() const;
	// Множитель дальностей LOD у видов теней (Models.shadow_lod_scale, свойство «Shadow LOD scale» окна «Models»): < 1 —
	// тень более грубым LOD ближе, чем в кадре. Тень ближнего экземпляра — от той же геометрии, что в кадре (он сам себя
	// затеняет), дальнего — грубее: у ели LOD0 втрое тяжелее LOD1, а его мелкие карточки хвои в тени не видны
	float shadowLodScale() const;
	void setShadowLodScale( float scale );
	// LOD у видов теней не детальнее этого (Models.shadow_min_lod, свойство «Shadow min LOD»; 0 — как в кадре): тень
	// травы — её самым грубым LOD и у ближних пучков, мелких травинок в тени не видно. Не дальше последнего LOD
	uint32_t shadowMinLod() const;
	void setShadowMinLod( uint32_t lod );

	// Простая коллизия модели в мире столкновений (Models.collision, как Simple Collision у Static Mesh в UE;
	// docs/physics.md) — по LOD 0: выпуклая оболочка вершин (по умолчанию: камни, шары), коробка по границам, треугольники
	// (неподвижное сложной формы), ствол — вертикальная капсула по следу вершин у низа модели (деревья), без столкновений
	enum class Collision { hull, box, mesh, capsule, none };
	Collision collision() const { return m_collision; }
	void setCollision( Collision collision ) { m_collision = collision; }
	// Радиус ствола (capsule), м в координатах модели (Models.collision_radius, как радиус капсулы простой коллизии в UE);
	// 0 — полуширина следа вершин у низа модели (TrunkCollision.h: у ели — комель с корнями, шире ствола)
	float collisionRadius() const { return m_collisionRadius; }
	void setCollisionRadius( float radius ) { m_collisionRadius = radius; }
	// Имя колонки → значение; false — неизвестное имя
	static bool parseCollision( const std::string& name, Collision& collision );

	// Скелет модели (Models.skeleton_id — скелет в AnimationLibrary мира, строка Skeletons); 0 — модель без скелета. Меши
	// модели со скелетом лежат в позе покоя, с весами костей (блок SKIN), рисуются вариантом материала SKINNED
	uint32_t skeleton() const { return m_skeleton; }
	void setSkeleton( uint32_t skeleton ) { m_skeleton = skeleton; }
	// Границы модели во всех позах клипов её скелета (AnimationClip::boundsMin / boundsMax), пространство модели: по ним
	// отсекаются экземпляры в анимации (лёжа, в прыжке); у модели без клипов — границы LOD 0
	const DirectX::BoundingBox& animatedBounds() const { return m_animatedBounds; }
	void setAnimatedBounds( const DirectX::BoundingBox& bounds ) { m_animatedBounds = bounds; }

	PropertyContainer* properties();
private:
	std::vector<std::pair<float, std::shared_ptr<LodBlock>>> m_lods;
	Collision m_collision = Collision::hull;
	float m_collisionRadius = 0.0f;
	uint32_t m_skeleton = 0;
	DirectX::BoundingBox m_animatedBounds;
	PropertyContainer m_properties;
};

}