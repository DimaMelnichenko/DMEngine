#include "ModelInstances.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include "System.h"
#include "Shaders\lod_transition.h"
#include "Terrain\TerrainHeightSource.h"
#include <cfloat>

using namespace DirectX;

namespace
{

// Поворот кватернионом ↔ углами XMQuaternionRotationRollPitchYaw (сначала крен вокруг Z, затем тангаж вокруг X,
// затем рыскание вокруг Y), градусы
XMFLOAT3 quaternionToAngles( const XMFLOAT4& rotation )
{
	XMFLOAT3X3 m;
	XMStoreFloat3x3( &m, XMMatrixRotationQuaternion( XMLoadFloat4( &rotation ) ) );
	const float pitch = std::asin( std::clamp( -m._32, -1.0f, 1.0f ) );
	float yaw;
	float roll;
	if( std::abs( m._32 ) < 0.9999f )
	{
		yaw = std::atan2( m._31, m._33 );
		roll = std::atan2( m._12, m._22 );
	}
	else
	{
		// Взгляд вертикально: крен и рыскание — один поворот, он весь в рыскании
		yaw = std::atan2( -m._13, m._11 );
		roll = 0.0f;
	}
	// + 0: без «-0» в окне
	return XMFLOAT3( XMConvertToDegrees( pitch ) + 0.0f, XMConvertToDegrees( yaw ) + 0.0f, XMConvertToDegrees( roll ) + 0.0f );
}

XMFLOAT4 anglesToQuaternion( const XMFLOAT3& angles )
{
	XMFLOAT4 rotation;
	XMStoreFloat4( &rotation, XMQuaternionRotationRollPitchYaw( XMConvertToRadians( angles.x ), XMConvertToRadians( angles.y ),
																 XMConvertToRadians( angles.z ) ) );
	return rotation;
}

bool equal( const XMFLOAT3& a, const XMFLOAT3& b )
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

}

