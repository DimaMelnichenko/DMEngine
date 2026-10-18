#include "Soundscape.h"
#include <algorithm>
#include <cmath>
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"
#include "Engine\Simulation\WorldClock.h"
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
// Кадр дольше (загрузка, точка останова) — время певцов идёт не больше чем на столько, с
constexpr float maxStep = 0.25f;

bool contains( const std::vector<std::string>& names, const std::string& name )
{
	return std::find( names.begin(), names.end(), name ) != names.end();
}

// Отмеченные флажки — текстом через запятую, как в базе: сначала прежние по их порядку в before (без правки строка не
// меняется; имя без флажка — записи, которой нет в Sounds, — остаётся), затем новые по порядку флажков
std::string checkedList( const PropertyContainer& flags, const std::string& before )
{
	std::vector<std::string> names;
	str_split( before, names, ", " );
	std::string result;
	auto append = [&result]( const std::string& name ) { result += ( result.empty() ? "" : "," ) + name; };
	for( const std::string& name : names )
		if( !flags.exists( name ) || flags[name].data<bool>() )
			append( name );
	for( const std::string& name : flags.names() )
		if( flags[name].data<bool>() && !contains( names, name ) )
			append( name );
	return result;
}

}

Soundscape::Soundscape() : SceneObject( "Soundscape" )
{
	m_properties.setName( "Soundscape" );
	m_soundProperties.setName( "Sounds" );
}

Soundscape::~Soundscape()
{
	if( !m_mixer )
		return;
	for( const auto& layer : m_layers )
		silence( *layer );
	for( const auto& sound : m_sounds )
		stop( *sound );
}

float Soundscape::uniform( float from, float to )
{
	return from + ( to - from ) * std::uniform_real_distribution<float>( 0.0f, 1.0f )( m_random );
}

bool Soundscape::initialize( const std::vector<Layer>& layers, const std::vector<SoundRecord>& sounds, AudioMixer& mixer,
							 const TerrainPipeline* terrain, const WorldClock& clock )
{
	m_mixer = &mixer;
	m_terrain = terrain;
	m_clock = &clock;
	m_clearSolo = mixer.audition().clearRevision();

	// Записи — окно «Sounds»: громкость и прослушивание
	for( const SoundRecord& record : sounds )
	{
		auto sound = std::make_unique<Sound>();
		sound->record = record;
		sound->properties.setName( record.name );
		addSettingProperties( sound->properties, soundRecordSchema(), record, "" );
		Sound* pointer = sound.get();
		sound->properties.addButton( "Play", "Listen to the next phrase of the recording at the camera with the volume of the recording "
										 "(as a singer 10 m away with the layer volume 1)",
									 [this, pointer]() -> PropertyContainer::ButtonAction
		{
			play( *pointer );
			return {};
		} );
		sound->properties.addButton( "Stop", "Stop listening", [this, pointer]() -> PropertyContainer::ButtonAction
		{
			stop( *pointer );
			return {};
		} );
		m_soundProperties.addSubContainer( &sound->properties );
		m_sounds.push_back( std::move( sound ) );
	}

	// Места певцов: маски рельефа, берег озера и записанное у слоёв (маски нет у этого рельефа — флажок остаётся)
	if( terrain )
		m_habitats = terrain->maskNames();
	m_habitats.push_back( lakeShore );
	for( const Layer& settings : layers )
	{
		std::vector<std::string> names;
		str_split( settings.habitat, names, ", " );
		for( const std::string& name : names )
			if( !contains( m_habitats, name ) )
				m_habitats.push_back( name );
	}

	for( const Layer& settings : layers )
	{
		std::unique_ptr<LayerState> layer = createLayer( settings );
		// Звук записей включённого слоя — при загрузке: певцу не ждать чтения файла
		if( settings.enabled )
		{
			refreshLists( *layer );
			if( layer->sounds.empty() )
				LOG( "Soundscape layer " + settings.name + " is silent: no sounds loaded" );
		}
		// Первые певцы — не все сразу: в пределах «Arrival»
		layer->spawnDelay = uniform( 0.0f, std::max( settings.arrival.from, 0.0f ) );
		m_layers.push_back( std::move( layer ) );
	}
	m_properties.addButton( "Add layer", "A new layer of singers with default fields and no recording: tick its recordings and "
							"habitat (Save level inserts its row, Ctrl+Z removes it; rename it with Tools/soundscape.py set)",
							[this]() -> PropertyContainer::ButtonAction
	{
		const uint32_t key = addLayer();
		return { [this, key] { removeLayer( key ); }, [this, key] { restoreLayer( key ); }, "Add layer" };
	} );
	updateStructure();
	return true;
}

