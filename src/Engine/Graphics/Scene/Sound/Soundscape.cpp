#include "Soundscape.h"
#include <algorithm>
#include <cmath>
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"
#include "Light\DMLightDriver.h"
#include "SoundPlace.h"
#include "Terrain\TerrainPipeline.h"
#include "Utils\utilites.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;
// Громкость певца задана в столько метрах; ближе не растёт
constexpr float referenceDistance = 10.0f;
// Края часов слоя: активность растёт и спадает за столько часов
constexpr float hourFade = 0.75f;
// Попыток найти место певцу по маскам
constexpr int32_t placeTries = 16;
// Слушатель ушёл: певец дальше верхней границы полосы во столько раз — улетает (допевает фразу)
constexpr float leaveDistance = 1.5f;
// Новый певец прилетает через столько секунд после прошлого (не все сразу)
constexpr float spawnDelayFrom = 2.0f;
constexpr float spawnDelayTo = 8.0f;
// Кадр дольше (загрузка, точка останова) — время певцов идёт не больше чем на столько, с
constexpr float maxStep = 0.25f;

}

Soundscape::Soundscape() : SceneObject( "Soundscape" )
{
	m_properties.setName( "Soundscape" );
}

Soundscape::~Soundscape()
{
	if( !m_mixer )
		return;
	for( const auto& layer : m_layers )
		for( const Singer& singer : layer->singers )
			if( singer.voice )
				m_mixer->removeVoice( singer.voice );
}

float Soundscape::uniform( float from, float to )
{
	return from + ( to - from ) * std::uniform_real_distribution<float>( 0.0f, 1.0f )( m_random );
}

bool Soundscape::initialize( const std::vector<Layer>& layers, const std::vector<SoundRecord>& sounds, AudioMixer& mixer,
							 const TerrainPipeline* terrain, DMLightDriver& lights )
{
	for( const Layer& settings : layers )
	{
		auto layer = std::make_unique<LayerState>();
		layer->settings = settings;
		layer->properties.setName( settings.name );
		addSettingProperties( layer->properties, soundscapeLayerSchema(), settings, "" );
		m_properties.addSubContainer( &layer->properties );
		// Записи слоя — имена Sounds через запятую: у каждого певца — своя из них
		std::vector<std::string> names;
		str_split( settings.sound, names, ", " );
		for( const std::string& name : names )
		{
			const auto record = std::find_if( sounds.begin(), sounds.end(), [&]( const SoundRecord& s ) { return s.name == name; } );
			if( record == sounds.end() )
				continue;
			std::string reason;
			std::shared_ptr<const AudioClip> clip = AudioClip::load( record->file, record->phrases, reason );
			if( clip && !clip->phrases.empty() )
			{
				LOG( "Soundscape layer " + settings.name + ": sound " + record->name + ", " + std::to_string( clip->phrases.size() ) +
					 " phrases" + ( record->author.empty() ? std::string() : ", by " + record->author ) +
					 ( record->license.empty() ? std::string() : " (" + record->license + ")" ) );
				layer->clips.push_back( std::move( clip ) );
			}
			else
			{
				LOG( "Soundscape layer " + settings.name + ": sound " + record->name + " is skipped: " + reason );
			}
		}
		if( layer->clips.empty() )
			LOG( "Soundscape layer " + settings.name + " is silent: no sounds loaded" );
		layer->spawnDelay = uniform( 0.0f, spawnDelayFrom );
		m_layers.push_back( std::move( layer ) );
	}
	m_mixer = &mixer;
	m_terrain = terrain;
	m_lights = &lights;
	return true;
}

Soundscape::Layer Soundscape::current( const LayerState& layer ) const
{
	Layer settings = layer.settings;
	readSettingProperties( layer.properties, soundscapeLayerSchema(), settings, "" );
	return settings;
}

std::vector<Soundscape::Layer> Soundscape::layerSettings() const
{
	std::vector<Layer> result;
	for( const auto& layer : m_layers )
		result.push_back( current( *layer ) );
	return result;
}

float Soundscape::activity( const XMFLOAT2& hours, float hour )
{
	// «От» больше «до» — через полночь: часы после полуночи — за 24
	float from = hours.x;
	float to = hours.y;
	if( to < from )
	{
		to += 24.0f;
		if( hour < from - hourFade )
			hour += 24.0f;
	}
	const float rise = std::clamp( ( hour - from ) / hourFade + 0.5f, 0.0f, 1.0f );
	const float fall = std::clamp( ( to - hour ) / hourFade + 0.5f, 0.0f, 1.0f );
	return rise * fall;
}

