#include "AmbientSound.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include "Engine\Audio\AudioSynth.h"
#include "Properties\SettingsProperties.h"
#include "Terrain\TerrainPipeline.h"
#include "Wind\Wind.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;

// Маски, по которым слышны трава и лес (TerrainMasks)
const char* const grassMasks[] = { "mask_grass", "mask_grass_riparian", "mask_camomile" };
const char* const forestMasks[] = { "mask_forest_spruce", "mask_forest_pine", "mask_forest_birch" };

// Голоса слева и справа — в стороны на столько от камеры берётся ветер, м: порыв проходит от уха к уху
constexpr float earSpacing = 6.0f;
// Ветер у земли — у травы; на высоте камеры сильнее: до этого множителя к 40 м над землёй
constexpr float heightWindGain = 1.6f;
// Трава под ногами слышна в полную силу, пока камера не выше этого над землёй, м
constexpr float grassFullHeight = 2.5f;
// Под пологом ельника травы почти нет и ветра у земли тоже: шелест травы тише на эту долю леса в круге травы
constexpr float grassUnderForest = 0.8f;
// Кроны — выше травы: ветер в них сильнее; камера выше крон — лес под ней дальше на эту разность высот
constexpr float crownWindGain = 1.2f;
constexpr float crownHeight = 15.0f;
// Громкость слоя растёт со скоростью ветра до полуторной опорной, дальше — нет
constexpr float maxRelativeSpeed = 1.5f;
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

}

AmbientSound::AmbientSound() : SceneObject( "Ambient sound" )
{
	m_properties.setName( "Ambient sound" );
	m_windProperties.setName( "Wind" );
	m_grassProperties.setName( "Grass" );
	m_crownProperties.setName( "Crowns" );
}

AmbientSound::~AmbientSound()
{
	if( !m_mixer )
		return;
	for( AudioMixer::VoiceId voice : m_windVoices )
		m_mixer->removeVoice( voice );
	for( AudioMixer::VoiceId voice : m_grassVoices )
		m_mixer->removeVoice( voice );
	for( AudioMixer::VoiceId voice : m_crownVoices )
		m_mixer->removeVoice( voice );
}

bool AmbientSound::initialize( const Settings& settings, AudioMixer& mixer, Wind& wind, const TerrainPipeline* terrain )
{
	m_settings = settings;
	m_wind = &wind;
	m_terrain = terrain;
	addSettingProperties( m_properties, ambientSoundSchema(), settings, "" );
	addSettingProperties( m_windProperties, ambientSoundSchema(), settings, "Wind" );
	addSettingProperties( m_grassProperties, ambientSoundSchema(), settings, "Grass" );
	addSettingProperties( m_crownProperties, ambientSoundSchema(), settings, "Crowns" );
	m_properties.addSubContainer( &m_windProperties );
	m_properties.addSubContainer( &m_grassProperties );
	m_properties.addSubContainer( &m_crownProperties );

	// Голоса — сразу все, молчат, пока не задана громкость
	uint32_t seed = 0;
	for( AudioMixer::VoiceId& voice : m_windVoices )
		voice = mixer.addVoice( std::make_unique<WindGenerator>( voiceSeed( seed++ ) ) );
	FoliagePreset grass;
	grass.flutterRate = 7.0f;
	grass.crackleRate = 60.0f;
	grass.crackleShare = 0.35f;
	for( AudioMixer::VoiceId& voice : m_grassVoices )
		voice = mixer.addVoice( std::make_unique<FoliageGenerator>( voiceSeed( seed++ ), grass ) );
	// Кроны ели — шум ниже и медленнее, щелчков меньше (иголки, а не листья)
	FoliagePreset crowns;
	crowns.hissQ = 0.6f;
	crowns.flutterRate = 1.5f;
	crowns.flutterDepth = 0.6f;
	crowns.crackleRatio = 2.5f;
	crowns.crackleRate = 40.0f;
	crowns.crackleShare = 0.2f;
	for( AudioMixer::VoiceId& voice : m_crownVoices )
		voice = mixer.addVoice( std::make_unique<FoliageGenerator>( voiceSeed( seed++ ), crowns ) );
	m_mixer = &mixer;
	return true;
}

AmbientSound::Settings AmbientSound::settings() const
{
	Settings settings = m_settings;
	readSettingProperties( m_properties, ambientSoundSchema(), settings, "" );
	readSettingProperties( m_windProperties, ambientSoundSchema(), settings, "Wind" );
	readSettingProperties( m_grassProperties, ambientSoundSchema(), settings, "Grass" );
	readSettingProperties( m_crownProperties, ambientSoundSchema(), settings, "Crowns" );
	return settings;
}

std::vector<const std::vector<float>*> AmbientSound::masks( const char* const* names, size_t count ) const
{
	std::vector<const std::vector<float>*> result;
	if( !m_terrain )
		return result;
	const size_t size = m_terrain->mapSize();
	for( size_t i = 0; i < count; ++i )
		if( const std::vector<float>* values = m_terrain->maskValues( names[i] ); values && values->size() == size * size )
			result.push_back( values );
	return result;
}