std::unique_ptr<Soundscape::LayerState> Soundscape::createLayer( const Layer& settings )
{
	auto layer = std::make_unique<LayerState>();
	layer->key = m_nextKey++;
	layer->settings = settings;
	layer->properties.setName( settings.name );
	addSettingProperties( layer->properties, soundscapeLayerSchema(), settings, "" );
	addAuditionProperties( layer->properties );

	// Записи и места — флажками: строкового свойства у окна нет
	std::vector<std::string> names;
	str_split( settings.sound, names, ", " );
	layer->soundFlags.setName( "Sounds" );
	for( const auto& sound : m_sounds )
	{
		const SoundRecord& record = sound->record;
		layer->soundFlags.insert( record.name, contains( names, record.name ) )->setTooltip(
			"Each singer sings one of the ticked recordings: " + record.file + ( record.author.empty() ? "" : ", by " + record.author ) +
			( record.license.empty() ? "" : " (" + record.license + ")" ) );
	}
	names.clear();
	str_split( settings.habitat, names, ", " );
	layer->habitatFlags.setName( "Habitat" );
	for( const std::string& habitat : m_habitats )
		layer->habitatFlags.insert( habitat, contains( names, habitat ) )->setTooltip(
			habitat == lakeShore ? std::string( "On the lake shore: points of the lake edge instead of the masks" )
								 : "Terrain mask: singers sit where it is (the sum of the ticked masks); none ticked - anywhere" );
	layer->properties.addSubContainer( &layer->soundFlags );
	layer->properties.addSubContainer( &layer->habitatFlags );

	const uint32_t key = layer->key;
	layer->properties.addButton( "Delete", "Remove the layer from the level (Save level deletes its row, Ctrl+Z returns it)",
								 [this, key]() -> PropertyContainer::ButtonAction
	{
		const auto found = std::find_if( m_layers.begin(), m_layers.end(), [key]( const auto& l ) { return l->key == key; } );
		if( found == m_layers.end() )
			return {};
		const std::string name = "Delete " + ( *found )->properties.name();
		removeLayer( key );
		return { [this, key] { restoreLayer( key ); }, [this, key] { removeLayer( key ); }, name };
	} );
	return layer;
}

uint32_t Soundscape::addLayer()
{
	// Имя — «Layer», с номером, если такое уже есть (и у убранных: их вернёт отмена)
	auto taken = [this]( const std::string& name )
	{
		auto same = [&name]( const auto& layer ) { return layer->properties.name() == name; };
		return std::any_of( m_layers.begin(), m_layers.end(), same ) || std::any_of( m_removed.begin(), m_removed.end(), same );
	};
	Layer settings;
	settings.name = "Layer";
	for( int number = 2; taken( settings.name ); ++number )
		settings.name = "Layer " + std::to_string( number );
	std::unique_ptr<LayerState> layer = createLayer( settings );
	// Значения по умолчанию — «сохранённые»: Details отмечает правки после них, а сам новый слой — до «Save level»
	layer->properties.markSaved();
	const uint32_t key = layer->key;
	m_layers.push_back( std::move( layer ) );
	updateStructure();
	return key;
}

bool Soundscape::removeLayer( uint32_t key )
{
	const auto found = std::find_if( m_layers.begin(), m_layers.end(), [key]( const auto& layer ) { return layer->key == key; } );
	if( found == m_layers.end() )
		return false;
	silence( **found );
	m_removed.push_back( std::move( *found ) );
	m_layers.erase( found );
	updateStructure();
	return true;
}

