#include "Material.h"
#include <algorithm>

namespace GS
{

Material::Material( uint32_t id, const std::string& name ) : DMResource( id, name )
{
	m_parameters.setName( name );
}

Material::~Material()
{
}

std::vector<int> Material::depthPhases() const
{
	std::vector<int> phases;
	for( int phase = 0; phase < phaseCount(); ++phase )
		if( !hasPixelShader( phase ) )
			phases.push_back( phase );
	return phases;
}

std::vector<int> Material::colorPhases() const
{
	const std::vector<int> depth = depthPhases();
	std::vector<int> phases;
	for( int phase = 0; phase < phaseCount(); ++phase )
		if( std::find( depth.begin(), depth.end(), phase ) == depth.end() )
			phases.push_back( phase );
	return phases;
}

}