float AmbientSound::maskAt( const std::vector<const std::vector<float>*>& masks, float x, float z ) const
{
	if( masks.empty() )
		return 0.0f;
	const uint32_t size = m_terrain->mapSize();
	const float texel = m_terrain->worldSize() / size;
	// Строка 0 — дальний край по z, как у карты высот (TerrainPipeline::paintProbe)
	const int32_t column = static_cast<int32_t>( std::floor( x / texel ) );
	const int32_t row = static_cast<int32_t>( std::floor( ( m_terrain->worldSize() - z ) / texel ) );
	if( column < 0 || row < 0 || column >= static_cast<int32_t>( size ) || row >= static_cast<int32_t>( size ) )
		return 0.0f;
	const size_t index = static_cast<size_t>( row ) * size + column;
	float sum = 0.0f;
	for( const std::vector<float>* values : masks )
		sum += ( *values )[index];
	return std::min( sum, 1.0f );
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
	sample.speed = wind.speed * field * Wind::gust( wind, x, z, time );
	return sample;
}

void AmbientSound::update( const FrameContext& frame )
{
	if( !m_mixer )
		return;
	const Settings settings = this->settings();
	const WindParameters wind = m_wind->parameters();
	const float volume = visible() ? std::max( settings.volume, 0.0f ) : 0.0f;
	const float time = frame.gameTime;

	// Слушатель — камера: вправо — первая строка обратной матрицы вида (крена нет — горизонтально), вперёд по горизонту
	AudioListener listener;
	listener.position = frame.view.position;
	XMFLOAT3 right;
	XMStoreFloat3( &right, frame.view.viewInverse.r[0] );
	const float rightLength = std::max( std::sqrt( right.x * right.x + right.z * right.z ), 1e-4f );
	listener.right = XMFLOAT3( right.x / rightLength, 0.0f, right.z / rightLength );
	listener.forward = XMFLOAT3( -listener.right.z, 0.0f, listener.right.x );
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
	const auto grassMaskValues = masks( grassMasks, std::size( grassMasks ) );
	const auto forestMaskValues = masks( forestMasks, std::size( forestMasks ) );
	float grass = 0.0f;
	{
		// Трава в круге: центр и шесть точек на 0,6 радиуса, под лесом — реже
		const float radius = settings.grassRadius * 0.6f;
		for( int32_t i = 0; i < 7; ++i )
		{
			const float angle = i * pi / 3.0f;
			const float pointX = i == 6 ? x : x + radius * std::cos( angle );
			const float pointZ = i == 6 ? z : z + radius * std::sin( angle );
			grass += maskAt( grassMaskValues, pointX, pointZ ) * ( 1.0f - grassUnderForest * maskAt( forestMaskValues, pointX, pointZ ) );
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
		const float windGain = volume * settings.windVolume * windRelative * std::sqrt( windRelative );
		target.left = side == 0 ? windGain : 0.0f;
		target.right = side == 0 ? 0.0f : windGain;
		target.controls[0] = windSpeed;
		target.controls[1] = std::min( sample.turbulence + 0.2f, 1.0f );
		target.controls[2] = settings.windFrequency;
		m_mixer->setTarget( m_windVoices[side], target );

		// Трава — у земли: ветер без прибавки высоты
		const float grassRelative = relative( sample.speed );
		const float grassGain = volume * settings.grassVolume * std::sqrt( grass ) * grassNear * rustle( grassRelative );
		target = AudioVoiceTarget();
		target.left = side == 0 ? grassGain : 0.0f;
		target.right = side == 0 ? 0.0f : grassGain;
		target.controls[0] = grassRelative * 1.5f;
		target.controls[1] = settings.grassFrequency;
		m_mixer->setTarget( m_grassVoices[side], target );
	}

	// Кроны: сторона света — сектор лучами, лес ближе — весомее; ветер — в середине сектора. Доля леса — против весов у
	// земли: камера выше крон — тот же лес дальше и тише
	const float crownRadius = std::max( settings.crownRadius, 1.0f );
	const float aboveCrowns = std::max( aboveGround - crownHeight, 0.0f );
	for( uint32_t side = 0; side < crownSides; ++side )
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
				forest += maskAt( forestMaskValues, x + distance * std::sin( angle ), z + distance * std::cos( angle ) ) /
						  ( 1.0f + slant / ( 0.25f * crownRadius ) );
				weights += 1.0f / ( 1.0f + distance / ( 0.25f * crownRadius ) );
			}
		}
		forest /= std::max( weights, 1e-4f );
		const float middle = 0.25f * crownRadius;
		const XMFLOAT3 direction( std::sin( azimuth ), 0.0f, std::cos( azimuth ) );
		const WindSample sample = windAt( wind, x + middle * direction.x, z + middle * direction.z, time );
		const float crownRelative = relative( sample.speed * crownWindGain );

		AudioVoiceTarget target;
		spatialize( listener, direction, volume * settings.crownVolume * std::sqrt( forest ) * rustle( crownRelative ), target );
		target.controls[0] = crownRelative * 1.5f;
		target.controls[1] = settings.crownFrequency;
		m_mixer->setTarget( m_crownVoices[side], target );
	}
}

}
