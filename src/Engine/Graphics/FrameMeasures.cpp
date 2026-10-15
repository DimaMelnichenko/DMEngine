#include "FrameMeasures.h"
#include <algorithm>
#include <cstdio>

namespace GS
{

void FrameMeasures::start( float seconds, std::function<void( const std::string& )> done )
{
	Measure measure;
	measure.start = std::chrono::steady_clock::now();
	measure.duration = std::chrono::duration<float>( seconds );
	measure.done = std::move( done );
	m_measures.push_back( std::move( measure ) );
}

void FrameMeasures::accumulate( const Values& values, const std::function<std::string()>& tail )
{
	if( m_measures.empty() || values.empty() )
		return;

	// Одноимённые значения кадра сначала складываются, потом усредняются по кадрам
	Values frame;
	for( const auto& [name, value] : values )
	{
		auto it = std::find_if( frame.begin(), frame.end(), [&]( const auto& entry ) { return entry.first == name; } );
		if( it == frame.end() )
			frame.push_back( { name, value } );
		else
			it->second += value;
	}

	const auto now = std::chrono::steady_clock::now();
	for( auto measure = m_measures.begin(); measure != m_measures.end(); )
	{
		std::vector<Average>& averages = measure->averages;
		for( const auto& [name, value] : frame )
		{
			auto it = std::find_if( averages.begin(), averages.end(), [&]( const Average& a ) { return a.name == name; } );
			if( it == averages.end() )
			{
				averages.push_back( { name } );
				it = averages.end() - 1;
			}
			it->sum += value;
			it->count++;
		}

		if( now - measure->start < measure->duration )
		{
			++measure;
			continue;
		}
		char text[32];
		std::string line = m_title + " average over " + std::to_string( averages[0].count ) + " frames, ms:";
		for( const Average& average : averages )
		{
			std::snprintf( text, sizeof( text ), "%.3f", average.sum / std::max( average.count, 1u ) );
			line += " " + average.name + " " + text + ";";
		}
		if( tail )
			line += tail();
		measure->done( line );
		measure = m_measures.erase( measure );
	}
}

}
