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

int Material::phaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options ) const
{
	return permutation( permutationKey( params, options, false ) );
}

int Material::depthPhaseFor( const PropertyContainer& params, const ShaderPhaseOptions& options ) const
{
	if( renderState( params ).blendMode == BlendMode::translucent )
		return -1;
	return permutation( permutationKey( params, options, true ) );
}

uint32_t Material::permutationKey( const PropertyContainer& params, const ShaderPhaseOptions& options, bool depth ) const
{
	using namespace ShaderPermutation;
	const bool masked = renderState( params ).blendMode == BlendMode::masked;
	if( options.impostorBake )
		return impostorBake | ( masked ? clipAlpha : 0 );
	uint32_t key = depth ? depthOnly : 0;
	if( options.skinned )
		key |= skinned;
	if( options.placed )
		key |= placed;
	else if( options.instanced && !options.lodDither && !options.skinned )
		key |= instanced;
	if( options.lodDither )
		key |= lodDither;
	const bool clip = depth || !options.depthFromPrepass;
	if( masked && clip )
		key |= clipAlpha;
	if( options.lodDither && clip )
		key |= clipDither;
	return key;
}

std::vector<int> Material::permutationPhases( uint32_t mask, uint32_t bits ) const
{
	std::vector<int> phases;
	const std::vector<int>& table = permutations();
	for( uint32_t key = 0; key < table.size(); ++key )
	{
		const int phase = table[key];
		if( phase >= 0 && ( key & mask ) == bits && std::find( phases.begin(), phases.end(), phase ) == phases.end() )
			phases.push_back( phase );
	}
	return phases;
}

std::vector<int> Material::depthPhases() const
{
	return permutationPhases( ShaderPermutation::depthOnly | ShaderPermutation::impostorBake, ShaderPermutation::depthOnly );
}

std::vector<int> Material::bakePhases() const
{
	return permutationPhases( ShaderPermutation::impostorBake, ShaderPermutation::impostorBake );
}

std::vector<int> Material::colorPhases() const
{
	return permutationPhases( ShaderPermutation::depthOnly | ShaderPermutation::impostorBake, 0 );
}

}
