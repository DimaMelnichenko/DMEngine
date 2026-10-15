#include "Scatterer.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include "D3D\DMD3D.h"
#include "System.h"
#include "ConstantBuffers.h"
#include "Shaders\lod_transition.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Materials\ImpostorMaterial.h"
#include "Terrain\TerrainMasks.h"

using namespace DirectX;

namespace GS
{

Scatterer::Scatterer( const std::string& name, const ScatterSetSettings& settings ) :
	SceneObject( name ),
	m_setId( settings.id )
{
	m_properties.setName( name );
	Property* shadowLod = m_properties.insert( "Shadow LOD scale", settings.shadowLodScale );
	shadowLod->setLow( 0.1f );
	shadowLod->setHigh( 1.0f );
}

bool Scatterer::Initialize()
{
	if( !m_computeShader.Initialize( "Shaders\\scatter.cs", "main" ) )
		return false;

	if( !m_commandShader.Initialize( "Shaders\\scatter.cs", "buildCommands" ) )
		return false;

	return DMD3D::instance().createShaderConstantBuffer( sizeof( TerrainParams ), m_terrainBuffer ) &&
		   DMD3D::instance().createShaderConstantBuffer( sizeof( FrustumParams ), m_frustumBuffer );
}

namespace
{

// Сторона кадра импостера — степень двойки 16…512; в окне — список, значение свойства — номер
const std::vector<std::string> frameSizeItems = { "16", "32", "64", "128", "256", "512" };

uint32_t frameSizeIndex( uint32_t size )
{
	uint32_t index = 0;
	while( index + 1 < frameSizeItems.size() && ( 16u << index ) < size )
		++index;
	return index;
}

uint32_t frameSizeFromIndex( int32_t index )
{
	return 16u << std::clamp( index, 0, static_cast<int32_t>( frameSizeItems.size() ) - 1 );
}

// Настройки запекания импостера слоя («Impostor frame size», «Impostor density», «Impostor occlusion»)
GS::ImpostorMaterial::BakeSettings bakeSettings( const GS::ScatterLayerSettings& settings )
{
	GS::ImpostorMaterial::BakeSettings bake;
	bake.density = settings.impostorDensity;
	bake.occlusion = settings.impostorOcclusion;
	bake.frameSize = settings.impostorFrameSize;
	return bake;
}

// Свойство границы LOD варианта: «LOD 1 up to»
std::string lodRangeName( uint32_t lod )
{
	return "LOD " + std::to_string( lod ) + " up to";
}

// Роли слоёв земли — имена через запятую («grass_dry,grass_wet»); «none» — ни одной
std::vector<std::string> filterRoles( const std::string& filter )
{
	std::vector<std::string> roles;
	size_t begin = 0;
	while( begin <= filter.size() )
	{
		const size_t end = std::min( filter.find( ',', begin ), filter.size() );
		std::string name = filter.substr( begin, end - begin );
		name.erase( 0, name.find_first_not_of( ' ' ) );
		name.erase( name.find_last_not_of( ' ' ) + 1 );
		if( !name.empty() && name != "none" )
			roles.push_back( name );
		begin = end + 1;
	}
	return roles;
}

// Биты слоёв материала террейна по ролям (groundLayers — слой каждой роли, Scatterer::setGroundLayers)
uint32_t roleLayerBits( const std::string& roles, const std::vector<int32_t>& groundLayers )
{
	uint32_t bits = 0;
	for( const std::string& name : filterRoles( roles ) )
	{
		const uint32_t role = static_cast<uint32_t>( terrainLayerRole( name ) );
		if( role < groundLayers.size() && groundLayers[role] >= 0 && groundLayers[role] < 8 )
			bits |= 1u << groundLayers[role];
	}
	return bits;
}

// Флажки ролей слоёв земли, у которых есть слой («Grows on» слоя, «Prefers» вида): включён — роль в списке roles
void addRoleFlags( PropertyContainer& container, const std::vector<int32_t>& groundLayers, const std::vector<std::string>& roles,
				   bool allWhenEmpty, const char* tooltip )
{
	for( uint32_t role = 0; role < groundLayers.size() && role < terrainLayerRoleCount; ++role )
	{
		if( groundLayers[role] < 0 )
			continue;
		const std::string name = terrainLayerRoleName( static_cast<TerrainLayerRole>( role ) );
		const bool on = ( allWhenEmpty && roles.empty() ) || std::find( roles.begin(), roles.end(), name ) != roles.end();
		container.insert( name, on )->setTooltip( tooltip );
	}
}

// Включённые роли флажков через запятую; all — все включены
std::string rolesFromFlags( const PropertyContainer& container, const std::vector<int32_t>& groundLayers, bool& all )
{
	std::string roles;
	all = true;
	for( uint32_t role = 0; role < groundLayers.size() && role < terrainLayerRoleCount; ++role )
	{
		const std::string name = terrainLayerRoleName( static_cast<TerrainLayerRole>( role ) );
		if( groundLayers[role] < 0 || !container.exists( name ) )
			continue;
		if( container[name].data<bool>() )
			roles += ( roles.empty() ? "" : "," ) + name;
		else
			all = false;
	}
	return roles;
}

// Своя раскладка варианта: свойства подокна варианта ↔ ScatterVariantPlacement. Разброс — «Own size variation» и
// значение, наклон — список «As layer / Off / On», группа куртин — 0: своя; предпочтение слоёв земли — подокно «Prefers»
// (флажки ролей, ни одного — нет) и «Ground preference»
void addPlacementProperties( PropertyContainer& properties, std::unique_ptr<PropertyContainer>& prefers, const ScatterVariantPlacement& placement,
							 float layerSizeVariation, const std::vector<int32_t>& groundLayers )
{
	Property* size = properties.insert( "Size", placement.sizeMultiplier );
	size->setLow( 0.05f );
	size->setHigh( 10.0f );
	size->setLogarithmic()->setTooltip( "Size multiplier of this model on top of the layer Size" );
	properties.insert( "Own size variation", placement.sizeVariation >= 0.0f )->setTooltip(
		"Own random size spread of this model; off - the layer Size variation" );
	Property* variation = properties.insert( "Size variation", placement.sizeVariation >= 0.0f ? placement.sizeVariation : layerSizeVariation );
	variation->setLow( 0.0f );
	variation->setHigh( 1.0f );
	variation->setTooltip( "Random size spread of this model (with Own size variation): Size x (1 - variation ... 1)" );
	properties.insert( "Align to terrain", static_cast<int32_t>( placement.alignToTerrain + 1 ) )->setItems( { "As layer", "Off", "On" } )
		->setTooltip( "Instance Y axis along the terrain normal: as the layer, vertical or along the normal" );
	Property* group = properties.insert( "Patch group", placement.patchGroup );
	group->setLow( 0.0f );
	group->setHigh( static_cast<float>( ScatterPass::maxVariants ) );
	group->setTooltip( "Patches (Patch size of the layer): models of one group are one species and grow in the same patches; "
					   "0 - own patches" );
	if( std::any_of( groundLayers.begin(), groundLayers.end(), []( int32_t layer ) { return layer >= 0; } ) )
	{
		Property* preference = properties.insert( "Ground preference", placement.groundPreference );
		preference->setLow( 0.0f );
		preference->setHigh( 1.0f );
		preference->setTooltip( "How strongly the model keeps to its Prefers terrain layers: weight x (1 - preference + preference x "
								"their share at the point); 1 - only there, 0 - evenly" );
		prefers = std::make_unique<PropertyContainer>( "Prefers" );
		addRoleFlags( *prefers, groundLayers, filterRoles( placement.groundLayers ), false,
					  "The model grows more on this terrain layer (its share among the layer models follows the layer weights); "
					  "none on - evenly everywhere" );
		properties.addSubContainer( prefers.get() );
	}
}

ScatterVariantPlacement placementFromProperties( const PropertyContainer& properties, const PropertyContainer* prefers,
												const std::vector<int32_t>& groundLayers )
{
	ScatterVariantPlacement placement;
	placement.sizeMultiplier = std::max( properties["Size"].data<float>(), 0.01f );
	placement.sizeVariation = properties["Own size variation"].data<bool>() ? std::clamp( properties["Size variation"].data<float>(), 0.0f, 1.0f ) : -1.0f;
	placement.alignToTerrain = std::clamp( properties["Align to terrain"].data<int32_t>(), 0, 2 ) - 1;
	placement.patchGroup = std::clamp( properties["Patch group"].data<int32_t>(), 0, static_cast<int32_t>( ScatterPass::maxVariants ) );
	if( prefers )
	{
		bool all = false;
		placement.groundLayers = rolesFromFlags( *prefers, groundLayers, all );
		placement.groundPreference = std::clamp( properties["Ground preference"].data<float>(), 0.0f, 1.0f );
	}
	return placement;
}

// Своя раскладка — в вариант раскладки; группа куртин — номер узора шума: своя — номер варианта, общая — после них;
// предпочтение — биты слоёв по ролям
void applyPlacement( const ScatterVariantPlacement& placement, uint32_t index, ScatterPass::Variant& variant,
					 const std::vector<int32_t>& groundLayers )
{
	variant.sizeMultiplier = placement.sizeMultiplier;
	variant.sizeVariation = placement.sizeVariation;
	variant.alignToTerrain = static_cast<float>( placement.alignToTerrain );
	variant.patchGroup = placement.patchGroup >= 1 ? ScatterPass::maxVariants + static_cast<uint32_t>( placement.patchGroup ) : index;
	variant.groundLayers = roleLayerBits( placement.groundLayers, groundLayers );
	variant.groundPreference = variant.groundLayers != 0 ? std::clamp( placement.groundPreference, 0.0f, 1.0f ) : 0.0f;
}

// Надпись окна (GUIControlType::LABEL) — значение считает объект: правкой не отмечается
template<class TYPE>
void setLabel( Property& property, const TYPE& value )
{
	if( property.data<TYPE>() != value )
	{
		property.setData( value );
		property.markSaved();
	}
}

}

void Scatterer::setTerrain( const TerrainHeightSource* terrain )
{
	m_terrain = terrain;
}

void Scatterer::setGroundLayers( const std::vector<int32_t>& layersOfRoles )
{
	m_groundLayers = layersOfRoles;
}

uint32_t Scatterer::groundLayerMask( const ScatterLayerSettings& settings ) const
{
	return settings.layerFilter.empty() ? 0 : ( 1u << 31 ) | roleLayerBits( settings.layerFilter, m_groundLayers );
}

bool Scatterer::addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterLayerSettings& layerSettings )
{
	if( models.empty() )
		return false;
	// Камни русел — постоянный слой: раскладывается раз, по камням конвейера
	ScatterLayerSettings settings = layerSettings;
	if( settings.placement == ScatterPlacement::streamStones )
		settings.persistent = true;
	// Кадр импостера — из списка окна (степень двойки 16…512): запечённое при загрузке совпадёт с окном
	settings.impostorFrameSize = frameSizeFromIndex( static_cast<int32_t>( frameSizeIndex( settings.impostorFrameSize ) ) );

	Layer layer;
	layer.mask = mask;
	layer.settings = settings;
	layer.pass = std::make_unique<ScatterPass>();
	// Константы раскладки (Shaders\scatter.cs) из строки ScatterLayers; ёмкости и число вариантов — createBuffers()
	ScatterPass::PopulateParams& params = layer.pass->populateParams();
	fillParams( params, settings );

	// Дальности LOD — из модели, как у моделей уровня; последний LOD рисуется до конца кольца. Границы — сфера LOD0:
	// по ней отсечение экземпляров (у деревьев — десятки метров) и длина их тени
	std::vector<ScatterPass::Variant> passVariants;
	const size_t variantCount = std::min<size_t>( models.size(), ScatterPass::maxVariants );
	for( size_t v = 0; v < variantCount; ++v )
	{
		DMModel* model = models[v].model;
		ScatterPass::Variant passVariant;
		passVariant.weight = models[v].weight;
		passVariant.lodCount = std::min<uint32_t>( model->lodCount(), ScatterPass::maxLods );
		if( DMModel::LodBlock* lod0 = model->getLodById( 0 ) )
		{
			const BoundingBox& box = lod0->bounds;
			const float radius = XMVectorGetX( XMVector3Length( XMLoadFloat3( &box.Extents ) ) );
			passVariant.bounds = XMFLOAT4( box.Center.x, box.Center.y, box.Center.z, radius );
			const BoundingBox& base = lod0->baseBounds;
			passVariant.footprint = XMFLOAT4( base.Center.x, base.Center.z, std::hypot( base.Extents.x, base.Extents.z ), 0.0f );
		}
		applyPlacement( models[v].placement, static_cast<uint32_t>( v ), passVariant, m_groundLayers );
		LayerVariant variant;
		if( DMModel::LodBlock* lod0 = model->getLodById( 0 ) )
			variant.box = lod0->bounds;
		variant.castShadow = models[v].castShadow;
		variant.id = models[v].id;
		variant.properties = std::make_unique<PropertyContainer>( model->properties()->name() );
		Property* weight = variant.properties->insert( "Weight", models[v].weight );
		weight->setLow( 0.0f );
		weight->setHigh( std::max( 10.0f, models[v].weight * 2.0f ) );
		weight->setTooltip( "Share of the layer cells: weight / sum of the weights" );
		variant.properties->insert( "Cast shadow", models[v].castShadow )->setTooltip( "If the layer casts a shadow" );
		addPlacementProperties( *variant.properties, variant.prefersProperties, models[v].placement, settings.sizeVariation, m_groundLayers );
		// Импостер — ещё один LOD после последнего LOD модели (если есть место): модель — до impostor_distance. LOD модели,
		// которые начинаются не ближе impostor_distance, не нужны: импостер сменяет предыдущий сразу, с дизерингом
		// Границы LOD модели: свои у варианта слоя (ScatterLayerModels.lod_distances), иначе — дальности модели
		const std::vector<float>& ownRanges = models[v].lodDistances;
		auto modelRange = [&]( uint32_t lod )
		{
			return lod < ownRanges.size() ? ownRanges[lod] : model->lodRange( static_cast<uint16_t>( lod ) );
		};
		uint32_t modelLods = passVariant.lodCount;
		variant.model = model;
		variant.impostor = settings.impostorDistance > 0.0f;
		while( variant.impostor && modelLods > 1 && modelRange( modelLods - 2 ) >= settings.impostorDistance )
			--modelLods;
		passVariant.lodCount = modelLods;
		if( variant.impostor && modelLods >= ScatterPass::maxLods )
		{
			LOG( "Scatter layer " + layer.mask + ": model " + model->properties()->name() + " has no free LOD for an impostor" );
			variant.impostor = false;
		}
		for( uint32_t i = 0; i < modelLods; ++i )
		{
			passVariant.lodEnd[i] = i + 1 < modelLods ? modelRange( i ) : variant.impostor ? settings.impostorDistance : params.farBorder;
			LayerLod lod;
			DMModel::LodBlock* block = model->getLodById( static_cast<uint16_t>( i ) );
			const size_t sectionCount = std::min<size_t>( block->sections.size(), ScatterPass::maxSections );
			for( size_t s = 0; s < sectionCount; ++s )
			{
				LayerSection section;
				section.section = block->sections[s].get();
				section.material = System::materials().get( section.section->material ).get();
				// Вариант материала для экземпляров расстановки — до прогрева пайплайнов
				if( !section.material->enablePlacedInstances() )
					LOG( "Scatter layer " + layer.mask + ": material " + section.material->name() + " can't draw scatter instances" );
				lod.sections.push_back( section );
			}
			passVariant.sectionCount[i] = static_cast<uint32_t>( std::max<size_t>( sectionCount, 1 ) );
			variant.lods.push_back( lod );
		}
		if( variant.impostor )
		{
			// Секция импостера — карточка (MeshStorage::cardId); материал появится при запекании (bake)
			variant.impostorSection = std::make_unique<DMModel::Section>();
			variant.impostorSection->mesh = MeshStorage::cardId;
			variant.impostorSection->material = 0;
			variant.impostorSection->ownParams = std::make_unique<PropertyContainer>( "Impostor" );
			variant.impostorSection->params = variant.impostorSection->ownParams.get();
			LayerLod lod;
			lod.sections.push_back( { variant.impostorSection.get(), nullptr } );
			passVariant.lodEnd[modelLods] = params.farBorder;
			passVariant.sectionCount[modelLods] = 1;
			passVariant.lodCount = modelLods + 1;
			passVariant.shadowLastLodDistance = settings.shadowImpostorDistance;
			variant.lods.push_back( lod );
		}
		// Кольца LOD варианта в окне слоя: границы LOD модели правятся (свои у слоя — «Save level» пишет их в
		// ScatterLayerModels.lod_distances), конец последнего LOD модели — дальность импостера слоя или край кольца,
		// у импостера — край кольца; тень — с какого LOD и до какой дальности. Надписи обновляет compute
		variant.modelLods = modelLods;
		for( uint32_t i = 0; i + 1 < modelLods; ++i )
		{
			Property* range = variant.properties->insert( lodRangeName( i ), passVariant.lodEnd[i] );
			range->setLow( 0.1f );
			range->setHigh( std::max( 200.0f, passVariant.lodEnd[i] * 2.0f ) );
			char tooltip[160];
			snprintf( tooltip, sizeof( tooltip ), "LOD %u is drawn up to this distance, then LOD %u (each instance switches up to "
					  "20 %% closer); the model has %g m", i, i + 1, model->lodRange( static_cast<uint16_t>( i ) ) );
			range->setUnit( "m" )->setTooltip( tooltip );
		}
		Property* lastRange = variant.properties->insert( lodRangeName( modelLods - 1 ), passVariant.lodEnd[modelLods - 1] );
		lastRange->setControlType( GUIControlType::LABEL );
		lastRange->setUnit( "m" )->setTooltip( variant.impostor ? "The last LOD of the model: up to the impostor distance of the layer" :
															   "The last LOD of the model: up to the far border of the layer" );
		if( variant.impostor )
		{
			Property* impostorRange = variant.properties->insert( "Impostor up to", params.farBorder );
			impostorRange->setControlType( GUIControlType::LABEL );
			impostorRange->setUnit( "m" )->setTooltip( "Impostor: from the impostor distance up to the far border of the layer" );
		}
		variant.properties->insert( "Shadow from LOD", 0u )->setTooltip(
			"Shadow views draw at most this detailed LOD (Shadow min LOD of the model)" );
		Property* shadowRange = variant.properties->insert( "Shadow up to", 0.0f );
		shadowRange->setControlType( GUIControlType::LABEL );
		shadowRange->setUnit( "m" )->setTooltip( "The instances cast a shadow up to this distance (0 - no shadow)" );
		layer.variants.push_back( std::move( variant ) );
		passVariants.push_back( passVariant );
	}
	layer.passVariants = passVariants;
	layer.maxHeight = maxHeight( layer );
	if( !layer.pass->createBuffers( passVariants, settings.persistent ) )
		return false;

	// Постоянный слой: пул и кластеры на весь мир террейна
	if( settings.persistent )
	{
		if( !m_terrain )
			return false;
		if( !m_persistentShaders )
		{
			if( !m_placeShader.Initialize( "Shaders\\scatter.cs", "placeWorld" ) || !m_cullShader.Initialize( "Shaders\\scatter.cs", "cullPlaced" ) )
				return false;
			m_persistentShaders = true;
		}
		if( !layer.pass->createPersistent( m_terrain->terrainHeight().worldSize, maxRadius( layer ), placedCapacity( settings ) ) )
			return false;
	}

	// Имя окна слоя — первая модель и число остальных
	std::string name = std::to_string( m_layers.size() ) + ": " + models[0].model->properties()->name();
	if( variantCount > 1 )
		name += " + " + std::to_string( variantCount - 1 );
	layer.properties = std::make_unique<PropertyContainer>( name );
	auto number = [&layer]( const char* name, float value, float low, float high, const char* unit, const char* tooltip )
	{
		Property* property = layer.properties->insert( name, value );
		property->setLow( low );
		property->setHigh( std::max( high, value ) );
		property->setUnit( unit )->setTooltip( tooltip );
		return property;
	};
	// Скрыть слой — посмотреть на другие (траву убрать — видны одуванчики): не считается и не рисуется, с уровнем не
	// сохраняется
	layer.properties->insert( "Visible", true )->setTransient()->setTooltip(
		"Show the layer: off - it is neither placed nor drawn (to see the other layers); not saved with the level" );
	const float ringHigh = std::max( 200.0f, settings.farBorder * 2.0f );
	number( "Cell size", settings.cellSize, 0.05f, 20.0f, "m", "Grid step: at most one instance per cell" )->setLogarithmic();
	number( "Near border", settings.nearBorder, 0.0f, ringHigh, "m", "Ring around the camera where the layer grows" );
	number( "Far border", settings.farBorder, 1.0f, ringHigh, "m", "Ring around the camera where the layer grows" );
	number( "Near fade", settings.nearFade, 0.0f, 100.0f, "m", "Instances shrink to zero over this width at the near border" );
	number( "Far fade", settings.farFade, 0.0f, 200.0f, "m", "Instances shrink to zero over this width at the far border" );
	number( "Size", settings.sizeMultiplier, 0.05f, 10.0f, "", "Size multiplier of the models" )->setLogarithmic();
	number( "Size variation", settings.sizeVariation, 0.0f, 1.0f, "",
			"Random size spread: an instance is Size x (1 - variation ... 1)" );
	number( "Jitter", settings.jitter, 0.0f, 1.0f, "", "Random offset inside the cell, share of the cell size" );
	// Цвет земли у растения: трава на пожелтевшей земле не сочно-зелёная, вдали пучки не выделяются тёмными корнями
	number( "Ground color", settings.groundColor, 0.0f, 1.0f, "",
			"Share of the ground color under the plant at its root (the terrain layers by weight, with their tint): 0 - own color" );
	number( "Ground color height", settings.groundColorHeight, 0.0f, 2.0f, "m",
			"Height above the root where the ground color fades out to the plant's own color" );
	number( "Ground color far", settings.groundColorFar, 0.0f, 1.0f, "",
			"Share of the ground color over the whole plant at the far border (from half the ring): far plants blend into the ground" );
	number( "Color variation", settings.colorVariation, 0.0f, 0.5f, "",
			"Random brightness spread between plants (+-); the ground brightness spots of the terrain apply too" );
	// Края кольца и прореживание вдали: граница не ровной дугой, вдали меньше экземпляров
	number( "Edge spread", settings.edgeSpread, 0.0f, 1.0f, "",
			"Own fade distance of each instance over the fade bands (and a coarse world noise): the ring edge is ragged, "
			"plants vanish one by one; 0 - all shrink together" );
	number( "Thinning start", settings.thinStart, 0.0f, std::max( settings.farBorder, 1.0f ), "m",
			"Distance where thinning starts" );
	layer.properties->insert( "Dither fade", settings.ditherFade )->setTooltip(
		"At the ring borders an instance fades in from transparency (a share of its pixels, like foliage in UE) instead of "
		"growing from zero size; needs materials with dithered LOD transition" );
	number( "Thinning", settings.thinning, 0.0f, 0.95f, "",
			"Share of instances removed at the far border (the same ones always; the rest are bigger - the cover stays): fewer "
			"instances far away; 0 - none" );
	// Фильтр по слоям земли (как Landscape Grass Type в UE): подокно «Grows on» — флажок на роль слоя материала террейна.
	// У камней русел нет: их кладёт CPU по камням конвейера, не раскладка по маске
	if( settings.placement != ScatterPlacement::streamStones &&
		std::any_of( m_groundLayers.begin(), m_groundLayers.end(), []( int32_t layer ) { return layer >= 0; } ) )
	{
		const std::vector<std::string> roles = filterRoles( settings.layerFilter );
		for( const std::string& name : roles )
			if( terrainLayerRole( name ) == TerrainLayerRole::count )
				LOG( "Scatter layer " + layer.mask + ": unknown terrain layer role " + name + " in layer_filter" );
		layer.groundProperties = std::make_unique<PropertyContainer>( "Grows on" );
		addRoleFlags( *layer.groundProperties, m_groundLayers, settings.layerFilter == "none" ? std::vector<std::string>{ "none" } : roles,
					  true, "The layer grows on this terrain layer (by its weight in the splat map and the detail weights); all on - no filter" );
		number( "Ground threshold", settings.layerFilterThreshold, 0.0f, 0.5f, "",
				"Share of the Grows on terrain layers where the density is half; from twice the share - full, at 0 - none "
				"(a snow or pebble stroke over a meadow removes the grass); 0 - any share of them" );
	}
	// Куртины: модели слоя — виды, растут пятнами (окно варианта: «Patch group» — модели одного вида в одних пятнах)
	number( "Patch size", settings.patchScale, 0.0f, 100.0f, "m",
			"Patches of the layer models (species): size of the world noise spots where a model group outnumbers the others; "
			"0 - the models are mixed evenly by weight" );
	number( "Patch strength", settings.patchStrength, 0.0f, 1.0f, "",
			"How strongly a model group outnumbers the others in its patches: weight x e^(4 x strength x noise)" );
	Property* rotation = layer.properties->insert( "Rotation", settings.rotationRange );
	rotation->setLow( 0.0f );
	rotation->setHigh( 360.0f );
	rotation->setUnit( "deg" )->setTooltip( "Random rotation limit around X, Y, Z" );
	layer.properties->insert( "Align to terrain", settings.alignToTerrain )->setTooltip( "Instance Y axis along the terrain normal" );
	layer.properties->insert( "Cast shadow", params.castShadow > 0.5f );
	if( settings.impostorDistance > 0.0f )
	{
		Property* impostorDistance = layer.properties->insert( "Impostor distance", settings.impostorDistance );
		impostorDistance->setLow( 0.1f );
		impostorDistance->setHigh( std::max( settings.farBorder, settings.impostorDistance ) );
		impostorDistance->setUnit( "m" )->setTooltip( "From this distance an instance is an impostor (the last LOD of the model - "
													  "up to it); model LODs that started beyond it at loading are not used" );
		// С какого расстояния тень — импостером (0 — как в кадре); до impostor_distance — раньше, чем в кадре
		Property* shadowImpostor = layer.properties->insert( "Shadow impostor distance", settings.shadowImpostorDistance );
		shadowImpostor->setLow( 0.0f );
		shadowImpostor->setHigh( settings.impostorDistance );
		shadowImpostor->setUnit( "m" );
		layer.properties->insert( "Impostor shadow", settings.impostorCastShadow )->setTooltip(
			"If impostors cast a shadow: off - shadow views skip them (far grass)" );
		// Запекание импостера: правка — кадры заново, когда ползунок отпущен (rebakeChangedImpostors)
		Property* frameSize = layer.properties->insert( "Impostor frame size", static_cast<int32_t>( frameSizeIndex( settings.impostorFrameSize ) ) );
		frameSize->setItems( frameSizeItems );
		frameSize->setUnit( "texels" )->setTooltip( "Side of an impostor frame (8 x 8 frames): smaller - less memory and a faster bake "
													"(64 - a quarter of 128); the impostor is rebaked" );
		Property* density = layer.properties->insert( "Impostor density", settings.impostorDensity );
		density->setLow( 0.25f );
		density->setHigh( 4.0f );
		density->setTooltip( "Coverage multiplier of the bake: > 1 - denser (thin blades and needles far away merge instead of "
							 "being cut by the alpha test); rebaked when the edit ends" );
		Property* occlusion = layer.properties->insert( "Impostor occlusion", settings.impostorOcclusion );
		occlusion->setLow( 0.0f );
		occlusion->setHigh( 1.0f );
		occlusion->setTooltip( "Sky light occlusion inside the crown, baked from the frames depth; rebaked when the edit ends" );
		Property* baseShade = layer.properties->insert( "Impostor base shade", settings.impostorBaseShade );
		baseShade->setLow( 0.0f );
		baseShade->setHigh( 1.0f );
		baseShade->setTooltip( "Impostor color at the root x (1 - shade), up to the model top - none: far grass without "
							   "a shadow is not lighter than the shadowed grass next to it" );
	}
	if( layer.groundProperties )
		layer.properties->addSubContainer( layer.groundProperties.get() );
	for( LayerVariant& variant : layer.variants )
		layer.properties->addSubContainer( variant.properties.get() );
	// «Materials» — материалы растений слоя: экземпляры материалов секций их моделей, каждый один раз (общие у всех моделей
	// и LOD с ним, System::materialInstances). Правка — вживую: группы и их параметры слой берёт каждый кадр; «Save level»
	// пишет изменённое в MaterialParameterInstance. Импостеры — запекаются заново, когда правка закончилась
	// (rebakeChangedImpostors)
	layer.materialProperties = std::make_unique<PropertyContainer>( "Materials" );
	for( const LayerVariant& variant : layer.variants )
		for( const LayerLod& lod : variant.lods )
			for( const LayerSection& section : lod.sections )
			{
				if( !section.section || !section.material )
					continue;	// секция импостера: материал — после запекания, своих параметров нет
				std::vector<PropertyContainer*>& shown = layer.materialProperties->subContainer();
				if( std::find( shown.begin(), shown.end(), section.section->params ) == shown.end() )
					layer.materialProperties->addSubContainer( section.section->params );
			}
	layer.properties->addSubContainer( layer.materialProperties.get() );
	m_properties.addSubContainer( layer.properties.get() );

	m_layers.push_back( std::move( layer ) );
	return true;
}