namespace GS
{

ModelInstances::ModelInstances() :
	SceneObject( "Models" )
{
	m_properties.setName( "Models" );
	m_instanceProperties.setName( "Model instances" );
	m_addProperties.setName( "Add" );
}

void ModelInstances::initialize( const std::vector<LevelDescription::ModelInstance>& instances )
{
	std::unordered_set<uint32_t> withProperties;
	m_instances.reserve( instances.size() );
	for( const LevelDescription::ModelInstance& description : instances )
	{
		Instance instance;
		instance.model = System::models().get( description.model ).get();
		instance.modelId = description.model;
		instance.id = description.id;
		instance.key = m_nextKey++;
		instance.transform.setPosition( description.position );
		instance.transform.setRotation( description.rotation );
		instance.transform.setScale( description.scale );
		instance.angles = quaternionToAngles( instance.transform.rotation() );

		if( withProperties.insert( description.model ).second )
		{
			m_properties.addSubContainer( instance.model->properties() );
			m_palette.push_back( description.model );
		}
		createProperties( instance );
		m_instances.push_back( std::move( instance ) );
	}
	// Палитра — по имени модели
	std::sort( m_palette.begin(), m_palette.end(), [this]( uint32_t a, uint32_t b ) { return modelName( a ) < modelName( b ); } );
	updateStructure();
}

void ModelInstances::createProperties( Instance& instance )
{
	// Имя с id — updateStructure
	instance.properties = std::make_unique<PropertyContainer>( instance.model->properties()->name() );
	Property* position = instance.properties->insert( "Position", instance.transform.position() );
	// Перетаскивание без границ (low = high), шаг — на пиксель
	position->setControlType( GUIControlType::DRAG );
	position->setLow( 0.0f );
	position->setHigh( 0.0f );
	position->setUnit( "m" )->setDragSpeed( 0.05f );
	Property* rotation = instance.properties->insert( "Rotation", instance.angles );
	rotation->setControlType( GUIControlType::DRAG );
	rotation->setLow( 0.0f );
	rotation->setHigh( 0.0f );
	rotation->setUnit( "deg" )->setDragSpeed( 0.5f )->setTooltip( "Pitch (around X), yaw (around Y), roll (around Z)" );
	Property* scale = instance.properties->insert( "Scale", instance.transform.scale() );
	scale->setControlType( GUIControlType::DRAG );
	scale->setLow( 0.001f );
	scale->setHigh( 1000.0f );
	scale->setDragSpeed( 0.01f );

	const uint32_t key = instance.key;
	instance.properties->addButton( deleteButton, "Remove the instance from the level (Delete in the viewport; Save level deletes its row, Ctrl+Z returns it)",
									[this, key]() -> PropertyContainer::ButtonAction
	{
		const int index = indexOf( key );
		if( index < 0 )
			return {};
		const std::string name = "Delete " + m_instances[index].name;
		removeInstance( key );
		return { [this, key] { restoreInstance( key ); }, [this, key] { removeInstance( key ); }, name };
	} );
}

uint32_t ModelInstances::addInstance( uint32_t model, const XMFLOAT3& position, const TerrainHeightSource* terrain )
{
	Instance instance;
	instance.model = System::models().get( model ).get();
	instance.modelId = model;
	instance.key = m_nextKey++;
	instance.transform.setPosition( position );
	createProperties( instance );
	const uint32_t key = instance.key;
	m_instances.push_back( std::move( instance ) );
	if( terrain )
		snapToTerrain( static_cast<int>( m_instances.size() ) - 1, *terrain );
	// Значения при добавлении — «сохранённые»: Details отмечает правки после них, а сам новый экземпляр — до «Save level»
	// (отметку состава ставит updateStructure)
	m_instances.back().properties->markSaved();
	updateStructure();
	return key;
}

bool ModelInstances::removeInstance( uint32_t key )
{
	const int index = indexOf( key );
	if( index < 0 )
		return false;
	m_removed.push_back( std::move( m_instances[index] ) );
	m_instances.erase( m_instances.begin() + index );
	updateStructure();
	return true;
}

bool ModelInstances::restoreInstance( uint32_t key )
{
	const auto removed = std::find_if( m_removed.begin(), m_removed.end(), [key]( const Instance& instance ) { return instance.key == key; } );
	if( removed == m_removed.end() )
		return false;
	// На прежнее место: список упорядочен по ключу (порядок базы, затем добавленные)
	const auto at = std::find_if( m_instances.begin(), m_instances.end(), [key]( const Instance& instance ) { return instance.key > key; } );
	m_instances.insert( at, std::move( *removed ) );
	m_removed.erase( removed );
	updateStructure();
	return true;
}

uint32_t ModelInstances::key( int instance ) const
{
	return instance >= 0 && instance < static_cast<int>( m_instances.size() ) ? m_instances[instance].key : 0;
}

int ModelInstances::indexOf( uint32_t key ) const
{
	for( size_t i = 0; i < m_instances.size(); ++i )
	{
		if( key != 0 && m_instances[i].key == key )
			return static_cast<int>( i );
	}
	return -1;
}

const std::string& ModelInstances::modelName( uint32_t model ) const
{
	return System::models().get( model )->properties()->name();
}

std::vector<uint32_t> ModelInstances::removedIds() const
{
	std::vector<uint32_t> ids;
	for( const Instance& instance : m_removed )
	{
		if( instance.id != 0 )
			ids.push_back( instance.id );
	}
	return ids;
}

void ModelInstances::markSaved( const std::vector<LevelDescription::ModelInstance>& saved )
{
	for( size_t i = 0; i < m_instances.size() && i < saved.size(); ++i )
		m_instances[i].id = saved[i].id;
	// Строк убранных больше нет: вернёт отмена — вставится заново
	for( Instance& instance : m_removed )
		instance.id = 0;
	updateStructure();
}

void ModelInstances::updateStructure()
{
	m_instanceProperties.subContainer().clear();
	m_instanceProperties.addSubContainer( &m_addProperties );
	for( Instance& instance : m_instances )
	{
		// Нового в базе ещё нет: вместо id — «new» и ключ (имя различает подокна в пути команд set / press)
		instance.name = ( instance.id != 0 ? std::to_string( instance.id ) : "new " + std::to_string( instance.key ) ) + ": " +
						modelName( instance.modelId );
		instance.properties->setName( instance.name );
		instance.properties->setStructureModified( instance.id == 0 );
		m_instanceProperties.addSubContainer( instance.properties.get() );
	}
	// Убранный экземпляр ещё есть в базе — окно изменено до «Save level»
	m_instanceProperties.setStructureModified( std::any_of( m_removed.begin(), m_removed.end(), []( const Instance& instance ) { return instance.id != 0; } ) );
	updateBounds();
}

void ModelInstances::update( const FrameContext& frame )
{
	bool moved = false;
	for( Instance& instance : m_instances )
	{
		const PropertyContainer& properties = *instance.properties;
		const XMFLOAT3& position = properties["Position"].data<XMFLOAT3>();
		const XMFLOAT3& angles = properties["Rotation"].data<XMFLOAT3>();
		const XMFLOAT3& scale = properties["Scale"].data<XMFLOAT3>();
		if( !equal( position, instance.transform.position() ) )
		{
			instance.transform.setPosition( position );
			moved = true;
		}
		if( !equal( angles, instance.angles ) )
		{
			instance.angles = angles;
			instance.transform.setRotation( anglesToQuaternion( angles ) );
			moved = true;
		}
		if( !equal( scale, instance.transform.scale() ) )
		{
			instance.transform.setScale( scale );
			moved = true;
		}
	}
	// Границы — для ближней и дальней плоскости каскадов теней (Scene::bounds)
	if( moved )
		updateBounds();
}

void ModelInstances::updateBounds()
{
	bool first = true;
	for( const Instance& instance : m_instances )
	{
		const DMModel::LodBlock* lod = instance.model->getLodById( 0 );
		if( !lod )
			continue;
		DirectX::BoundingBox meshBounds;
		lod->bounds.Transform( meshBounds, instance.transform.worldMatrix() );
		if( first )
			m_bounds = meshBounds;
		else
			DirectX::BoundingBox::CreateMerged( m_bounds, m_bounds, meshBounds );
		first = false;
	}
}

void ModelInstances::setInstanceMatrix( int instance, FXMMATRIX matrix )
{
	XMVECTOR scale;
	XMVECTOR rotation;
	XMVECTOR translation;
	if( !XMMatrixDecompose( &scale, &rotation, &translation, matrix ) )
		return;
	XMFLOAT3 position;
	XMFLOAT3 size;
	XMFLOAT4 quaternion;
	XMStoreFloat3( &position, translation );
	XMStoreFloat3( &size, scale );
	XMStoreFloat4( &quaternion, rotation );
	PropertyContainer& properties = *m_instances[instance].properties;
	properties["Position"].setData( position );
	properties["Scale"].setData( size );
	// Поворот не менялся (перемещение, масштаб) — углы окна как есть: перевод туда и обратно сдвинул бы их на ±0,0001
	const XMVECTOR current = XMLoadFloat4( &m_instances[instance].transform.rotation() );
	if( std::abs( XMVectorGetX( XMVector4Dot( current, rotation ) ) ) < 0.999999f )
		properties["Rotation"].setData( quaternionToAngles( quaternion ) );
}

size_t ModelInstances::reseat( const std::function<float( float x, float z, float radius )>& shift )
{
	size_t moved = 0;
	for( Instance& instance : m_instances )
	{
		// Размер модели — по масштабу экземпляра (примитивы и тестовые модели — около метра), не меньше метра
		const XMFLOAT3& scale = instance.transform.scale();
		const float radius = std::max( 0.5f * std::max( std::abs( scale.x ), std::abs( scale.z ) ), 1.0f );
		XMFLOAT3 position = instance.properties->property( "Position" ).data<XMFLOAT3>();
		const float dy = shift( position.x, position.z, radius );
		if( std::abs( dy ) < 1e-3f )
			continue;
		position.y += dy;
		instance.properties->property( "Position" ).setData( position );
		++moved;
	}
	return moved;
}

bool ModelInstances::snapToTerrain( int instance, const TerrainHeightSource& terrain )
{
	DirectX::BoundingOrientedBox box;
	if( instance < 0 || instance >= static_cast<int>( m_instances.size() ) || !instanceBox( instance, box ) )
		return false;
	XMFLOAT3 corners[DirectX::BoundingOrientedBox::CORNER_COUNT];
	box.GetCorners( corners );
	XMFLOAT3 low = corners[0];
	XMFLOAT3 high = corners[0];
	for( const XMFLOAT3& corner : corners )
	{
		low = XMFLOAT3( std::min( low.x, corner.x ), std::min( low.y, corner.y ), std::min( low.z, corner.z ) );
		high = XMFLOAT3( std::max( high.x, corner.x ), std::max( high.y, corner.y ), std::max( high.z, corner.z ) );
	}
	// Земля под границами — сетка 5 × 5 точек, самая высокая
	float ground = -FLT_MAX;
	for( int i = 0; i < 5; ++i )
	{
		for( int j = 0; j < 5; ++j )
		{
			float height = 0.0f;
			if( terrain.surfaceHeight( low.x + ( high.x - low.x ) * i / 4.0f, low.z + ( high.z - low.z ) * j / 4.0f, height ) )
				ground = std::max( ground, height );
		}
	}
	if( ground == -FLT_MAX )
		return false;
	XMFLOAT3 position = m_instances[instance].properties->property( "Position" ).data<XMFLOAT3>();
	position.y += ground - low.y;
	m_instances[instance].properties->property( "Position" ).setData( position );
	return true;
}

bool ModelInstances::instanceBox( int instance, DirectX::BoundingOrientedBox& box ) const
{
	const DMModel::LodBlock* lod = m_instances[instance].model->getLodById( 0 );
	if( !lod )
		return false;
	DirectX::BoundingOrientedBox local;
	DirectX::BoundingOrientedBox::CreateFromBoundingBox( local, lod->bounds );
	local.Transform( box, m_instances[instance].transform.worldMatrix() );
	return true;
}

int ModelInstances::pick( FXMVECTOR origin, FXMVECTOR direction, float& distance ) const
{
	int nearest = -1;
	for( int i = 0; i < static_cast<int>( m_instances.size() ); ++i )
	{
		DirectX::BoundingOrientedBox box;
		float hit = 0.0f;
		if( instanceBox( i, box ) && box.Intersects( origin, direction, hit ) && ( nearest < 0 || hit < distance ) )
		{
			nearest = i;
			distance = hit;
		}
	}
	return nearest;
}

std::vector<LevelDescription::ModelInstance> ModelInstances::instances() const
{
	std::vector<LevelDescription::ModelInstance> result;
	for( const Instance& instance : m_instances )
	{
		LevelDescription::ModelInstance description;
		description.id = instance.id;
		description.model = instance.modelId;
		description.position = instance.transform.position();
		description.rotation = instance.transform.rotation();
		description.scale = instance.transform.scale();
		result.push_back( description );
	}
	return result;
}

bool ModelInstances::bounds( DirectX::BoundingBox& bounds ) const
{
	if( m_instances.empty() )
		return false;
	bounds = m_bounds;
	return true;
}

void ModelInstances::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	const XMVECTOR lodOrigin = XMLoadFloat3( &view.lodOrigin );
	const XMVECTOR viewPosition = XMLoadFloat3( &view.position );

