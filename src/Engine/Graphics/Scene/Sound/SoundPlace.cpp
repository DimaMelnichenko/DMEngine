#include "SoundPlace.h"
#include <algorithm>
#include <cmath>
#include "RenderView.h"
#include "Terrain\TerrainPipeline.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Поглощение воздухом: срез — 20 кГц / (1 + расстояние / это), м
constexpr float airAbsorptionDistance = 25.0f;
// Заглушение гребнем: рельеф выше прямой на столько, м, — ещё не гребень, на столько больше — заглушено полностью: тише на
// долю и глуше до частоты
constexpr float occlusionFrom = 0.5f;
constexpr float occlusionRange = 2.5f;
constexpr float occlusionAttenuation = 0.7f;
constexpr float occludedLowpass = 1000.0f;
constexpr int32_t occlusionSamples = 12;

}

AudioListener listenerFromView( const RenderView& view )
{
	AudioListener listener;
	listener.position = view.position;
	XMFLOAT3 right;
	XMStoreFloat3( &right, view.viewInverse.r[0] );
	const float length = std::max( std::sqrt( right.x * right.x + right.z * right.z ), 1e-4f );
	listener.right = XMFLOAT3( right.x / length, 0.0f, right.z / length );
	listener.forward = XMFLOAT3( -listener.right.z, 0.0f, listener.right.x );
	return listener;
}

std::vector<const std::vector<float>*> terrainMasks( const TerrainPipeline* terrain, const std::vector<std::string>& names )
{
	std::vector<const std::vector<float>*> result;
	if( !terrain )
		return result;
	const size_t size = terrain->mapSize();
	for( const std::string& name : names )
		if( const std::vector<float>* values = terrain->maskValues( name ); values && values->size() == size * size )
			result.push_back( values );
	return result;
}

float terrainMaskAt( const TerrainPipeline* terrain, const std::vector<const std::vector<float>*>& masks, float x, float z )
{
	if( !terrain || masks.empty() )
		return 0.0f;
	const uint32_t size = terrain->mapSize();
	const float texel = terrain->worldSize() / size;
	// Строка 0 — дальний край по z, как у карты высот (TerrainPipeline::paintProbe)
	const int32_t column = static_cast<int32_t>( std::floor( x / texel ) );
	const int32_t row = static_cast<int32_t>( std::floor( ( terrain->worldSize() - z ) / texel ) );
	if( column < 0 || row < 0 || column >= static_cast<int32_t>( size ) || row >= static_cast<int32_t>( size ) )
		return 0.0f;
	const size_t index = static_cast<size_t>( row ) * size + column;
	float sum = 0.0f;
	for( const std::vector<float>* values : masks )
		sum += ( *values )[index];
	return std::min( sum, 1.0f );
}

float airAbsorptionLowpass( float distance )
{
	return 20000.0f / ( 1.0f + std::max( distance, 0.0f ) / airAbsorptionDistance );
}

float terrainOcclusion( const TerrainPipeline* terrain, const XMFLOAT3& from, const XMFLOAT3& to )
{
	if( !terrain )
		return 0.0f;
	float excess = 0.0f;
	for( int32_t i = 1; i < occlusionSamples; ++i )
	{
		const float t = static_cast<float>( i ) / occlusionSamples;
		const float x = from.x + ( to.x - from.x ) * t;
		const float z = from.z + ( to.z - from.z ) * t;
		float ground = 0.0f;
		if( terrain->surfaceHeight( x, z, ground ) )
			excess = std::max( excess, ground - ( from.y + ( to.y - from.y ) * t ) );
	}
	return std::clamp( ( excess - occlusionFrom ) / occlusionRange, 0.0f, 1.0f );
}

float occlusionGain( float occlusion )
{
	return 1.0f - occlusionAttenuation * occlusion;
}

float occlusionLowpass( float occlusion )
{
	return 20000.0f + ( occludedLowpass - 20000.0f ) * occlusion;
}

void addAuditionProperties( PropertyContainer& part )
{
	part.insert( "Mute", false )->setTransient()->setTooltip( "Silence this part while editing (not saved)" );
	part.insert( "Solo", false )->setTransient()->setTooltip(
		"Hear only the parts with Solo while editing (not saved); Clear solo of the Audio window turns them all off" );
}

float auditionGain( AudioAudition& audition, const PropertyContainer& part, bool enabled )
{
	if( !enabled )
		return 0.0f;
	return audition.gain( part["Mute"].data<bool>(), part["Solo"].data<bool>() );
}

bool takeClearSolo( const AudioAudition& audition, uint32_t& seen )
{
	if( audition.clearRevision() == seen )
		return false;
	seen = audition.clearRevision();
	return true;
}

void clearSolo( PropertyContainer& part )
{
	if( part["Solo"].data<bool>() )
		part["Solo"].setData( false );
}

}