bool Scatterer::bake( const BakeContext& context )
{
	// Импостер — шаг прогресса загрузки (запекание модели ~0,5 с); между импостерами состояние GPU не переходит
	uint32_t impostors = 0;
	for( const Layer& layer : m_layers )
		for( const LayerVariant& variant : layer.variants )
			impostors += variant.impostor ? 1 : 0;
	SlowTask task( static_cast<float>( std::max( impostors, 1u ) ) );
	for( Layer& layer : m_layers )
	for( LayerVariant& variant : layer.variants )
	{
		if( !variant.impostor )
			continue;
		// Материал импостера — на модель, общий для всех наборов (настройки запекания — первого слоя с этой моделью)
		const std::string name = "Impostor " + variant.model->properties()->name();
		task.enterProgressFrame( 1.0f, variant.model->properties()->name() );
		MaterialStorage& materials = System::materials();
		Material* material = materials.exists( name ) ? materials[name].get() : nullptr;
		if( !material )
		{
			const auto start = std::chrono::steady_clock::now();
			// Импостер без тени (трава) — плоский: по настройке слоя при загрузке
			auto impostor = std::make_unique<ImpostorMaterial>( materials.freeId(), name, !layer.settings.impostorCastShadow );
			impostor->setLayoutDesc( impostor->initLayouts() );
			if( !impostor->initialize() || !impostor->bake( *variant.model, context, bakeSettings( layer.settings ) ) )
			{
				// Без импостера дальний LOD варианта не рисуется (группы пропускают секции без материала)
				LOG( "Scatter layer " + layer.mask + ": impostor of model " + variant.model->properties()->name() + " is not baked" );
				continue;
			}
			material = impostor.get();
			materials.insertResource( std::move( impostor ) );
			const auto ms = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();
			LOG( "Impostor baked: " + name + ", ms: " + std::to_string( ms ) );
		}
		variant.impostorSection->material = material->id();
		variant.lods.back().sections.back().material = material;
		variant.impostorMaterial = dynamic_cast<ImpostorMaterial*>( material );
		variant.bakedParamsHash = bakeSourceHash( *variant.model, layer.settings );
	}
	m_bakeContext = std::make_unique<BakeContext>( context );
	return true;
}

