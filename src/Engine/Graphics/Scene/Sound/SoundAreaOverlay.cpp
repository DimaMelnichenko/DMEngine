#include "SoundAreaOverlay.h"
#include <algorithm>
#include <cmath>
#include "System.h"
#include "Logger\Logger.h"
#include "Terrain\TerrainPipeline.h"

namespace GS
{

const ShaderView* SoundAreaOverlay::update( const TerrainPipeline& terrain, std::vector<std::string> masks )
{
	std::sort( masks.begin(), masks.end() );
	masks.erase( std::unique( masks.begin(), masks.end() ), masks.end() );
	if( masks != m_masks || terrain.maskRevision() != m_maskRevision )
	{
		m_masks = std::move( masks );
		m_maskRevision = terrain.maskRevision();
		// Маски по максимуму: место слоя — там, где есть хоть одна из его масок (певец садится по их сумме — подсветка
		// показывает, где он может сесть)
		std::vector<uint8_t> bytes;
		uint32_t size = 0;
		for( const std::string& name : m_masks )
		{
			const std::vector<float>* values = terrain.maskValues( name );
			if( !values )
				continue;
			if( bytes.empty() )
			{
				size = static_cast<uint32_t>( std::lround( std::sqrt( static_cast<double>( values->size() ) ) ) );
				bytes.assign( static_cast<size_t>( size ) * size, 0 );
			}
			if( values->size() != bytes.size() )
				continue;
			for( size_t i = 0; i < bytes.size(); ++i )
				bytes[i] = std::max( bytes[i], static_cast<uint8_t>( std::lround( std::clamp( ( *values )[i], 0.0f, 1.0f ) * 255.0f ) ) );
		}
		m_valid = false;
		if( !bytes.empty() )
		{
			TextureDesc desc;
			desc.width = desc.height = size;
			desc.format = DXGI_FORMAT_R8_UNORM;
			TextureData data;
			data.data = bytes.data();
			data.rowPitch = size;
			m_valid = System::textures().setGenerated( textureName, desc, data );
			if( !m_valid )
				LOG( "Sound areas: texture " + std::string( textureName ) + " is not created" );
		}
	}
	return m_valid ? &System::textures().get( System::textures().id( textureName ) )->srv() : nullptr;
}

}