bool Soundscape::placeSinger( const Layer& settings, const XMFLOAT3& listener, Singer& singer )
{
	std::vector<std::string> names;
	str_split( settings.habitat, names, ", " );
	const auto habitat = terrainMasks( m_terrain, names );
	for( int32_t attempt = 0; attempt < placeTries; ++attempt )
	{
		// Равномерно по площади кольца
		const float angle = uniform( 0.0f, 2.0f * pi );
		const float inner = std::max( settings.distance.from, 0.0f );
		const float outer = std::max( settings.distance.to, inner );
		const float radius = std::sqrt( uniform( inner * inner, outer * outer ) );
		const float x = listener.x + radius * std::sin( angle );
		const float z = listener.z + radius * std::cos( angle );
		if( !names.empty() && uniform( 0.0f, 1.0f ) >= terrainMaskAt( m_terrain, habitat, x, z ) )
			continue;
		float ground = listener.y - 1.7f;
		if( m_terrain )
			m_terrain->surfaceHeight( x, z, ground );
		singer.position = XMFLOAT3( x, ground + uniform( settings.height.from, std::max( settings.height.to, settings.height.from ) ), z );
		singer.pitch = 1.0f + uniform( -settings.pitch, settings.pitch );
		singer.boutLeft = uniform( settings.bout.from, std::max( settings.bout.to, settings.bout.from ) );
		// Первая фраза — скоро: прилетел и запел
		singer.nextPhrase = uniform( 0.0f, std::max( settings.pause.from, 0.5f ) );
		return true;
	}
	return false;
}

void Soundscape::update( const FrameContext& frame )
{
	if( !m_mixer )
		return;
	const float step = std::clamp( frame.elapsedTime / 1000.0f, 0.0f, maxStep );
	float hour = 12.0f;
	if( const SunPosition* sun = m_lights->sunPosition() )
		hour = sun->settings().timeOfDay;
	const AudioListener listener = listenerFromView( frame.view );

	for( const auto& pointer : m_layers )
	{
		LayerState& layer = *pointer;
		const Layer settings = current( layer );
		const bool audible = visible() && !layer.clips.empty() && settings.volume > 0.0f;
		const int32_t wanted = audible ? static_cast<int32_t>( std::floor( settings.singers * activity( settings.hours, hour ) + 0.5f ) ) : 0;

		// Певцы: фразы с паузами, конец «bout» — улетает (допев фразу); слушатель ушёл далеко — тоже
		int32_t singing = 0;
		for( Singer& singer : layer.singers )
		{
			singer.boutLeft -= step;
			const float dx = singer.position.x - listener.position.x;
			const float dz = singer.position.z - listener.position.z;
			if( !audible || std::sqrt( dx * dx + dz * dz ) > settings.distance.to * leaveDistance )
				singer.boutLeft = std::min( singer.boutLeft, 0.0f );
			if( singer.boutLeft > 0.0f && ++singing > wanted )
				singer.boutLeft = 0.0f;		// часы слоя кончаются — лишние улетают
			if( singer.voice )
			{
				singer.voiceLeft -= step;
				if( singer.voiceLeft <= 0.0f || !audible )
				{
					m_mixer->removeVoice( singer.voice );
					singer.voice = 0;
				}
				continue;
			}
			singer.nextPhrase -= step;
			if( singer.boutLeft <= 0.0f || singer.nextPhrase > 0.0f )
				continue;
			// Фраза записи — случайная, не та же подряд
			const std::shared_ptr<const AudioClip>& clip = layer.clips[std::min( singer.clip, layer.clips.size() - 1 )];
			const auto& phrases = clip->phrases;
			const size_t count = phrases.size();
			const size_t index = std::uniform_int_distribution<size_t>( 0, count - 1 )( m_random );
			auto generator = std::make_unique<ClipGenerator>( clip, phrases[index], singer.pitch );
			singer.voiceLeft = generator->seconds() + 0.05f;
			singer.voice = m_mixer->addVoice( std::move( generator ) );
			singer.nextPhrase = uniform( settings.pause.from, std::max( settings.pause.to, settings.pause.from ) );
		}
		layer.singers.erase( std::remove_if( layer.singers.begin(), layer.singers.end(),
											 []( const Singer& s ) { return s.boutLeft <= 0.0f && s.voice == 0; } ),
							 layer.singers.end() );

		// Новый певец — если их меньше, чем нужно в этот час, и прошлый прилетел не только что
		layer.spawnDelay -= step;
		const auto alive = std::count_if( layer.singers.begin(), layer.singers.end(), []( const Singer& s ) { return s.boutLeft > 0.0f; } );
		if( alive < wanted && layer.spawnDelay <= 0.0f )
		{
			Singer singer;
			if( placeSinger( settings, listener.position, singer ) )
			{
				singer.clip = std::uniform_int_distribution<size_t>( 0, layer.clips.size() - 1 )( m_random );
				layer.singers.push_back( singer );
			}
			layer.spawnDelay = uniform( spawnDelayFrom, spawnDelayTo );
		}

		// Звучащие фразы: с места певца — затухание, воздух, гребень
		for( const Singer& singer : layer.singers )
		{
			if( !singer.voice )
				continue;
			const XMFLOAT3 direction( singer.position.x - listener.position.x, singer.position.y - listener.position.y,
									  singer.position.z - listener.position.z );
			const float distance = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
			const float occlusion = terrainOcclusion( m_terrain, listener.position, singer.position );
			AudioVoiceTarget target;
			target.lowpass = std::min( airAbsorptionLowpass( distance ), occlusionLowpass( occlusion ) );
			spatialize( listener, direction, settings.volume * std::min( referenceDistance / std::max( distance, 1e-3f ), 1.0f ) *
												 occlusionGain( occlusion ), target );
			m_mixer->setTarget( singer.voice, target );
		}
	}
}

}