uint64_t Scatterer::bakeSourceHash( DMModel& model, const ScatterLayerSettings& settings )
{
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash]( uint64_t value ) { hash = ( hash ^ value ) * 1099511628211ull; };
	if( DMModel::LodBlock* lod0 = model.getLodById( 0 ) )
		for( const auto& section : lod0->sections )
			mix( hashParams( *section->params ) );
	uint32_t bits;
	std::memcpy( &bits, &settings.impostorDensity, sizeof( bits ) );
	mix( bits );
	std::memcpy( &bits, &settings.impostorOcclusion, sizeof( bits ) );
	mix( bits );
	mix( settings.impostorFrameSize );
	return hash;
}

void Scatterer::rebakeChangedImpostors()
{
	if( !m_bakeContext )
		return;
	// Материал импостера — на модель, общий для слоёв: запекается раз, отпечаток обновляется у всех её вариантов
	// Пока ползунок тянут (окно слоя или параметры материала), ждём: запекание — ~0,3 с на модель
	std::vector<ImpostorMaterial*> rebaked;
	for( Layer& layer : m_layers )
	{
		const auto withImpostor = []( const LayerVariant& variant ) { return variant.impostorMaterial != nullptr; };
		if( std::none_of( layer.variants.begin(), layer.variants.end(), withImpostor ) || layer.properties->editing() )
			continue;
		// По окну слоя: правка запекания — новый отпечаток
		const ScatterLayerSettings settings = settingsFromProperties( layer );
	for( LayerVariant& variant : layer.variants )
	{
		if( !variant.impostorMaterial || paramsEditing( *variant.model ) )
			continue;
		const uint64_t hash = bakeSourceHash( *variant.model, settings );
		if( hash == variant.bakedParamsHash )
			continue;
		variant.bakedParamsHash = hash;
		if( std::find( rebaked.begin(), rebaked.end(), variant.impostorMaterial ) != rebaked.end() )
			continue;
		rebaked.push_back( variant.impostorMaterial );
		const auto start = std::chrono::steady_clock::now();
		const bool baked = variant.impostorMaterial->bake( *variant.model, *m_bakeContext, bakeSettings( settings ) );
		const auto ms = std::chrono::duration<float, std::milli>( std::chrono::steady_clock::now() - start ).count();
		LOG( "Impostor " + variant.model->properties()->name() + ( baked ? " rebaked after an edit, ms: " + std::to_string( ms ) :
																	  std::string( " is not rebaked" ) ) );
	}
	}
}

