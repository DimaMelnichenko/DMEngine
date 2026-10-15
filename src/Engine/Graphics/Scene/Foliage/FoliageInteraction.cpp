#include "FoliageInteraction.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "ConstantBuffers.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Поле — 384² клеток по 12,5 см: окно 48 м вокруг камеры (след за спиной идущего живёт ~20 м — дольше, чем трава
// распрямляется); клетка — меньше шага травы (0,2 м), наклон пучка читается в его корне
constexpr uint32_t fieldResolution = 384;
constexpr float fieldCellSize = 0.125f;
// Перескок камеры дальше — новое место (команда camera, телепорт), а не шаг: след не тянется через полкарты
constexpr float teleportDistance = 3.0f;
// Быстрее — трава под ногами ложится вперёд по ходу, медленнее — расступается от ног во все стороны
constexpr float movingSpeed = 0.2f;	// м/с

Property* addValue( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit,
					const char* tooltip )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setUnit( unit )->setTooltip( tooltip );
	return property;
}

int32_t floorToCell( float coordinate )
{
	return static_cast<int32_t>( std::floor( coordinate / fieldCellSize ) );
}

}

FoliageInteraction::FoliageInteraction() : SceneObject( "Foliage interaction" )
{
	m_properties.setName( "Foliage interaction" );
	m_properties.insert( "Enabled", true )->setTooltip( "Plants bend away from the camera near the ground (walking, low flight)" );
	addValue( m_properties, "Trail width", 0.9f, 0.1f, 4.0f, "m", "Width of the trail: plants within it bend fully" );
	addValue( m_properties, "Edge width", 0.22f, 0.0f, 2.0f, "m",
			  "Soft border on each side of the trail: towards its outer side plants bend less and less" );
	addValue( m_properties, "Body height", 2.0f, 0.3f, 5.0f, "m",
			  "The camera presses plants when it is this close to the ground: walking (eyes at 1.7 m) or low flight" );
	addValue( m_properties, "Max angle", 80.0f, 0.0f, 89.0f, "deg",
			  "Lean of a fully pressed plant: the stem bends over its bend length (TrampleBendLength of the material) up to this angle "
			  "and lies straight above, times TrampleWeight of the material" );
	addValue( m_properties, "Recovery time", 6.0f, 0.5f, 60.0f, "s", "Plants stand up again after the trail: 95 % in this time" );
	addValue( m_properties, "Forward push", 0.5f, 0.0f, 1.0f, "",
			  "Under moving feet plants lie along the way (1) or apart from the feet (0)" );
}

bool FoliageInteraction::initialize( const TerrainHeightSource* terrain )
{
	m_terrain = terrain;
	DMD3D& d3d = DMD3D::instance();
	if( !m_shader.initialize( "Shaders\\foliage_interaction.cs", "main" ) ||
		!d3d.createShaderConstantBuffer( sizeof( FoliageInteractionBuffer ), m_constants ) )
	{
		LOG( "Foliage interaction: shader or constants are not created" );
		return false;
	}
	for( uint32_t i = 0; i < 2; ++i )
	{
		TextureDesc desc;
		desc.width = fieldResolution;
		desc.height = fieldResolution;
		desc.format = DXGI_FORMAT_R16G16_FLOAT;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, m_fields[i] ) )
			return false;
		d3d.setName( m_fields[i], i == 0 ? "Foliage interaction 0" : "Foliage interaction 1" );
		if( !d3d.createStorageView( m_fields[i], {}, m_fieldUAV[i] ) || !d3d.createShaderView( m_fields[i], {}, m_fieldSRV[i] ) )
			return false;
	}
	m_initialized = true;
	return true;
}

bool FoliageInteraction::enabled() const
{
	return m_initialized && m_properties["Enabled"].data<bool>();
}

void FoliageInteraction::update( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	m_frameSeconds = frame.elapsedTime / 1000.0f;
	// Окно — клетки вокруг камеры, привязанные к миру: поле не плывёт, при движении сдвигается целыми клетками, и клетка
	// остаётся в той же ячейке текстуры (адрес — клетка по модулю размера, как у тороидального clipmap)
	m_previousOrigin[0] = m_origin[0];
	m_previousOrigin[1] = m_origin[1];
	m_origin[0] = floorToCell( frame.view.position.x ) - static_cast<int32_t>( fieldResolution / 2 );
	m_origin[1] = floorToCell( frame.view.position.z ) - static_cast<int32_t>( fieldResolution / 2 );
	if( enabled() )
		addCameraInteractor( frame );
	else
	{
		m_hasFeet = false;
		m_cleared = false;	// снова включили — с чистого поля
	}
}