bool Soundscape::restoreLayer( uint32_t key )
{
	const auto found = std::find_if( m_removed.begin(), m_removed.end(), [key]( const auto& layer ) { return layer->key == key; } );
	if( found == m_removed.end() )
		return false;
	// На прежнее место: слои по ключу (порядок базы, затем добавленные)
	const auto at = std::find_if( m_layers.begin(), m_layers.end(), [key]( const auto& layer ) { return layer->key > key; } );
	m_layers.insert( at, std::move( *found ) );
	m_removed.erase( found );
	updateStructure();
	return true;
}

void Soundscape::updateStructure()
{
	m_properties.subContainer().clear();
	for( const auto& layer : m_layers )
	{
		// Нового слоя в базе ещё нет
		layer->properties.setStructureModified( layer->settings.id == 0 );
		m_properties.addSubContainer( &layer->properties );
	}
	// Убранный слой ещё есть в базе — окно изменено до «Save level»
	m_properties.setStructureModified( std::any_of( m_removed.begin(), m_removed.end(), []( const auto& layer ) { return layer->settings.id != 0; } ) );
}

void Soundscape::silence( LayerState& layer )
{
	for( const Singer& singer : layer.singers )
		for( const Singer::Phrase& phrase : singer.phrases )
			m_mixer->removeVoice( phrase.voice );
	layer.singers.clear();
}

Soundscape::Layer Soundscape::current( const LayerState& layer ) const
{
	Layer settings = layer.settings;
	readSettingProperties( layer.properties, soundscapeLayerSchema(), settings, "" );
	settings.sound = checkedList( layer.soundFlags, layer.settings.sound );
	settings.habitat = checkedList( layer.habitatFlags, layer.settings.habitat );
	return settings;
}

void Soundscape::refreshLists( LayerState& layer )
{
	const uint64_t revision = std::max( layer.soundFlags.revision(), layer.habitatFlags.revision() );
	if( revision == layer.listsRevision )
		return;
	layer.listsRevision = revision;
	layer.sound = checkedList( layer.soundFlags, layer.settings.sound );
	layer.habitat = checkedList( layer.habitatFlags, layer.settings.habitat );
	layer.sounds = layerSounds( layer );
}

std::vector<Soundscape::Layer> Soundscape::layerSettings() const
{
	std::vector<Layer> result;
	for( const auto& layer : m_layers )
		result.push_back( current( *layer ) );
	return result;
}

std::vector<uint32_t> Soundscape::removedIds() const
{
	std::vector<uint32_t> ids;
	for( const auto& layer : m_removed )
		if( layer->settings.id != 0 )
			ids.push_back( layer->settings.id );
	return ids;
}

std::vector<SoundRecord> Soundscape::soundRecords() const
{
	std::vector<SoundRecord> records;
	for( const auto& sound : m_sounds )
	{
		SoundRecord record = sound->record;
		readSettingProperties( sound->properties, soundRecordSchema(), record, "" );
		records.push_back( std::move( record ) );
	}
	return records;
}

void Soundscape::markSaved( const std::vector<Layer>& saved )
{
	// Записанное — новая основа слоя (id новой строки, порядок записей и мест в тексте)
	for( size_t i = 0; i < m_layers.size() && i < saved.size(); ++i )
	{
		m_layers[i]->settings = saved[i];
		m_layers[i]->listsRevision = ~0ull;
	}
	// Строк убранных больше нет: вернёт отмена — вставится заново
	for( const auto& layer : m_removed )
		layer->settings.id = 0;
	updateStructure();
}