bool Scatterer::paramsEditing( DMModel& model )
{
	if( DMModel::LodBlock* lod0 = model.getLodById( 0 ) )
		for( const auto& section : lod0->sections )
			if( section->params->editing() )
				return true;
	return false;
}

bool Scatterer::layerVisible( const Layer& layer )
{
	return !layer.properties->exists( "Visible" ) || ( *layer.properties )["Visible"].data<bool>();
}

uint64_t Scatterer::hashParams( const PropertyContainer& params )
{
	// FNV-1a по именам и байтам значений в порядке добавления
	uint64_t hash = 14695981039346656037ull;
	const auto mix = [&hash]( const void* data, size_t size )
	{
		for( size_t i = 0; i < size; ++i )
		{
			hash ^= static_cast<const uint8_t*>( data )[i];
			hash *= 1099511628211ull;
		}
	};
	for( const std::string& name : params.names() )
	{
		mix( name.data(), name.size() );
		const Property& property = params.property( name );
		if( const bool* v = property.dataPtr<bool>() ) mix( v, sizeof( *v ) );
		else if( const float* f = property.dataPtr<float>() ) mix( f, sizeof( *f ) );
		else if( const XMFLOAT2* f2 = property.dataPtr<XMFLOAT2>() ) mix( f2, sizeof( *f2 ) );
		else if( const XMFLOAT3* f3 = property.dataPtr<XMFLOAT3>() ) mix( f3, sizeof( *f3 ) );
		else if( const XMFLOAT4* f4 = property.dataPtr<XMFLOAT4>() ) mix( f4, sizeof( *f4 ) );
		else if( const int32_t* i = property.dataPtr<int32_t>() ) mix( i, sizeof( *i ) );
		else if( const uint32_t* u = property.dataPtr<uint32_t>() ) mix( u, sizeof( *u ) );
	}
	return hash;
}