	for( const Instance& instance : m_instances )
	{
		const XMVECTOR position = XMLoadFloat3( &instance.transform.position() );
		const float lodDistance = XMVectorGetX( XMVector3Length( position - lodOrigin ) );
		const float distance = XMVectorGetX( XMVector3Length( position - viewPosition ) );

		// LOD i — до своей дальности; в полосе дальность × (1 ± LOD_TRANSITION_WIDTH / 2) — вместе со следующим (за
		// последним — с пустотой), t — доля перехода от начала полосы. У видов теней дальности моделей с «Shadow LOD
		// scale» < 1 ближе (DMModel::shadowLodScale): тень дальних — более грубым LOD; переход у каждого вида свой. LOD
		// детальнее «Shadow min LOD» у видов теней нет (DMModel::shadowMinLod)
		const uint16_t lodCount = instance.model->lodCount();
		const float lodScale = view.index > 0 ? instance.model->shadowLodScale() : 1.0f;
		const uint16_t firstLod = view.index > 0 ? static_cast<uint16_t>( std::min<uint32_t>( instance.model->shadowMinLod(), lodCount ) ) : 0;
		for( uint16_t i = firstLod; i < lodCount; ++i )
		{
			// Дальность последнего LOD — конец модели: её множитель не сдвигает
			const float range = instance.model->lodRange( i ) * ( i + 1 < lodCount ? lodScale : 1.0f );
			const float halfBand = range * LOD_TRANSITION_WIDTH * 0.5f;
			if( lodDistance >= range + halfBand )
				continue;
			const float t = halfBand > 0.0f ? std::clamp( ( lodDistance - ( range - halfBand ) ) / ( 2.0f * halfBand ), 0.0f, 1.0f ) : 0.0f;
			addLod( instance, i, t, distance, view, collector );
			if( t > 0.0f && i + 1 < lodCount )
				addLod( instance, static_cast<uint16_t>( i + 1 ), t - 1.0f, distance, view, collector );
			break;
		}
	}
}