const AudioClip* Soundscape::clipOf( Sound& sound )
{
	if( !sound.loaded )
	{
		sound.loaded = true;
		const SoundRecord& record = sound.record;
		std::string reason;
		sound.clip = AudioClip::load( record.file, record.phrases, reason );
		if( sound.clip && !sound.clip->phrases.empty() )
		{
			LOG( "Soundscape: sound " + record.name + ", " + std::to_string( sound.clip->phrases.size() ) + " phrases" +
				 ( record.author.empty() ? std::string() : ", by " + record.author ) +
				 ( record.license.empty() ? std::string() : " (" + record.license + ")" ) );
		}
		else
		{
			LOG( "Soundscape: sound " + record.name + " is skipped: " + ( reason.empty() ? std::string( "no phrases" ) : reason ) );
			sound.clip.reset();
		}
	}
	return sound.clip.get();
}

std::vector<size_t> Soundscape::layerSounds( const LayerState& layer )
{
	std::vector<size_t> sounds;
	for( size_t i = 0; i < m_sounds.size(); ++i )
		if( layer.soundFlags[m_sounds[i]->record.name].data<bool>() && clipOf( *m_sounds[i] ) )
			sounds.push_back( i );
	return sounds;
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

void Soundscape::buildShore()
{
	m_shore.clear();
	const TerrainHydrology::Result* hydrology = m_terrain ? m_terrain->hydrology() : nullptr;
	const uint32_t size = hydrology ? hydrology->size : 0;
	if( size == 0 || hydrology->staticWater.size() != static_cast<size_t>( size ) * size )
		return;
	const float world = m_terrain->worldSize();
	const float texel = world / size;
	auto lake = [&]( int32_t row, int32_t column )
	{
		return row >= 0 && column >= 0 && row < static_cast<int32_t>( size ) && column < static_cast<int32_t>( size ) &&
			   hydrology->staticWater[static_cast<size_t>( row ) * size + column].w > -1e8f;
	};
	for( int32_t row = 0; row < static_cast<int32_t>( size ); ++row )
		for( int32_t column = 0; column < static_cast<int32_t>( size ); ++column )
			if( lake( row, column ) && ( !lake( row - 1, column ) || !lake( row + 1, column ) || !lake( row, column - 1 ) || !lake( row, column + 1 ) ) )
				// Строка 0 — дальний край по z
				m_shore.push_back( XMFLOAT3( ( column + 0.5f ) * texel, hydrology->staticWater[static_cast<size_t>( row ) * size + column].w,
											 world - ( row + 0.5f ) * texel ) );
	LOG( "Soundscape: " + std::to_string( m_shore.size() ) + " points of lake shore" );
}

bool Soundscape::placeSinger( const Layer& settings, const XMFLOAT3& listener, Singer& singer )
{
	std::vector<std::string> names;
	str_split( settings.habitat, names, ", " );
	const float inner = std::max( settings.distance.from, 0.0f );
	const float outer = std::max( settings.distance.to, inner );
	const float height = uniform( settings.height.from, std::max( settings.height.to, settings.height.from ) );
	bool placed = false;
	if( contains( names, lakeShore ) )
	{
		// Берег озера: случайная точка края воды в полосе расстояний
		std::vector<const XMFLOAT3*> nearby;
		for( const XMFLOAT3& point : m_shore )
		{
			const float distance = std::hypot( point.x - listener.x, point.z - listener.z );
			if( distance >= inner && distance <= outer )
				nearby.push_back( &point );
		}
		if( !nearby.empty() )
		{
			const XMFLOAT3& point = *nearby[std::uniform_int_distribution<size_t>( 0, nearby.size() - 1 )( m_random )];
			singer.position = XMFLOAT3( point.x, point.y + height, point.z );
			placed = true;
		}
	}
	else
	{
		const auto habitat = terrainMasks( m_terrain, names );
		for( int32_t attempt = 0; attempt < placeTries && !placed; ++attempt )
		{
			// Равномерно по площади кольца
			const float angle = uniform( 0.0f, 2.0f * pi );
			const float radius = std::sqrt( uniform( inner * inner, outer * outer ) );
			const float x = listener.x + radius * std::sin( angle );
			const float z = listener.z + radius * std::cos( angle );
			if( !names.empty() && uniform( 0.0f, 1.0f ) >= terrainMaskAt( m_terrain, habitat, x, z ) )
				continue;
			float ground = listener.y - 1.7f;
			if( m_terrain )
				m_terrain->surfaceHeight( x, z, ground );
			singer.position = XMFLOAT3( x, ground + height, z );
			placed = true;
		}
	}
	if( placed )
	{
		singer.pitch = 1.0f + uniform( -settings.pitch, settings.pitch );
		singer.boutLeft = uniform( settings.bout.from, std::max( settings.bout.to, settings.bout.from ) );
		// Первая фраза — скоро: прилетел и запел
		singer.nextPhrase = uniform( 0.0f, std::clamp( settings.pause.from, 0.5f, 4.0f ) );
	}
	return placed;
}

void Soundscape::play( Sound& sound )
{
	stop( sound );
	const AudioClip* clip = clipOf( sound );
	if( !clip )
		return;
	const AudioClip::Phrase& phrase = clip->phrases[sound.previewPhrase++ % clip->phrases.size()];
	auto generator = std::make_unique<ClipGenerator>( sound.clip, phrase, 1.0f );
	sound.previewLeft = generator->seconds() + 0.05f;
	sound.preview = m_mixer->addVoice( std::move( generator ) );
}

void Soundscape::stop( Sound& sound )
{
	if( sound.preview == 0 )
		return;
	m_mixer->removeVoice( sound.preview );
	sound.preview = 0;
}

void Soundscape::updatePreviews( float step, const AudioListener& listener )
{
	// Прослушивание — мимо видимости, Mute и Solo слоёв: слушают саму запись
	for( const auto& pointer : m_sounds )
	{
		Sound& sound = *pointer;
		if( sound.preview == 0 )
			continue;
		sound.previewLeft -= step;
		if( sound.previewLeft <= 0.0f )
		{
			stop( sound );
			continue;
		}
		AudioVoiceTarget target;
		spatialize( listener, listener.forward, std::max( sound.properties["Volume"].data<float>(), 0.0f ), target, 1.0f );
		m_mixer->setTarget( sound.preview, target );
	}
}

void Soundscape::update( const FrameContext& frame )
{
	if( !m_mixer )
		return;
	AudioAudition& audition = m_mixer->audition();
	if( takeClearSolo( audition, m_clearSolo ) )
		for( const auto& layer : m_layers )
			clearSolo( layer->properties );
	const float step = std::clamp( frame.elapsedTime / 1000.0f, 0.0f, maxStep );
	const float hour = static_cast<float>( m_clock->hours() );
	const AudioListener listener = listenerFromView( frame.view );
	if( m_terrain && m_terrain->heightRevision() != m_shoreRevision )
	{
		m_shoreRevision = m_terrain->heightRevision();
		buildShore();
	}
	updatePreviews( step, listener );

	for( const auto& pointer : m_layers )
	{
		LayerState& layer = *pointer;
		// Поля окна — каждый кадр, записи и места — по правке флажков (звук записей читается, когда слой включён)
		Layer settings = layer.settings;
		readSettingProperties( layer.properties, soundscapeLayerSchema(), settings, "" );
		if( settings.enabled )
			refreshLists( layer );
		settings.sound = layer.sound;
		settings.habitat = layer.habitat;
		static const std::vector<size_t> none;
		const std::vector<size_t>& sounds = settings.enabled ? layer.sounds : none;
		// Прослушивание: Mute — певцы поют беззвучно, Solo другого слоя — тоже
		const float heard = auditionGain( audition, layer.properties, settings.enabled );
		const bool audible = visible() && settings.enabled && !sounds.empty() && settings.volume > 0.0f;
		const int32_t wanted = audible ? static_cast<int32_t>( std::floor( settings.singers * activity( settings.hours, hour ) + 0.5f ) ) : 0;

		// Певцы: фразы с паузами, конец «bout» — улетает (допев фразу); слушатель ушёл далеко или запись убрали — тоже
		int32_t singing = 0;
		for( Singer& singer : layer.singers )
		{
			singer.boutLeft -= step;
			const float dx = singer.position.x - listener.position.x;
			const float dz = singer.position.z - listener.position.z;
			if( !audible || std::sqrt( dx * dx + dz * dz ) > settings.distance.to * leaveDistance ||
				std::find( sounds.begin(), sounds.end(), singer.sound ) == sounds.end() )
				singer.boutLeft = std::min( singer.boutLeft, 0.0f );
			if( singer.boutLeft > 0.0f && ++singing > wanted )
				singer.boutLeft = 0.0f;		// часы слоя кончаются — лишние улетают
			// Дозвучавшие фразы — голос прочь
			singer.phrases.erase( std::remove_if( singer.phrases.begin(), singer.phrases.end(), [&]( Singer::Phrase& phrase )
			{
				phrase.left -= step;
				if( phrase.left > 0.0f && audible )
					return false;
				m_mixer->removeVoice( phrase.voice );
				return true;
			} ), singer.phrases.end() );
			singer.nextPhrase -= step;
			if( singer.boutLeft <= 0.0f || singer.nextPhrase > 0.0f || singer.phrases.size() >= 2 )
				continue;
			// Фраза записи — случайная; пауза меньше нуля — следующая начнётся до конца этой, переход — на длину наложения
			const std::shared_ptr<const AudioClip>& clip = m_sounds[singer.sound]->clip;
			const auto& phrases = clip->phrases;
			const size_t index = std::uniform_int_distribution<size_t>( 0, phrases.size() - 1 )( m_random );
			const float pause = uniform( settings.pause.from, std::max( settings.pause.to, settings.pause.from ) );
			auto generator = std::make_unique<ClipGenerator>( clip, phrases[index], singer.pitch,
															  std::max( -pause, ClipGenerator::defaultFade ) );
			const float seconds = generator->seconds();
			singer.phrases.push_back( { m_mixer->addVoice( std::move( generator ) ), seconds + 0.05f } );
			singer.nextPhrase = std::max( seconds + pause, 0.25f * seconds );
		}
		layer.singers.erase( std::remove_if( layer.singers.begin(), layer.singers.end(),
											 []( const Singer& s ) { return s.boutLeft <= 0.0f && s.phrases.empty(); } ),
							 layer.singers.end() );

		// Новый певец — если их меньше, чем нужно в этот час, и прошлый прилетел не только что
		// Ожидание идёт, пока место пустует: «Arrival» — от того, как прошлый улетел
		const auto alive = std::count_if( layer.singers.begin(), layer.singers.end(), []( const Singer& s ) { return s.boutLeft > 0.0f; } );
		if( alive < wanted )
			layer.spawnDelay -= step;
		if( alive < wanted && layer.spawnDelay <= 0.0f )
		{
			Singer singer;
			if( placeSinger( settings, listener.position, singer ) )
			{
				singer.sound = sounds[std::uniform_int_distribution<size_t>( 0, sounds.size() - 1 )( m_random )];
				layer.singers.push_back( singer );
			}
			layer.spawnDelay = uniform( settings.arrival.from, std::max( settings.arrival.to, settings.arrival.from ) );
		}

		// Звучащие фразы: с места певца — громкость слоя и записи, затухание, воздух, гребень
		for( const Singer& singer : layer.singers )
		{
			if( singer.phrases.empty() )
				continue;
			const XMFLOAT3 direction( singer.position.x - listener.position.x, singer.position.y - listener.position.y,
									  singer.position.z - listener.position.z );
			const float distance = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
			const float occlusion = terrainOcclusion( m_terrain, listener.position, singer.position );
			const float record = std::max( m_sounds[singer.sound]->properties["Volume"].data<float>(), 0.0f );
			AudioVoiceTarget target;
			target.lowpass = std::min( airAbsorptionLowpass( distance ), occlusionLowpass( occlusion ) );
			spatialize( listener, direction, settings.volume * record * heard *
												 std::min( referenceDistance / std::max( distance, 1e-3f ), 1.0f ) * occlusionGain( occlusion ),
						target );
			for( const Singer::Phrase& phrase : singer.phrases )
				m_mixer->setTarget( phrase.voice, target );
		}
	}
}

}