void Scatterer::assignGroups( Layer& layer )
{
	// Секции списков с одним материалом, параметрами и состоянием — одна группа: один ExecuteIndirect на вид. Порядок
	// обхода постоянен, поэтому у неизменившегося слоя таблицы не меняются и на GPU не уходят
	layer.groups.clear();
	const bool layerShadow = ( *layer.properties )["Cast shadow"].data<bool>();
	for( uint32_t v = 0; v < layer.variants.size(); ++v )
	{
		const LayerVariant& variant = layer.variants[v];
		for( uint32_t i = 0; i < variant.lods.size(); ++i )
		for( bool transition : { false, true } )
		{
			const uint32_t list = ScatterPass::listIndex( v, i, transition );
			const LayerLod& lod = variant.lods[i];
			for( uint32_t s = 0; s < ScatterPass::maxSections; ++s )
			{
				// Список перехода — экземпляры в полосе смены LOD; без дизеринга он пуст
				if( s >= lod.sections.size() || !lod.sections[s].material || ( transition && !variant.ditheredLodTransition ) )
				{
					layer.pass->clearSectionArgs( list, s );
					continue;
				}
				const LayerSection& section = lod.sections[s];
				LayerGroup group;
				group.material = section.material;
				group.params = section.section->params;
				group.paramsHash = hashParams( *section.section->params );
				group.state = section.material->renderState( *section.section->params );
				group.pass = passFor( group.state.blendMode );
				group.transition = transition;
				group.prepassed = inDepthPrepass( section );
				group.castShadow = layerShadow && castsShadow( layer, variant, section );

				uint32_t index = 0;
				for( ; index < layer.groups.size(); ++index )
				{
					const LayerGroup& other = layer.groups[index];
					if( other.material == group.material && other.paramsHash == group.paramsHash && other.transition == group.transition &&
						other.state.twoSided == group.state.twoSided && other.state.blendMode == group.state.blendMode &&
						other.prepassed == group.prepassed && other.castShadow == group.castShadow )
						break;
				}
				if( index == layer.groups.size() )
					layer.groups.push_back( group );

				const AbstractMesh* mesh = System::meshes().get( section.section->mesh ).get();
				layer.pass->setSectionArgs( list, s, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset(), index );
			}
		}
	}
}

void Scatterer::compute( const FrameContext& frame )
{
	if( !m_computeEnabled || !m_terrain || m_layers.empty() )
		return;

	const TerrainHeight terrain = m_terrain->terrainHeight();
	Device::updateResource<TerrainParams>( m_terrainBuffer, [&terrain]( TerrainParams& params )
	{
		params.worldSize = terrain.worldSize;
		params.heightMultiplier = terrain.heightMultiplier;
		params.heightOffset = terrain.heightOffset;
		params.detailTileSize = terrain.detailTileSize;
	} );
	DMD3D::instance().setConstantBuffer( 5, m_terrainBuffer );

	// Луч от верха инстанса до земли длиннее высоты в 1 / sin(высота солнца) раз; у горизонта — не больше 10
	const XMFLOAT3& toSun = frame.toShadowLight;
	m_shadowLength = toSun.y > 0.0f ? std::min( 1.0f / toSun.y, 10.0f ) : 0.0f;

	// Виды кадра: главный и каскады теней. Дальности LOD у видов теней — у вариантов (ScatterPass::setShadowLods):
	// множитель набора × множитель модели, «Shadow min LOD» модели, импостер без тени
	const float shadowLodScale = m_properties["Shadow LOD scale"].data<float>();
	Device::updateResource<FrustumParams>( m_frustumBuffer, [&]( FrustumParams& params )
	{
		params = {};
		const uint32_t count = frame.viewCount ? std::min( frame.viewCount, maxRenderViews ) : 1;
		for( uint32_t v = 0; v < count; ++v )
		{
			const RenderView& view = frame.viewCount ? *frame.views[v] : frame.view;
			for( int i = 0; i < 6; ++i )
				XMStoreFloat4( &params.planes[v * 6 + i], view.frustum.planes()[i] );
			params.viewParams[v] = XMFLOAT4( 0.0f, 0.0f, view.cascadeNear, view.cascadeFar );
			params.viewDepths[v] = XMFLOAT4( view.cascadeNear, view.cascadeDepthFar, 0.0f, 0.0f );
		}
		params.viewCount = count;
		params.shadowCast = XMFLOAT4( -toSun.x, -toSun.y, -toSun.z, m_shadowLength );
	} );
	DMD3D::instance().setConstantBuffer( 6, m_frustumBuffer );

	for( Layer& layer : m_layers )
	{
		if( !layerVisible( layer ) )
			continue;
		applyProperties( layer );
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		if( params.cellSize <= 0.0f || params.farBorder <= 0.0f )
			continue;

		// Флаг тени из GUI: по нему compute раскладывает инстансы и в списки видов теней
		bool anyShadow = false;
		for( const LayerVariant& variant : layer.variants )
			anyShadow = anyShadow || variant.castShadow;
		layer.pass->populateParams().castShadow = anyShadow && ( *layer.properties )["Cast shadow"].data<bool>() ? 1.0f : 0.0f;

		// Смена LOD дизерингом — если её умеют материалы всех секций варианта
		for( uint32_t v = 0; v < layer.variants.size(); ++v )
		{
			LayerVariant& variant = layer.variants[v];
			variant.ditheredLodTransition = variant.lods.size() > 1;
			for( const LayerLod& lod : variant.lods )
			for( const LayerSection& section : lod.sections )
				variant.ditheredLodTransition = variant.ditheredLodTransition && section.material &&
												section.material->renderState( *section.section->params ).ditheredLodTransition;
			layer.pass->setDitheredLodTransition( v, variant.ditheredLodTransition );
			// LOD теней: множители набора и модели — только для границ LOD модели (переход к импостеру у теней задаёт
			// слой), «Shadow min LOD» модели, импостер без тени — флаг слоя
			const uint32_t modelLods = static_cast<uint32_t>( variant.lods.size() ) - ( variant.impostor ? 1 : 0 );
			ScatterPass::ShadowLods shadowLods;
			shadowLods.scale = shadowLodScale * ( variant.model ? variant.model->shadowLodScale() : 1.0f );
			shadowLods.minLod = variant.model ? std::min( variant.model->shadowMinLod(), modelLods > 0 ? modelLods - 1 : 0u ) : 0;
			shadowLods.modelBoundaries = modelLods > 0 ? modelLods - 1 : 0;
			if( variant.impostor && !( *layer.properties )["Impostor shadow"].data<bool>() )
				shadowLods.castingLods = modelLods;
			layer.pass->setShadowLods( v, shadowLods );
			if( variant.impostor )
				layer.pass->setShadowLastLodDistance( v, ( *layer.properties )["Shadow impostor distance"].data<float>() );
			if( variant.impostorMaterial )
				variant.impostorMaterial->setBaseShade( ( *layer.properties )["Impostor base shade"].data<float>() );

			// Надписи колец LOD: конец последнего LOD модели, импостера и тень — с какого LOD и до куда
			PropertyContainer& variantProperties = *variant.properties;
			const ScatterPass::Variant& passVariant = layer.passVariants[v];
			const float farBorder = layer.settings.farBorder;
			if( modelLods > 0 )
				setLabel( variantProperties[lodRangeName( modelLods - 1 )], passVariant.lodEnd[modelLods - 1] );
			if( variant.impostor )
				setLabel( variantProperties["Impostor up to"], farBorder );
			const bool casts = variant.castShadow && ( *layer.properties )["Cast shadow"].data<bool>();
			float shadowEnd = 0.0f;
			if( casts )
			{
				shadowEnd = farBorder;
				if( shadowLods.castingLods < passVariant.lodCount && modelLods > 0 )
				{
					// Импостер без тени: тень — до дальности импостера (у теней — не дальше их своей)
					const float shadowImpostor = ( *layer.properties )["Shadow impostor distance"].data<float>();
					shadowEnd = passVariant.lodEnd[modelLods - 1];
					if( shadowImpostor > 0.0f )
						shadowEnd = std::min( shadowEnd, shadowImpostor );
				}
			}
			setLabel( variantProperties["Shadow from LOD"], casts ? shadowLods.minLod : 0u );
			setLabel( variantProperties["Shadow up to"], shadowEnd );
		}
		assignGroups( layer );

		const ShaderView& heightMap = *terrain.heightMap;
		const ShaderView& foliageClear = *terrain.foliageClearMask;
		const ShaderView& mask = System::textures().get( layer.mask )->srv();

		// Постоянный слой: раз — раскладка на карту кластерами, каждый кадр — отбор кластеров и их экземпляров в пул
		// кадра и списки видов
		if( layer.pass->persistent() )
		{
			if( !layer.placed && layer.settings.placement == ScatterPlacement::streamStones )
			{
				placeStreamStones( layer );
				layer.placed = true;
			}
			if( !layer.placed )
			{
				PassDesc place = layer.pass->placeDesc( "Scatter placement" );
				place.reads = { { &heightMap, "height map" }, { &foliageClear, "foliage clear mask" }, { &mask, "density mask" } };
				DMD3D::instance().beginPass( place );
				DMD3D::instance().setSRV( 0, heightMap );
				DMD3D::instance().setSRV( 1, foliageClear );
				DMD3D::instance().setSRV( 2, mask );
				layer.pass->place( m_placeShader );
				layer.placed = true;
			}
			PassDesc cull = layer.pass->passDesc( "Scatter clusters" );
			cull.reads = layer.pass->placedReads();
			DMD3D::instance().beginPass( cull );
			layer.pass->resetCounters();
			layer.pass->cull( m_cullShader );
			layer.pass->buildCommands( m_commandShader );
			continue;
		}

		// Сетка покрывает квадрат со стороной 2 · farBorder вокруг камеры
		const float cells = std::ceil( 2.0f * params.farBorder / params.cellSize ) + 1.0f;
		const uint16_t gridDim = static_cast<uint16_t>( std::min( cells, static_cast<float>( maxGridDim ) ) );

		// Проход раскладки слоя: читает карту высот и маску плотности, пишет счётчики (сначала нули), пулы, списки
		// индексов видов и команды
		PassDesc pass = layer.pass->passDesc( "Scatter layer" );
		pass.reads = { { &heightMap, "height map" }, { &foliageClear, "foliage clear mask" }, { &mask, "density mask" } };
		DMD3D::instance().beginPass( pass );
		layer.pass->resetCounters();
		DMD3D::instance().setSRV( 0, heightMap );
		DMD3D::instance().setSRV( 1, foliageClear );
		DMD3D::instance().setSRV( 2, mask );
		layer.pass->populate( m_computeShader, gridDim );
		layer.pass->buildCommands( m_commandShader );
	}
}

