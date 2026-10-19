#include "AmbientSound.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include "Engine\Audio\AudioSynth.h"
#include "Logger\Logger.h"
#include "Properties\SettingsProperties.h"
#include "SoundPlace.h"
#include "SoundRecordings.h"
#include "Terrain\TerrainPipeline.h"
#include "Wind\Wind.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;

// Маски, по которым слышны трава и лес (TerrainMasks)
const std::vector<std::string> grassMasks = { "mask_grass", "mask_grass_riparian", "mask_camomile" };
const std::vector<std::string> forestMasks = { "mask_forest_spruce", "mask_forest_pine", "mask_forest_birch" };

// Голоса слева и справа — в стороны на столько от камеры берётся ветер, м: порыв проходит от уха к уху
constexpr float earSpacing = 6.0f;
// Ветер у земли — у травы; на высоте камеры сильнее: до этого множителя к 40 м над землёй
constexpr float heightWindGain = 1.6f;
// Трава под ногами слышна в полную силу, пока камера не выше этого над землёй, м
constexpr float grassFullHeight = 2.5f;
// Под пологом ельника травы почти нет и ветра у земли тоже: шелест травы тише на эту долю леса в круге травы
constexpr float grassUnderForest = 0.8f;
// Камера выше крон — лес под ней дальше на эту разность высот
constexpr float crownHeight = 15.0f;
// Громкость слоя растёт со скоростью ветра до полуторной опорной, дальше — нет
constexpr float maxRelativeSpeed = 1.5f;
// Ручьи: громкость места — по опорному расстоянию, ближе не растёт, м; ровная вода тоже журчит — к перепаду уровня в месте
// прибавка, м; перекат — уклон воды от и до (доли); место, которое уже звучит, держится, пока другое не громче во столько раз;
// у края слышимости — затухание с этой доли расстояния
constexpr float streamReferenceDistance = 3.0f;
constexpr float streamRoughness = 0.15f;
// Громкость места — по мощности в этой степени: перепад «тихий ручей — водопад» в тысячи раз по мощности на слух — ~12 дБ;
// мощность места — не меньше этой доли шумного: у плёса течение почти стоит, а вода у камней всё равно журчит
constexpr float streamLoudness = 0.2f;
constexpr float streamQuietest = 0.05f;
constexpr float streamRapidsFrom = 0.02f;
constexpr float streamRapidsTo = 0.15f;
constexpr float streamKeep = 1.5f;
constexpr float streamFadeFrom = 0.7f;
// Запись места воды: высота — в этих пределах вокруг записи (у каждого места своя: соседние не в унисон)
constexpr float streamPitchSpread = 0.06f;

// Сектор стороны крон: лучи под этими углами к стороне, точки — на этих долях радиуса
const float crownAngles[] = { -30.0f, 0.0f, 30.0f };
const float crownDistances[] = { 0.06f, 0.125f, 0.25f, 0.5f, 1.0f };

