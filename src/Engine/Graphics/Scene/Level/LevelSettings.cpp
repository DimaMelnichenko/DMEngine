#include "LevelSettings.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

namespace GS
{

Tonemapper tonemapperFromName( const std::string& name )
{
	return name == "None" ? Tonemapper::none : name == "ACES" ? Tonemapper::aces : Tonemapper::agx;
}

const char* tonemapperName( Tonemapper tonemapper )
{
	switch( tonemapper )
	{
		case Tonemapper::none: return "None";
		case Tonemapper::aces: return "ACES";
		default: return "AgX";
	}
}

MeteringMode meteringModeFromName( const std::string& name )
{
	return name == "Manual" ? MeteringMode::manual : MeteringMode::autoHistogram;
}

const char* meteringModeName( MeteringMode mode )
{
	return mode == MeteringMode::manual ? "Manual" : "AutoHistogram";
}

std::vector<XMFLOAT2> curveFromText( const std::string& text )
{
	std::vector<XMFLOAT2> curve;
	std::istringstream keys( text );
	std::string key;
	while( std::getline( keys, key, ';' ) )
	{
		XMFLOAT2 value;
		if( std::sscanf( key.c_str(), " %f , %f", &value.x, &value.y ) == 2 )
			curve.push_back( value );
	}
	std::sort( curve.begin(), curve.end(), []( const XMFLOAT2& a, const XMFLOAT2& b ) { return a.x < b.x; } );
	if( curve.size() > maxExposureCurveKeys )
		curve.resize( maxExposureCurveKeys );
	return curve;
}

std::string curveText( const std::vector<XMFLOAT2>& curve )
{
	std::string text;
	for( const XMFLOAT2& key : curve )
	{
		char value[64];
		std::snprintf( value, sizeof( value ), "%s%g,%g", text.empty() ? "" : "; ", key.x, key.y );
		text += value;
	}
	return text;
}

}