bool Scatterer::castsShadow( const Layer& layer, const LayerVariant& variant, const LayerSection& section ) const
{
	ShaderPhaseOptions placed;
	placed.placed = true;
	return variant.castShadow && ( *layer.properties )["Cast shadow"].data<bool>() &&
		   section.material->depthPhaseFor( *section.section->params, placed ) >= 0;
}

bool Scatterer::inDepthPrepass( const LayerSection& section ) const
{
	ShaderPhaseOptions placed;
	placed.placed = true;
	return passFor( section.material->renderState( *section.section->params ).blendMode ) == MeshPass::opaque &&
		   section.material->depthPhaseFor( *section.section->params, placed ) >= 0;
}

void Scatterer::collectMeshes( const RenderView&, MeshCollector& collector )
{
	// Режим читается каждый кадр: параметры материала меняются в GUI
	uint32_t passMask = 0;
	for( const Layer& layer : m_layers )
	{
		if( !layerVisible( layer ) )
			continue;
		for( const LayerVariant& variant : layer.variants )
		for( const LayerLod& lod : variant.lods )
		for( const LayerSection& section : lod.sections )
		{
			if( !section.material )
				continue;
			const MeshPass pass = passFor( section.material->renderState( *section.section->params ).blendMode );
			passMask |= passBit( pass );
			if( pass == MeshPass::opaque && section.material->readsSceneDepth() )
				passMask |= passBit( MeshPass::opaqueDepthRead );
			if( inDepthPrepass( section ) )
				passMask |= passBit( MeshPass::depthPrepass );
			if( castsShadow( layer, variant, section ) )
				passMask |= passBit( MeshPass::csmShadowDepth );
		}
	}
	if( passMask )
		collector.addCustom( passMask );
}

void Scatterer::renderCustom( const RenderContext& context )
{
	ScopedRenderState scatterState;
	DMD3D& d3d = DMD3D::instance();
	const XMMATRIX worldMatrix = XMMatrixIdentity();

	const bool shadow = context.pass == MeshPass::csmShadowDepth;
	const bool prepass = context.pass == MeshPass::depthPrepass;
	const uint32_t view = std::min( context.view.index, ScatterPass::maxViews - 1 );
	for( Layer& layer : m_layers )
	{
		if( !layerVisible( layer ) )
			continue;
		const ScatterPass::PopulateParams& params = layer.pass->populateParams();
		// Инстансы слоя — не дальше кольца от камеры, их тень — не дальше её длины: каскад дальше этого их теней не содержит
		if( shadow && ( params.castShadow < 0.5f || context.view.cascadeNear >= params.farBorder + layer.maxHeight * m_shadowLength ) )
			continue;

		for( uint32_t g = 0; g < layer.groups.size(); ++g )
		{
			const LayerGroup& group = layer.groups[g];
			// Читающим глубину сцены (импостерам) после prepass — свой проход opaqueDepthRead (Material::readsSceneDepth)
			const MeshPass groupPass = group.pass == MeshPass::opaque && group.material->readsSceneDepth() && context.depthFromPrepass ?
									   MeshPass::opaqueDepthRead : group.pass;
			if( shadow ? !group.castShadow : prepass ? !group.prepassed : groupPass != context.pass )
				continue;

			// Группа, которой не было в depth prepass, в проходе цвета пишет глубину сама
			std::optional<ScopedRenderState> ownDepth;
			if( context.depthFromPrepass && !group.prepassed )
				ownDepth.emplace( DepthState::enabled );
			d3d.setState( materialRasterState( group.state.twoSided, false, context.frameRaster ) );

			// После depth prepass Masked и дизеринг не отсекают: маска уже в глубине, проверка EQUAL
			// Экземпляры — из пула расстановки (вариант материала placed: у модели уровня — свой, у PBRInstance — основной)
			ShaderPhaseOptions options;
			options.placed = true;
			options.depthFromPrepass = context.depthFromPrepass && group.prepassed;
			options.lodDither = group.transition;
			Material* shader = group.material;
			if( !shader->setPass( isDepthOnlyPass( context.pass ) ? shader->depthPhaseFor( *group.params, options ) :
								  shader->phaseFor( *group.params, options ) ) )
				continue;
			shader->setParams( *group.params );

			// Вершинный шейдер: индекс инстанса — из списка вида по root-константе команды (начало списка), сам инстанс —
			// из пула (INST_POS, INST_SCALE, INST_ROTATE — Shaders\instance.sh; у списка перехода — ещё LOD_DITHER)
			d3d.setSRV( SLOT_INSTANCE_INDICES, layer.pass->indices() );
			d3d.setSRV( SLOT_INSTANCE_DATA, group.transition ? layer.pass->transitions() : layer.pass->items() );
			context.constants.setPerObjectBuffer( worldMatrix );
			d3d.drawIndexedInstancedIndirectCount( layer.pass->commands(), layer.pass->commandsOffset( view, g ), layer.pass->groupCapacity( g ),
												   layer.pass->counters(), ScatterPass::groupCountOffset( view, g ) );
		}
	}
}

PropertyContainer* Scatterer::properties()
{
	return &m_properties;
}

void Scatterer::fillParams( ScatterPass::PopulateParams& params, const ScatterLayerSettings& settings ) const
{
	params = {};
	params.cellSize = settings.cellSize;
	params.nearBorder = settings.nearBorder;
	params.farBorder = settings.farBorder;
	params.nearFade = settings.nearFade;
	params.farFade = settings.farFade;
	params.sizeMultiplier = settings.sizeMultiplier;
	params.jitter = settings.jitter;
	params.sizeVariation = settings.sizeVariation;
	params.groundColor = settings.groundColor;
	params.groundColorHeight = settings.groundColorHeight;
	params.groundColorFar = settings.groundColorFar;
	params.colorVariation = settings.colorVariation;
	params.edgeSpread = settings.edgeSpread;
	params.thinStart = settings.thinStart;
	params.thinning = settings.thinning;
	params.ditherFade = settings.ditherFade ? 1.0f : 0.0f;
	params.patchScale = settings.patchScale;
	params.patchStrength = settings.patchStrength;
	params.groundLayers = groundLayerMask( settings );
	params.groundThreshold = settings.layerFilterThreshold;
	params.rotationRange = XMFLOAT3( XMConvertToRadians( settings.rotationRange.x ), XMConvertToRadians( settings.rotationRange.y ),
									 XMConvertToRadians( settings.rotationRange.z ) );
	params.alignToTerrain = settings.alignToTerrain ? 1.0f : 0.0f;
	params.castShadow = settings.castShadow ? 1.0f : 0.0f;
}

float Scatterer::maxHeight( const Layer& layer )
{
	float height = 1.0f;
	for( const ScatterPass::Variant& variant : layer.passVariants )
	{
		const float size = layer.settings.sizeMultiplier * variant.sizeMultiplier;
		height = std::max( { height, ( variant.bounds.y + variant.bounds.w ) * size, size } );
	}
	return height;
}