void FoliageInteraction::addCameraInteractor( const FrameContext& frame )
{
	const XMFLOAT3& camera = frame.view.position;
	float ground = 0.0f;
	if( !m_terrain || !m_terrain->surfaceHeight( camera.x, camera.z, ground ) )
	{
		m_hasFeet = false;
		return;
	}
	// Тело — от глаз до земли: ниже «Body height» над землёй камера идёт по траве, выше — сходит на нет за 0,3 м
	const float bodyHeight = m_properties["Body height"].data<float>();
	const float strength = 1.0f - std::clamp( ( camera.y - ground - bodyHeight ) / 0.3f, 0.0f, 1.0f );
	const XMFLOAT2 feet( camera.x, camera.z );
	if( strength <= 0.0f )
	{
		m_hasFeet = false;
		return;
	}

	Interactor interactor;
	interactor.from = feet;
	interactor.to = feet;
	interactor.radius = 0.5f * m_properties["Trail width"].data<float>();
	interactor.edge = m_properties["Edge width"].data<float>();
	interactor.strength = strength;
	if( m_hasFeet )
	{
		const float dx = feet.x - m_previousFeet.x;
		const float dz = feet.y - m_previousFeet.y;
		const float distance = std::sqrt( dx * dx + dz * dz );
		if( distance < teleportDistance )
		{
			interactor.from = m_previousFeet;
			if( distance > movingSpeed * std::max( m_frameSeconds, 1e-3f ) )
			{
				const float forward = m_properties["Forward push"].data<float>() / distance;
				interactor.direction = XMFLOAT2( dx * forward, dz * forward );
			}
		}
	}
	m_previousFeet = feet;
	m_hasFeet = true;
	addInteractor( interactor );
}

void FoliageInteraction::addInteractor( const Interactor& interactor )
{
	if( m_interactors.size() < maxInteractors )
		m_interactors.push_back( interactor );
}

void FoliageInteraction::compute( const FrameContext& frame )
{
	if( !enabled() )
	{
		m_interactors.clear();
		return;
	}

	FoliageInteractionBuffer params = {};
	params.origin = XMINT2( m_origin[0], m_origin[1] );
	// Первый кадр (или снова включили): прошлого поля нет — прошлое окно далеко, все клетки начинаются с нуля
	const int32_t shift = m_cleared ? 0 : static_cast<int32_t>( fieldResolution ) * 4;
	params.previousOrigin = XMINT2( m_previousOrigin[0] + shift, m_previousOrigin[1] + shift );
	params.cellSize = fieldCellSize;
	// Распрямление: за «Recovery time» остаётся 5 % (e^−3)
	const float recovery = std::max( m_properties["Recovery time"].data<float>(), 0.01f );
	params.keep = std::exp( -3.0f * m_frameSeconds / recovery );
	params.resolution = fieldResolution;
	params.interactorCount = static_cast<uint32_t>( m_interactors.size() );
	for( size_t i = 0; i < m_interactors.size(); ++i )
	{
		const Interactor& interactor = m_interactors[i];
		params.segments[i] = XMFLOAT4( interactor.from.x, interactor.from.y, interactor.to.x, interactor.to.y );
		params.shapes[i] = XMFLOAT4( interactor.radius, interactor.strength, interactor.direction.x, interactor.direction.y );
		params.edges[i] = XMFLOAT4( interactor.edge, 0.0f, 0.0f, 0.0f );
	}
	m_interactors.clear();

	DMD3D& d3d = DMD3D::instance();
	const uint32_t previous = m_current;
	m_current ^= 1;
	Device::updateResourceData<FoliageInteractionBuffer>( m_constants, params );
	d3d.beginPass( PassDesc{ "Foliage interaction", {}, {}, 0, 0, { { &m_fieldSRV[previous], "foliage interaction (previous)" } },
							 { { &m_fieldUAV[m_current], "foliage interaction" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 0, m_fieldSRV[previous] );
	m_shader.setUAVBuffer( 0, m_fieldUAV[m_current] );
	m_shader.dispatchGroups( fieldResolution / 8, fieldResolution / 8, 1 );
	// Растениям (Shaders/foliage_interaction.sh): привязка завершает проход над полем
	d3d.setSRV( SLOT_FOLIAGE_INTERACTION, m_fieldSRV[m_current] );
	m_cleared = true;
}

void FoliageInteraction::frameConstants( const RenderView&, FrameParameters& constants )
{
	if( !enabled() )
		return;
	constants.foliageInteraction = XMFLOAT4( static_cast<float>( m_origin[0] ) * fieldCellSize, static_cast<float>( m_origin[1] ) * fieldCellSize,
					 1.0f / ( static_cast<float>( fieldResolution ) * fieldCellSize ),
					 XMConvertToRadians( m_properties["Max angle"].data<float>() ) );
}

}