float smoothstep( float from, float to, float x )
{
	const float t = std::clamp( ( x - from ) / std::max( to - from, 1e-4f ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

uint32_t voiceSeed( uint32_t index )
{
	return ( 0x9E3779B9u * ( index + 1 ) ) ^ 0x85EBCA6Bu;
}

// Зёрна шума голосов: свои у каждого голоса, одни и те же при каждом включении части; ручьи — дальше
constexpr uint32_t windSeed = 0;
constexpr uint32_t grassSeed = 2;
constexpr uint32_t crownSeed = 4;
constexpr uint32_t streamSeed = crownSeed + AmbientSound::crownSides;

FoliagePreset grassPreset()
{
	FoliagePreset grass;
	grass.flutterRate = 7.0f;
	grass.crackleRate = 60.0f;
	grass.crackleShare = 0.35f;
	return grass;
}

// Кроны ели — шум ниже и медленнее, щелчков меньше (иголки, а не листья)
FoliagePreset crownPreset()
{
	FoliagePreset crowns;
	crowns.hissQ = 0.6f;
	crowns.flutterRate = 1.5f;
	crowns.flutterDepth = 0.6f;
	crowns.crackleRatio = 2.5f;
	crowns.crackleRate = 40.0f;
	crowns.crackleShare = 0.2f;
	return crowns;
}

// Голоса части: включена — заведены (генератор make( номер голоса )), выключена — убраны (затихают)
template<size_t count, typename Make>
void syncPartVoices( AudioMixer& mixer, AudioMixer::VoiceId ( &voices )[count], bool enabled, Make make )
{
	for( size_t i = 0; i < count; ++i )
	{
		if( enabled && voices[i] == 0 )
		{
			voices[i] = mixer.addVoice( make( static_cast<uint32_t>( i ) ) );
		}
		else if( !enabled && voices[i] != 0 )
		{
			mixer.removeVoice( voices[i] );
			voices[i] = 0;
		}
	}
}

}

AmbientSound::AmbientSound() : SceneObject( "Ambient sound" )
{
	m_properties.setName( "Ambient sound" );
	m_windProperties.setName( "Wind" );
	m_grassProperties.setName( "Grass" );
	m_crownProperties.setName( "Crowns" );
	m_streamProperties.setName( "Streams" );
}

AmbientSound::~AmbientSound()
{
	if( !m_mixer )
		return;
	Settings off;
	off.windEnabled = off.grassEnabled = off.crownEnabled = false;
	syncVoices( off );
	for( const StreamVoice& voice : m_streamVoices )
		m_mixer->removeVoice( voice.voice );
}

void AmbientSound::syncVoices( const Settings& settings )
{
	syncPartVoices( *m_mixer, m_windVoices, settings.windEnabled,
					[]( uint32_t i ) { return std::make_unique<WindGenerator>( voiceSeed( windSeed + i ) ); } );
	syncPartVoices( *m_mixer, m_grassVoices, settings.grassEnabled,
					[]( uint32_t i ) { return std::make_unique<FoliageGenerator>( voiceSeed( grassSeed + i ), grassPreset() ); } );
	syncPartVoices( *m_mixer, m_crownVoices, settings.crownEnabled,
					[]( uint32_t i ) { return std::make_unique<FoliageGenerator>( voiceSeed( crownSeed + i ), crownPreset() ); } );
}

bool AmbientSound::initialize( const Settings& settings, AudioMixer& mixer, Wind& wind, const TerrainPipeline* terrain,
							   SoundRecordings* recordings )
{
	m_settings = settings;
	m_wind = &wind;
	m_terrain = terrain;
	m_recordings = recordings;
	addSettingProperties( m_properties, ambientSoundSchema(), settings, "" );
	addSettingProperties( m_windProperties, ambientSoundSchema(), settings, "Wind" );
	addSettingProperties( m_grassProperties, ambientSoundSchema(), settings, "Grass" );
	addSettingProperties( m_crownProperties, ambientSoundSchema(), settings, "Crowns" );
	addSettingProperties( m_streamProperties, ambientSoundSchema(), settings, "Streams" );
	for( PropertyContainer* part : { &m_windProperties, &m_grassProperties, &m_crownProperties, &m_streamProperties } )
	{
		addAuditionProperties( *part );
		// Ветер — вокруг слушателя: области нет
		if( part != &m_windProperties )
			addAreaProperty( *part );
		m_properties.addSubContainer( part );
	}

	// Голоса включённых частей — сразу все, молчат, пока не задана громкость; ручьёв — по слышным местам воды (updateStreams)
	m_mixer = &mixer;
	m_nextSeed = streamSeed;
	m_clearSolo = mixer.audition().clearRevision();
	syncVoices( settings );
	return true;
}

AmbientSound::Settings AmbientSound::settings() const
{
	Settings settings = m_settings;
	readSettingProperties( m_properties, ambientSoundSchema(), settings, "" );
	readSettingProperties( m_windProperties, ambientSoundSchema(), settings, "Wind" );
	readSettingProperties( m_grassProperties, ambientSoundSchema(), settings, "Grass" );
	readSettingProperties( m_crownProperties, ambientSoundSchema(), settings, "Crowns" );
	readSettingProperties( m_streamProperties, ambientSoundSchema(), settings, "Streams" );
	return settings;
}

AmbientSound::WindSample AmbientSound::windAt( const WindParameters& wind, float x, float z, float time ) const
{
	WindSample sample;
	if( wind.strength <= 0.0f )
		return sample;
	// Поле ветра по рельефу: доля скорости ветра уровня и порывистость (Shaders/wind_field.sh, windAt); вне карты — ровный ветер
	float field = 1.0f;
	if( m_terrain && m_terrain->hasWindField() && m_wind->fieldEnabled() )
	{
		const std::vector<TerrainWind::Cell>& cells = m_terrain->windField();
		const uint32_t size = static_cast<uint32_t>( std::lround( std::sqrt( static_cast<double>( cells.size() ) ) ) );
		const float texel = m_terrain->worldSize() / std::max( size, 1u );
		const int32_t column = static_cast<int32_t>( std::floor( x / texel ) );
		const int32_t row = static_cast<int32_t>( std::floor( ( m_terrain->worldSize() - z ) / texel ) );
		if( size > 0 && column >= 0 && row >= 0 && column < static_cast<int32_t>( size ) && row < static_cast<int32_t>( size ) )
		{
			const TerrainWind::Cell& cell = cells[static_cast<size_t>( row ) * size + column];
			field = std::sqrt( cell.x * cell.x + cell.z * cell.z );
			sample.turbulence = cell.w;
		}
	}
	const float gust = Wind::gust( wind, x, z, time );
	sample.speed = wind.speed * field * gust;
	sample.field = field;
	sample.gust = wind.gustMax > wind.gustMin ? std::clamp( ( gust - wind.gustMin ) / ( wind.gustMax - wind.gustMin ), 0.0f, 1.0f ) : 1.0f;
	return sample;
}

void AmbientSound::update( const FrameContext& frame )
{
	if( !m_mixer )
		return;
	AudioAudition& audition = m_mixer->audition();
	if( takeClearSolo( audition, m_clearSolo ) )
		for( PropertyContainer* part : { &m_windProperties, &m_grassProperties, &m_crownProperties, &m_streamProperties } )
			clearSolo( *part );
	const Settings settings = this->settings();
	syncVoices( settings );
	const WindParameters wind = m_wind->parameters();
	const float volume = visible() ? std::max( settings.volume, 0.0f ) : 0.0f;
	// Части: включение и прослушивание
	const float windPart = auditionGain( audition, m_windProperties, settings.windEnabled );
	const float grassPart = auditionGain( audition, m_grassProperties, settings.grassEnabled );
	const float crownPart = auditionGain( audition, m_crownProperties, settings.crownEnabled );
	const float streamPart = auditionGain( audition, m_streamProperties, settings.streamEnabled );
	const float time = frame.gameTime;

	const AudioListener listener = listenerFromView( frame.view );
	m_listener = listener.position;
	const float x = listener.position.x;
	const float z = listener.position.z;

	float ground = listener.position.y - 1.7f;
	if( m_terrain )
		m_terrain->surfaceHeight( x, z, ground );
	const float aboveGround = std::max( listener.position.y - ground, 0.0f );
	const float heightGain = 1.0f + ( heightWindGain - 1.0f ) * smoothstep( 2.0f, 40.0f, aboveGround );
	const float reference = std::max( settings.windReferenceSpeed, 0.1f );
	auto relative = [&]( float speed ) { return std::min( speed / reference, maxRelativeSpeed ); };
	// Шелест растений громче уже при слабом ветре (касания), гул ветра — только при сильном
	auto rustle = []( float relative ) { return std::pow( relative, 0.75f ); };

	// Ветер и трава: голос слева и справа, ветер — в точке у этого уха
	const auto grassMaskValues = terrainMasks( m_terrain, grassMasks );
	const auto forestMaskValues = terrainMasks( m_terrain, forestMasks );
	float grass = 0.0f;
	{
		// Трава в круге: центр и шесть точек на 0,6 радиуса, под лесом — реже
		const float radius = settings.grassRadius * 0.6f;
		for( int32_t i = 0; i < 7; ++i )
		{
			const float angle = i * pi / 3.0f;
			const float pointX = i == 6 ? x : x + radius * std::cos( angle );
			const float pointZ = i == 6 ? z : z + radius * std::sin( angle );
			grass += terrainMaskAt( m_terrain, grassMaskValues, pointX, pointZ ) * ( 1.0f - grassUnderForest * terrainMaskAt( m_terrain, forestMaskValues, pointX, pointZ ) );
		}
		grass /= 7.0f;
	}
	const float grassNear = 1.0f - smoothstep( grassFullHeight, std::max( settings.grassHeight, grassFullHeight + 0.5f ), aboveGround );
	for( int32_t side = 0; side < 2; ++side )
	{
		const float sign = side == 0 ? -1.0f : 1.0f;
		const WindSample sample = windAt( wind, x + sign * earSpacing * listener.right.x, z + sign * earSpacing * listener.right.z, time );
		const float windSpeed = sample.speed * heightGain;
		const float windRelative = relative( windSpeed );

		AudioVoiceTarget target;
		const float windGain = volume * windPart * settings.windVolume * windRelative * std::sqrt( windRelative );
		target.left = side == 0 ? windGain : 0.0f;
		target.right = side == 0 ? 0.0f : windGain;
		target.controls[0] = windSpeed;
		target.controls[1] = std::min( sample.turbulence + 0.2f, 1.0f );
		target.controls[2] = settings.windFrequency;
		if( m_windVoices[side] != 0 )
			m_mixer->setTarget( m_windVoices[side], target );

		// Трава — у земли: ветер без прибавки высоты
		const float grassRelative = relative( sample.speed );
		const float grassGain = volume * grassPart * settings.grassVolume * std::sqrt( grass ) * grassNear * rustle( grassRelative );
		target = AudioVoiceTarget();
		target.left = side == 0 ? grassGain : 0.0f;
		target.right = side == 0 ? 0.0f : grassGain;
		target.controls[0] = grassRelative * 1.5f;
		target.controls[1] = settings.grassFrequency;
		if( m_grassVoices[side] != 0 )
			m_mixer->setTarget( m_grassVoices[side], target );
	}

	// Кроны: сторона света — сектор лучами; лес на «Falloff» — вдвое тише, дальше — по квадрату расстояния: слышно в лесу и у
	// опушки, а не через луг. Доля леса — против весов у земли: камера выше крон — тот же лес дальше и тише. Громкость — по силе
	// ветра уровня (изгиб деревьев) × поле ветра, порыв — в середине сектора: в затишье тише на «Gust swing»
	const float crownRadius = std::max( settings.crownRadius, 1.0f );
	const float crownFalloff = std::max( settings.crownFalloff, 1.0f );
	const float aboveCrowns = std::max( aboveGround - crownHeight, 0.0f );
	auto nearWeight = [crownFalloff]( float distance ) { return 1.0f / ( 1.0f + ( distance / crownFalloff ) * ( distance / crownFalloff ) ); };
	for( uint32_t side = 0; side < crownSides && settings.crownEnabled; ++side )
	{
		const float azimuth = side * 0.5f * pi;
		float forest = 0.0f;
		float weights = 0.0f;
		for( float angleDegrees : crownAngles )
		{
			const float angle = azimuth + angleDegrees * pi / 180.0f;
			for( float share : crownDistances )
			{
				const float distance = share * crownRadius;
				const float slant = std::sqrt( distance * distance + aboveCrowns * aboveCrowns );
				forest += terrainMaskAt( m_terrain, forestMaskValues, x + distance * std::sin( angle ), z + distance * std::cos( angle ) ) *
						  nearWeight( slant );
				weights += nearWeight( distance );
			}
		}
		forest /= std::max( weights, 1e-4f );
		const float middle = std::min( crownFalloff, crownRadius );
		const XMFLOAT3 direction( std::sin( azimuth ), 0.0f, std::cos( azimuth ) );
		const WindSample sample = windAt( wind, x + middle * direction.x, z + middle * direction.z, time );
		const float bending = std::min( wind.strength * sample.field / std::max( settings.crownReferenceStrength, 0.01f ), maxRelativeSpeed );
		const float gustGain = std::pow( 10.0f, -std::max( settings.crownGustSwing, 0.0f ) * ( 1.0f - sample.gust ) / 20.0f );
		const float gusting = wind.gustMax > 0.0f ? ( wind.gustMin + ( wind.gustMax - wind.gustMin ) * sample.gust ) / wind.gustMax : 1.0f;

		AudioVoiceTarget target;
		spatialize( listener, direction, volume * crownPart * settings.crownVolume * forest * rustle( bending ) * gustGain, target );
		// Щелчки и трепет — чаще в порыв
		target.controls[0] = bending * gusting * 1.5f;
		target.controls[1] = settings.crownFrequency;
		m_mixer->setTarget( m_crownVoices[side], target );
	}

	updateStreams( settings, listener, volume * streamPart );
}

void AmbientSound::soundDebug( SoundDebug& out )
{
	if( !m_mixer )
		return;
	const Settings settings = this->settings();
	// Места воды ближе «Distance»: звучащие — крупнее и ярче, размер — громкость места
	const XMFLOAT3 water( 0.35f, 0.65f, 1.0f );
	const float distance = std::max( settings.streamDistance, 1.0f );
	for( size_t i = 0; i < m_streamBlocks.size(); ++i )
	{
		const StreamBlock& block = m_streamBlocks[i];
		if( std::hypot( block.position.x - m_listener.x, block.position.z - m_listener.z ) > distance )
			continue;
		SoundMarker marker;
		marker.position = block.position;
		marker.color = water;
		marker.sounding = std::any_of( m_streamVoices.begin(), m_streamVoices.end(), [i]( const StreamVoice& v ) { return v.block == i; } );
		marker.size = std::clamp( std::pow( block.power, streamLoudness ), 0.4f, 1.3f );
		marker.properties = &m_streamProperties;
		out.markers.push_back( marker );
	}
	// Области частей: кольцо дальности и маски, по которым часть слышна
	auto area = [&out]( PropertyContainer& part, const XMFLOAT3& color, float radius, const std::vector<std::string>& masks )
	{
		if( !showArea( part ) )
			return;
		SoundArea result;
		result.color = color;
		result.ring = XMFLOAT2( 0.0f, radius );
		result.masks = masks;
		result.label = part.name();
		result.properties = &part;
		out.areas.push_back( std::move( result ) );
	};
	area( m_grassProperties, XMFLOAT3( 0.55f, 1.0f, 0.35f ), settings.grassRadius, grassMasks );
	// Кроны: кольцо — где лес ещё слышен (на двух «Falloff» — вчетверо тише, чем рядом)
	area( m_crownProperties, XMFLOAT3( 0.25f, 0.8f, 0.45f ), std::min( 2.0f * settings.crownFalloff, settings.crownRadius ), forestMasks );
	area( m_streamProperties, water, distance, {} );
}

void AmbientSound::buildStreamBlocks()
{
	m_streamBlocks.clear();
	const TerrainHydrology::Result* hydrology = m_terrain ? m_terrain->hydrology() : nullptr;
	const uint32_t size = hydrology ? hydrology->size : 0;
	if( size == 0 || hydrology->staticWater.size() != static_cast<size_t>( size ) * size )
		return;
	const float world = m_terrain->worldSize();
	const float texel = world / size;
	const uint32_t cells = std::max( 1u, static_cast<uint32_t>( std::lround( streamBlock / texel ) ) );
	const uint32_t blocks = ( size + cells - 1 ) / cells;
	// Клетки ручьёв (не озёр) с водой глубже видимой — в свой блок: середина, уровень, перепад уровня и расход
	struct Sum
	{
		double x = 0.0, z = 0.0, level = 0.0, flow = 0.0;
		float low = 1e9f, high = -1e9f;
		uint32_t count = 0;
	};
	std::vector<Sum> sums( static_cast<size_t>( blocks ) * blocks );
	for( uint32_t row = 0; row < size; ++row )
	{
		for( uint32_t column = 0; column < size; ++column )
		{
			const XMFLOAT4& cell = hydrology->staticWater[static_cast<size_t>( row ) * size + column];
			if( cell.x < -1e8f || cell.w > -1e8f )
				continue;
			// Строка 0 — дальний край по z
			const float x = ( column + 0.5f ) * texel;
			const float z = world - ( row + 0.5f ) * texel;
			float ground = 0.0f;
			if( !m_terrain->surfaceHeight( x, z, ground ) || cell.x - ground < TerrainHydrology::visibleWaterDepth )
				continue;
			Sum& sum = sums[static_cast<size_t>( row / cells ) * blocks + column / cells];
			sum.x += x;
			sum.z += z;
			sum.level += cell.x;
			sum.low = std::min( sum.low, cell.x );
			sum.high = std::max( sum.high, cell.x );
			// Расход через клетку поперёк течения: скорость × глубина × ширина клетки
			sum.flow += std::sqrt( cell.y * cell.y + cell.z * cell.z ) * std::min( cell.x - ground, 2.0f ) * texel;
			++sum.count;
		}
	}
	// Мощность, которую теряет течение в блоке, — расход × перепад уровня (ρ g Q ΔH): её малая доля — звук. Расход — сумма
	// по площади на длину блока
	const float length = cells * texel;
	for( const Sum& sum : sums )
	{
		if( sum.count == 0 )
			continue;
		StreamBlock block;
		block.position = XMFLOAT3( static_cast<float>( sum.x / sum.count ), static_cast<float>( sum.level / sum.count ),
								   static_cast<float>( sum.z / sum.count ) );
		const float drop = sum.high - sum.low;
		block.power = static_cast<float>( sum.flow / length ) * ( drop + streamRoughness );
		block.rapids = smoothstep( streamRapidsFrom, streamRapidsTo, drop / length );
		m_streamBlocks.push_back( block );
	}
	if( m_streamBlocks.empty() )
		return;
	// Мощность — доля шумного места уровня (громче 95 % мест): один водопад не делает тихими все ручьи
	std::vector<float> powers;
	for( const StreamBlock& block : m_streamBlocks )
		powers.push_back( block.power );
	const size_t loud = powers.size() * 95 / 100;
	std::nth_element( powers.begin(), powers.begin() + loud, powers.end() );
	const float reference = std::max( powers[loud], 1e-6f );
	size_t rapids = 0;
	for( StreamBlock& block : m_streamBlocks )
	{
		block.power = std::clamp( block.power / reference, streamQuietest, 4.0f );
		rapids += block.rapids > 0.5f ? 1 : 0;
	}
	LOG( "Ambient sound: " + std::to_string( m_streamBlocks.size() ) + " places of stream water, " + std::to_string( rapids ) +
		 " of them over steps" );
}

void AmbientSound::updateStreams( const Settings& settings, const AudioListener& listener, float volume )
{
	if( m_terrain && m_terrain->heightRevision() != m_streamRevision )
	{
		m_streamRevision = m_terrain->heightRevision();
		buildStreamBlocks();
		for( const StreamVoice& voice : m_streamVoices )
			m_mixer->removeVoice( voice.voice );
		m_streamVoices.clear();
	}
	refreshStreamRecordings( settings );
	const float streamVolume = volume * std::max( settings.streamVolume, 0.0f );
	const size_t voiceCount = streamVolume > 0.0f ? static_cast<size_t>( std::clamp( settings.streamVoices, 0, 16 ) ) : 0;
	const float maxDistance = std::max( settings.streamDistance, 1.0f );

	// Слышимость места: громкость × опорное расстояние / расстояние; ключ выбора — с запасом у звучащих (голоса не скачут)
	struct Candidate
	{
		size_t block;
		float distance;
		float gain;
		float key;
		float occlusion;
	};
	std::vector<Candidate> candidates;
	if( voiceCount > 0 )
	{
		for( size_t i = 0; i < m_streamBlocks.size(); ++i )
		{
			const XMFLOAT3& position = m_streamBlocks[i].position;
			const float dx = position.x - listener.position.x;
			const float dy = position.y - listener.position.y;
			const float dz = position.z - listener.position.z;
			const float distance = std::sqrt( dx * dx + dy * dy + dz * dz );
			if( distance >= maxDistance )
				continue;
			const float gain = streamVolume * std::pow( m_streamBlocks[i].power, streamLoudness ) * std::min( streamReferenceDistance / distance, 1.0f ) *
							   ( 1.0f - smoothstep( streamFadeFrom * maxDistance, maxDistance, distance ) );
			const bool playing = std::any_of( m_streamVoices.begin(), m_streamVoices.end(), [i]( const StreamVoice& v ) { return v.block == i; } );
			candidates.push_back( { i, distance, gain, playing ? gain * streamKeep : gain, 0.0f } );
		}
	}
	// Вдвое больше слышных — с заглушением гребнем (луч по карте высот), из них — самые громкие
	auto byKey = []( const Candidate& a, const Candidate& b ) { return a.key > b.key; };
	const size_t checked = std::min( candidates.size(), voiceCount * 2 );
	std::partial_sort( candidates.begin(), candidates.begin() + checked, candidates.end(), byKey );
	candidates.resize( checked );
	for( Candidate& candidate : candidates )
	{
		XMFLOAT3 water = m_streamBlocks[candidate.block].position;
		water.y += 0.3f;
		candidate.occlusion = terrainOcclusion( m_terrain, listener.position, water );
		const float muffled = occlusionGain( candidate.occlusion );
		candidate.gain *= muffled;
		candidate.key *= muffled;
	}
	std::sort( candidates.begin(), candidates.end(), byKey );
	candidates.resize( std::min( candidates.size(), voiceCount ) );
	candidates.erase( std::remove_if( candidates.begin(), candidates.end(), []( const Candidate& c ) { return c.gain < 1e-4f; } ),
					  candidates.end() );

	// Ушедшие места — их голоса затихают; новые — свой голос (генератор со своим зерном)
	m_streamVoices.erase( std::remove_if( m_streamVoices.begin(), m_streamVoices.end(), [&]( const StreamVoice& voice )
	{
		const bool kept = std::any_of( candidates.begin(), candidates.end(), [&]( const Candidate& c ) { return c.block == voice.block; } );
		if( !kept )
			m_mixer->removeVoice( voice.voice );
		return !kept;
	} ), m_streamVoices.end() );
	for( const Candidate& candidate : candidates )
	{
		auto found = std::find_if( m_streamVoices.begin(), m_streamVoices.end(), [&]( const StreamVoice& v ) { return v.block == candidate.block; } );
		if( found == m_streamVoices.end() )
		{
			StreamVoice voice;
			voice.block = candidate.block;
			voice.voice = addStreamVoice( settings, candidate.block, voice.recording );
			m_streamVoices.push_back( std::move( voice ) );
			found = m_streamVoices.end() - 1;
		}
		const StreamBlock& block = m_streamBlocks[candidate.block];
		// Запись — с громкостью окна «Sounds» (правка слышна сразу)
		const float recordingVolume = !found->recording.empty() && m_recordings ? m_recordings->recordingVolume( found->recording ) : 1.0f;
		AudioVoiceTarget target;
		// Вдали — глуше (воздух), за гребнем — ещё глуше; у самой воды — вокруг слушателя
		target.lowpass = std::min( airAbsorptionLowpass( candidate.distance ), occlusionLowpass( candidate.occlusion ) );
		const XMFLOAT3 direction( block.position.x - listener.position.x, 0.0f, block.position.z - listener.position.z );
		spatialize( listener, direction, candidate.gain * recordingVolume, target,
					1.0f - std::clamp( candidate.distance / streamBlock, 0.0f, 1.0f ) );
		target.controls[0] = std::min( 0.5f + std::sqrt( block.power ), 1.5f );
		target.controls[1] = block.rapids;
		m_mixer->setTarget( found->voice, target );
	}
}

void AmbientSound::refreshStreamRecordings( const Settings& settings )
{
	const std::string lists = settings.streamSound + "|" + settings.streamRapidsSound;
	if( lists == m_recordingLists )
		return;
	m_recordingLists = lists;
	auto resolve = [this]( const std::string& list, std::vector<StreamRecording>& recordings )
	{
		recordings.clear();
		size_t start = 0;
		while( start <= list.size() )
		{
			const size_t end = std::min( list.find( ',', start ), list.size() );
			std::string name = list.substr( start, end - start );
			name.erase( 0, name.find_first_not_of( " \t" ) );
			name.erase( name.find_last_not_of( " \t" ) + 1 );
			start = end + 1;
			if( name.empty() )
				continue;
			std::shared_ptr<const AudioClip> clip = m_recordings ? m_recordings->recording( name ) : nullptr;
			if( clip )
				recordings.push_back( { name, std::move( clip ) } );
			else
				LOG( "Ambient sound: no recording " + name + " for the streams (table Sounds, Tools/pack_sound.py)" );
		}
	};
	resolve( settings.streamSound, m_calmRecordings );
	resolve( settings.streamRapidsSound, m_rapidsRecordings );
	LOG( "Ambient sound: streams - " + std::to_string( m_calmRecordings.size() ) + " calm and " +
		 std::to_string( m_rapidsRecordings.size() ) + " rapids recordings" + ( m_calmRecordings.empty() ? ", calm water - synthesis" : "" ) );
	// Звучащие места — заново со своими записями
	for( const StreamVoice& voice : m_streamVoices )
		m_mixer->removeVoice( voice.voice );
	m_streamVoices.clear();
}

AudioMixer::VoiceId AmbientSound::addStreamVoice( const Settings& settings, size_t block, std::string& recording )
{
	const uint32_t seed = voiceSeed( m_nextSeed++ );
	// Порог — запись порогов (нет её — спокойная вода), спокойное место — спокойная вода; списка нет — синтез
	const bool rapids = m_streamBlocks[block].rapids >= settings.streamRapidsFrom && !m_rapidsRecordings.empty();
	const std::vector<StreamRecording>& list = rapids ? m_rapidsRecordings : m_calmRecordings;
	if( list.empty() )
	{
		recording.clear();
		return m_mixer->addVoice( std::make_unique<BrookGenerator>( seed ) );
	}
	// Запись — по номеру места (то же место — та же запись), начало и высота — свои у голоса
	const StreamRecording& chosen = list[voiceSeed( static_cast<uint32_t>( block ) ) % list.size()];
	recording = chosen.name;
	const float start = static_cast<float>( seed & 0xFFFFu ) / 65535.0f;
	const float pitch = 1.0f + streamPitchSpread * ( static_cast<float>( ( seed >> 16 ) & 0xFFFFu ) / 32767.5f - 1.0f );
	return m_mixer->addVoice( std::make_unique<LoopGenerator>( chosen.clip, start, pitch ) );
}

}