float Scatterer::maxRadius( const Layer& layer )
{
	float radius = 0.0f;
	for( const ScatterPass::Variant& variant : layer.passVariants )
		radius = std::max( radius, variant.bounds.w * variant.sizeMultiplier );
	return radius * layer.settings.sizeMultiplier;
}

ScatterLayerSettings Scatterer::settingsFromProperties( const Layer& layer ) const
{
	const PropertyContainer& properties = *layer.properties;
	ScatterLayerSettings settings = layer.settings;
	settings.cellSize = std::max( properties["Cell size"].data<float>(), 0.05f );
	settings.nearBorder = std::max( properties["Near border"].data<float>(), 0.0f );
	settings.farBorder = std::max( properties["Far border"].data<float>(), settings.nearBorder + settings.cellSize );
	settings.nearFade = std::max( properties["Near fade"].data<float>(), 0.0f );
	settings.farFade = std::max( properties["Far fade"].data<float>(), 0.0f );
	settings.sizeMultiplier = std::max( properties["Size"].data<float>(), 0.01f );
	settings.sizeVariation = std::clamp( properties["Size variation"].data<float>(), 0.0f, 1.0f );
	settings.jitter = std::max( properties["Jitter"].data<float>(), 0.0f );
	settings.groundColor = std::clamp( properties["Ground color"].data<float>(), 0.0f, 1.0f );
	settings.groundColorHeight = std::max( properties["Ground color height"].data<float>(), 0.0f );
	settings.groundColorFar = std::clamp( properties["Ground color far"].data<float>(), 0.0f, 1.0f );
	settings.colorVariation = std::clamp( properties["Color variation"].data<float>(), 0.0f, 1.0f );
	settings.edgeSpread = std::clamp( properties["Edge spread"].data<float>(), 0.0f, 1.0f );
	settings.thinStart = std::max( properties["Thinning start"].data<float>(), 0.0f );
	settings.thinning = std::clamp( properties["Thinning"].data<float>(), 0.0f, 0.95f );
	settings.ditherFade = properties["Dither fade"].data<bool>();
	settings.patchScale = std::max( properties["Patch size"].data<float>(), 0.0f );
	settings.patchStrength = std::clamp( properties["Patch strength"].data<float>(), 0.0f, 1.0f );
	// Фильтр слоёв земли: все флажки — без фильтра, ни одного — «none»
	if( layer.groundProperties )
	{
		bool all = false;
		const std::string filter = rolesFromFlags( *layer.groundProperties, m_groundLayers, all );
		settings.layerFilter = all ? std::string() : filter.empty() ? std::string( "none" ) : filter;
		settings.layerFilterThreshold = std::clamp( properties["Ground threshold"].data<float>(), 0.0f, 0.5f );
	}
	settings.rotationRange = properties["Rotation"].data<XMFLOAT3>();
	settings.alignToTerrain = properties["Align to terrain"].data<bool>();
	settings.castShadow = properties["Cast shadow"].data<bool>();
	if( properties.exists( "Shadow impostor distance" ) )
	{
		settings.impostorDistance = properties["Impostor distance"].data<float>();
		settings.shadowImpostorDistance = properties["Shadow impostor distance"].data<float>();
		settings.impostorCastShadow = properties["Impostor shadow"].data<bool>();
		settings.impostorBaseShade = std::clamp( properties["Impostor base shade"].data<float>(), 0.0f, 1.0f );
		settings.impostorFrameSize = frameSizeFromIndex( properties["Impostor frame size"].data<int32_t>() );
		settings.impostorDensity = std::max( properties["Impostor density"].data<float>(), 0.0f );
		settings.impostorOcclusion = std::clamp( properties["Impostor occlusion"].data<float>(), 0.0f, 1.0f );
	}
	return settings;
}

void Scatterer::applyProperties( Layer& layer )
{
	const ScatterLayerSettings settings = settingsFromProperties( layer );
	const ScatterLayerSettings& applied = layer.settings;

	bool weights = false;
	bool variantSizes = false;		// размер варианта: высота тени и запас кластеров постоянного слоя
	bool variantPlacement = false;	// разброс, наклон, группа куртин — константы раскладки
	bool patchGroups = false;		// группа куртин при куртинах — ёмкость списков
	const bool patches = settings.patchScale > 0.0f && settings.patchStrength > 0.0f;
	const bool appliedPatches = applied.patchScale > 0.0f && applied.patchStrength > 0.0f;
	for( uint32_t v = 0; v < layer.variants.size(); ++v )
	{
		LayerVariant& variant = layer.variants[v];
		variant.castShadow = ( *variant.properties )["Cast shadow"].data<bool>();
		const float weight = std::max( ( *variant.properties )["Weight"].data<float>(), 0.0f );
		if( v >= layer.passVariants.size() )
			continue;
		const ScatterPass::Variant& current = layer.passVariants[v];
		weights = weights || weight != current.weight;
		ScatterPass::Variant edited = current;
		applyPlacement( placementFromProperties( *variant.properties, variant.prefersProperties.get(), m_groundLayers ), v, edited, m_groundLayers );
		variantSizes = variantSizes || edited.sizeMultiplier != current.sizeMultiplier;
		variantPlacement = variantPlacement || edited.sizeVariation != current.sizeVariation || edited.alignToTerrain != current.alignToTerrain ||
						   edited.patchGroup != current.patchGroup;
		// Группа куртин при куртинах и предпочтение слоёв меняют ёмкость списков — буферы заново
		patchGroups = patchGroups || ( patches && edited.patchGroup != current.patchGroup ) || edited.groundLayers != current.groundLayers ||
					  edited.groundPreference != current.groundPreference;
	}
	// Кольца LOD: дальность импостера — до края кольца, границы LOD модели у варианта — по возрастанию и не дальше конца
	// последнего LOD модели (дальность импостера или край кольца); выход за пределы поле упирается, как в границу
	bool lods = false;
	ScatterLayerSettings clamped = settings;
	if( layer.properties->exists( "Impostor distance" ) )
	{
		clamped.impostorDistance = std::clamp( settings.impostorDistance, 0.1f, settings.farBorder );
		if( clamped.impostorDistance != settings.impostorDistance )
			( *layer.properties )["Impostor distance"].setData( clamped.impostorDistance );
		( *layer.properties )["Impostor distance"].setHigh( settings.farBorder );
		( *layer.properties )["Shadow impostor distance"].setHigh( clamped.impostorDistance );
	}
	for( uint32_t v = 0; v < layer.variants.size() && v < layer.passVariants.size(); ++v )
	{
		LayerVariant& variant = layer.variants[v];
		ScatterPass::Variant& passVariant = layer.passVariants[v];
		const float end = variant.impostor ? clamped.impostorDistance : clamped.farBorder;
		float previous = 0.0f;
		for( uint32_t i = 0; i + 1 < variant.modelLods; ++i )
		{
			Property& range = ( *variant.properties )[lodRangeName( i )];
			const float value = std::clamp( range.data<float>(), previous, end );
			if( value != range.data<float>() )
				range.setData( value );
			lods = lods || value != passVariant.lodEnd[i];
			previous = value;
		}
		if( variant.impostor && variant.modelLods > 0 )
			lods = lods || passVariant.lodEnd[variant.modelLods - 1] != clamped.impostorDistance;
	}
	// Исчезание дизерингом — экземпляры полос исчезания в пуле перехода: его ёмкость зависит от полос
	const bool fadeBands = settings.ditherFade != applied.ditherFade ||
						   ( settings.ditherFade && ( settings.nearFade != applied.nearFade || settings.farFade != applied.farFade ||
													  settings.thinStart != applied.thinStart || settings.thinning != applied.thinning ) );
	const bool buffers = weights || lods || fadeBands || variantSizes || patchGroups || patches != appliedPatches ||
						 settings.cellSize != applied.cellSize || settings.nearBorder != applied.nearBorder ||
						 settings.farBorder != applied.farBorder || settings.sizeMultiplier != applied.sizeMultiplier;
	const bool placement = settings.nearFade != applied.nearFade || settings.farFade != applied.farFade ||
						   settings.jitter != applied.jitter || settings.sizeVariation != applied.sizeVariation ||
						   settings.groundColor != applied.groundColor || settings.groundColorHeight != applied.groundColorHeight ||
						   settings.groundColorFar != applied.groundColorFar || settings.colorVariation != applied.colorVariation ||
						   settings.edgeSpread != applied.edgeSpread || settings.thinStart != applied.thinStart ||
						   settings.thinning != applied.thinning || variantPlacement ||
						   settings.patchScale != applied.patchScale || settings.patchStrength != applied.patchStrength ||
						   settings.layerFilter != applied.layerFilter || settings.layerFilterThreshold != applied.layerFilterThreshold ||
						   settings.alignToTerrain != applied.alignToTerrain ||
						   settings.rotationRange.x != applied.rotationRange.x || settings.rotationRange.y != applied.rotationRange.y ||
						   settings.rotationRange.z != applied.rotationRange.z;
	// Пока ползунок тянут или в поле набирают число (Property::editing), дорогое ждёт: новые буферы слоя (шаг, кольцо,
	// размер, веса — иначе каждый кадр новые буферы, а прежние живут до конца кадров в полёте) и раскладка постоянного слоя
	// на всю карту. Применяется один раз, когда правка закончилась; раскладка обычного слоя — константы, на лету
	if( ( buffers || ( placement && settings.persistent ) ) && layer.properties->editing() )
		return;
	for( uint32_t v = 0; v < layer.variants.size() && v < layer.passVariants.size(); ++v )
	{
		LayerVariant& variant = layer.variants[v];
		ScatterPass::Variant& passVariant = layer.passVariants[v];
		passVariant.weight = std::max( ( *variant.properties )["Weight"].data<float>(), 0.0f );
		applyPlacement( placementFromProperties( *variant.properties, variant.prefersProperties.get(), m_groundLayers ), v, passVariant, m_groundLayers );
		for( uint32_t i = 0; i + 1 < variant.modelLods; ++i )
			passVariant.lodEnd[i] = ( *variant.properties )[lodRangeName( i )].data<float>();
		if( variant.impostor && variant.modelLods > 0 )
			passVariant.lodEnd[variant.modelLods - 1] = clamped.impostorDistance;
	}
	layer.settings = clamped;
	if( buffers )
	{
		const bool rebuilt = rebuildPass( layer );
		LOG( "Scatter layer " + layer.mask + ( rebuilt ? ": buffers rebuilt after an edit" : ": buffers are not rebuilt" ) );
		return;
	}
	if( placement )
	{
		// Тень и дальность импостера тени — каждый кадр в compute; здесь — константы раскладки
		ScatterPass::PopulateParams& params = layer.pass->populateParams();
		const float castShadow = params.castShadow;
		ScatterPass::PopulateParams fresh;
		fillParams( fresh, settings );
		params.nearFade = fresh.nearFade;
		params.farFade = fresh.farFade;
		params.jitter = fresh.jitter;
		params.sizeVariation = fresh.sizeVariation;
		params.groundColor = fresh.groundColor;
		params.groundColorHeight = fresh.groundColorHeight;
		params.groundColorFar = fresh.groundColorFar;
		params.colorVariation = fresh.colorVariation;
		params.edgeSpread = fresh.edgeSpread;
		params.thinStart = fresh.thinStart;
		params.thinning = fresh.thinning;
		params.patchScale = fresh.patchScale;
		params.patchStrength = fresh.patchStrength;
		params.groundLayers = fresh.groundLayers;
		params.groundThreshold = fresh.groundThreshold;
		params.rotationRange = fresh.rotationRange;
		params.alignToTerrain = fresh.alignToTerrain;
		params.castShadow = castShadow;
		for( uint32_t v = 0; v < layer.passVariants.size(); ++v )
			layer.pass->setVariantPlacement( v, layer.passVariants[v] );
		// Постоянный слой разложен один раз — раскладка заново
		layer.placed = false;
	}
}