void ModelInstances::addLod( const Instance& instance, uint16_t lodIndex, float lodDither, float distance,
							 const RenderView& view, MeshCollector& collector ) const
{
	const DMModel::LodBlock* lod = instance.model->getLodById( lodIndex );
	if( !lod || !lod->isRender )
		return;

	// Отсечение по frustum вида: границы LOD (все секции), переведённые мировой матрицей экземпляра
	DirectX::BoundingBox bounds;
	lod->bounds.Transform( bounds, instance.transform.worldMatrix() );
	const XMFLOAT3 boundsMin( bounds.Center.x - bounds.Extents.x, bounds.Center.y - bounds.Extents.y,
							  bounds.Center.z - bounds.Extents.z );
	const XMFLOAT3 boundsMax( bounds.Center.x + bounds.Extents.x, bounds.Center.y + bounds.Extents.y,
							  bounds.Center.z + bounds.Extents.z );
	if( !view.frustum.checkBox( boundsMin, boundsMax ) )
		return;

	// Элемент отрисовки — секция: у каждой свой меш и материал, проход — по режиму её материала
	for( size_t i = 0; i < lod->sections.size(); ++i )
	{
		const DMModel::Section& section = *lod->sections[i];
		MeshBatch batch;
		batch.material = System::materials().get( section.material ).get();
		// Режим читается каждый кадр: параметры материала меняются в GUI
		batch.state = batch.material->renderState( section.params );
		if( lodDither != 0.0f && !batch.state.ditheredLodTransition )
		{
			// Без дизеринга — та сторона перехода, которой в полосе больше: смена ровно на дальности LOD
			const bool outgoing = lodDither > 0.0f;
			if( outgoing ? lodDither > 0.5f : lodDither + 1.0f <= 0.5f )
				continue;
			batch.lodDither = 0.0f;
		}
		else
		{
			batch.lodDither = lodDither;
		}

		const auto& mesh = System::meshes().get( section.mesh );
		batch.materialId = section.material;
		batch.params = &section.params;
		batch.indexCount = mesh->indexCount();
		batch.indexOffset = mesh->indexOffset();
		batch.vertexOffset = mesh->vertexOffset();
		batch.world = instance.transform.worldMatrix();
		batch.distance = distance;
		// «Эта секция этого LOD этой модели»: одинаковые у соседних экземпляров рисуются одним вызовом
		batch.instanceGroup = ( instance.modelId << 8 ) | ( static_cast<uint32_t>( lodIndex ) << 4 ) | static_cast<uint32_t>( i );
		collector.add( batch );
	}
}

PropertyContainer* ModelInstances::properties()
{
	return &m_properties;
}

}
