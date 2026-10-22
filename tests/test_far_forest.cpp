// Рельеф для леса дальних гор (FarForestRelief): водосбор по залитой карте и лавинные лотки на маленьких картах
#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "Terrain\FarForestRelief.h"

using namespace GS;

namespace
{

constexpr uint32_t size = 64;
constexpr float cell = 64.0f;

float& at( std::vector<float>& heights, uint32_t row, uint32_t col ) { return heights[static_cast<size_t>( row ) * size + col]; }

// Склон вниз к югу (к строке size − 1): до rows[0] — уклон slopes[0], до rows[1] — slopes[1], дальше — slopes[2], градусы;
// верх — top, м
std::vector<float> profile( const float slopes[3], const uint32_t rows[2], float top )
{
	std::vector<float> heights( static_cast<size_t>( size ) * size, 0.0f );
	float height = top;
	for( uint32_t row = 0; row < size; ++row )
	{
		for( uint32_t col = 0; col < size; ++col )
			at( heights, row, col ) = height;
		const float slope = row < rows[0] ? slopes[0] : row < rows[1] ? slopes[1] : slopes[2];
		height -= std::tan( slope / 57.29578f ) * cell;
	}
	return heights;
}

}

TEST_CASE( "FarForestRelief: the valley floor drains far more than the ridges" )
{
	// V-долина: дно по середине, вниз к югу
	std::vector<float> heights( static_cast<size_t>( size ) * size, 0.0f );
	for( uint32_t row = 0; row < size; ++row )
		for( uint32_t col = 0; col < size; ++col )
			at( heights, row, col ) = 2000.0f + 0.3f * std::abs( ( col + 0.5f - 0.5f * size ) * cell ) - 0.05f * row * cell;
	FarForestRelief relief;
	relief.build( heights, size, cell );
	const float floor = relief.drainage()[static_cast<size_t>( size - 4 ) * size + size / 2];
	const float ridge = relief.drainage()[static_cast<size_t>( size / 2 ) * size + 1];
	CHECK( ridge < 2.0f * cell * cell );
	CHECK( floor > 100.0f * ridge );
}

TEST_CASE( "FarForestRelief: an avalanche track runs below a steep slope down to the runout" )
{
	const float slopes[3] = { 38.0f, 20.0f, 3.0f };
	const uint32_t rows[2] = { 20, 40 };
	FarForestRelief relief;
	relief.build( profile( slopes, rows, 2500.0f ), size, cell );
	const std::vector<float> share = relief.avalanche( AvalancheRules{} );
	// Ниже крутого — по склону 20° (там мог бы расти лес) — лоток; за зоной выноса — нет
	CHECK( share[static_cast<size_t>( 30 ) * size + size / 2] > 0.3f );
	CHECK( share[static_cast<size_t>( 50 ) * size + size / 2] < 0.05f );
}

TEST_CASE( "FarForestRelief: no avalanches on gentle slopes or below the release height" )
{
	const float gentle[3] = { 12.0f, 12.0f, 12.0f };
	const uint32_t rows[2] = { 20, 40 };
	FarForestRelief relief;
	relief.build( profile( gentle, rows, 2500.0f ), size, cell );
	float most = 0.0f;
	for( float value : relief.avalanche( AvalancheRules{} ) )
		most = std::max( most, value );
	CHECK( most < 0.01f );

	// Тот же крутой склон, но весь ниже высоты отрыва
	const float steep[3] = { 35.0f, 20.0f, 3.0f };
	relief.build( profile( steep, rows, 600.0f ), size, cell );
	AvalancheRules rules;
	rules.height = 900.0f;
	most = 0.0f;
	for( float value : relief.avalanche( rules ) )
		most = std::max( most, value );
	CHECK( most < 0.01f );
}