bool Scatterer::rebuildPass( Layer& layer )
{
	const ScatterLayerSettings& settings = layer.settings;
	auto pass = std::make_unique<ScatterPass>();
	fillParams( pass->populateParams(), settings );
	// Последний LOD (у леса — импостер) — до конца кольца
	for( ScatterPass::Variant& variant : layer.passVariants )
		variant.lodEnd[variant.lodCount - 1] = settings.farBorder;
	if( !pass->createBuffers( layer.passVariants, settings.persistent ) )
		return false;
	if( settings.persistent && ( !m_terrain || !pass->createPersistent( m_terrain->terrainHeight().worldSize, maxRadius( layer ), placedCapacity( settings ) ) ) )
		return false;
	// Прежний пул и списки ещё в кадрах GPU: ресурсы отпускаются отложенно (DMD3D::deferRelease)
	layer.pass = std::move( pass );
	layer.maxHeight = maxHeight( layer );
	layer.placed = false;
	return true;
}

void Scatterer::setStreamStones( const std::vector<TerrainHydrology::Stone>& stones )
{
	m_streamStones = stones;
	for( Layer& layer : m_layers )
		if( layer.settings.placement == ScatterPlacement::streamStones )
			layer.placed = false;
}

void Scatterer::placeStreamStones( Layer& layer )
{
	// Веса вариантов — из окна (как у раскладки по маске)
	std::vector<float> weights;
	float total = 0.0f;
	for( const LayerVariant& variant : layer.variants )
	{
		weights.push_back( std::max( ( *variant.properties )["Weight"].data<float>(), 0.0f ) );
		total += weights.back();
	}
	std::vector<ScatterPass::PlacedInstance> instances;
	instances.reserve( m_streamStones.size() );
	float tallest = 0.0f;
	for( const TerrainHydrology::Stone& stone : m_streamStones )
	{
		// Случайные числа камня — PCG от его числа (Jarzynski, Olano 2020): одна и та же модель и поворот при каждой загрузке
		uint32_t state = stone.seed;
		auto random = [&state]()
		{
			state = state * 747796405u + 2891336453u;
			const uint32_t word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
			return static_cast<float>( ( ( word >> 22u ) ^ word ) / 4294967296.0 );
		};
		float pick = random() * total;
		uint32_t v = 0;
		while( v + 1 < layer.variants.size() && pick >= weights[v] )
			pick -= weights[v++];
		const BoundingBox& box = layer.variants[v].box;
		// Поперечник модели — средний по X и Z; камень утоплен в дно на долю своей высоты
		const float width = std::max( box.Extents.x + box.Extents.z, 1e-3f );
		const float scale = stone.size / width * layer.settings.sizeMultiplier * layer.passVariants[v].sizeMultiplier;
		const float bottom = box.Center.y - box.Extents.y;
		ScatterPass::PlacedInstance instance;
		instance.position = stone.position;
		instance.position.y -= ( bottom + stoneEmbed * 2.0f * box.Extents.y ) * scale;
		instance.size = scale;
		// Поворот: вокруг вертикали — любой, наклон — небольшой вокруг случайной горизонтальной оси
		const float yaw = random() * XM_2PI;
		const float axis = random() * XM_2PI;
		const float tilt = ( random() - 0.5f ) * 2.0f * stoneTilt;
		const XMVECTOR rotation = XMQuaternionMultiply( XMQuaternionRotationAxis( XMVectorSet( std::cos( axis ), 0.0f, std::sin( axis ), 0.0f ), tilt ),
														XMQuaternionRotationAxis( XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f ), yaw ) );
		XMStoreFloat4( &instance.rotation, rotation );
		instance.variant = v;
		// Своя доля дальностей LOD, как у раскладки по маске (Shaders\lod_transition.h)
		instance.lodScale = 1.0f - ( LOD_JITTER_RANDOM + LOD_JITTER_NOISE ) * random();
		tallest = std::max( tallest, ( box.Center.y + box.Extents.y - bottom ) * scale );
		instances.push_back( instance );
	}
	const size_t dropped = layer.pass->placeInstances( instances, layer.passVariants );
	// Длина тени — по самому высокому камню
	layer.maxHeight = std::max( 1.0f, tallest );
	LOG( "Scatter layer " + std::to_string( layer.settings.id ) + ": stream stones " + std::to_string( instances.size() - dropped ) +
		 ( dropped > 0 ? ", " + std::to_string( dropped ) + " do not fit" : std::string() ) );
}

ScatterSetSettings Scatterer::settings() const
{
	ScatterSetSettings settings;
	settings.id = m_setId;
	settings.shadowLodScale = m_properties["Shadow LOD scale"].data<float>();
	return settings;
}

std::vector<ScatterLayerRecord> Scatterer::layerRecords() const
{
	std::vector<ScatterLayerRecord> records;
	for( const Layer& layer : m_layers )
	{
		ScatterLayerRecord record;
		record.settings = settingsFromProperties( layer );
		for( const LayerVariant& variant : layer.variants )
		{
			ScatterModelSettings model;
			model.id = variant.id;
			model.weight = std::max( ( *variant.properties )["Weight"].data<float>(), 0.0f );
			model.castShadow = ( *variant.properties )["Cast shadow"].data<bool>();
			model.placement = placementFromProperties( *variant.properties, variant.prefersProperties.get(), m_groundLayers );
			// Свои границы LOD — если хоть одна отличается от модели; иначе пусто (в базе NULL — дальности модели)
			bool own = false;
			for( uint32_t i = 0; i + 1 < variant.modelLods; ++i )
			{
				const float range = ( *variant.properties )[lodRangeName( i )].data<float>();
				model.lodDistances.push_back( range );
				own = own || std::fabs( range - variant.model->lodRange( static_cast<uint16_t>( i ) ) ) > 1e-3f;
			}
			if( !own )
				model.lodDistances.clear();
			record.models.push_back( model );
		}
		records.push_back( std::move( record ) );
	}
	return records;
}

void Scatterer::setComputeEnabled( bool enabled )
{
	m_computeEnabled = enabled;
}

bool Scatterer::computeEnabled() const
{
	return m_computeEnabled;
}

}
