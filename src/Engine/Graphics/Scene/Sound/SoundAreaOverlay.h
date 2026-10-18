#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ShaderView;

namespace GS
{

class TerrainPipeline;

// Маски мест звука на земле — режим вьюпорта «Sound sources», «Show area» у слоя или части (SoundDebug.h): маски областей
// по максимуму в одну текстуру хранилища «sound_area» (R8, сетка масок TerrainPipeline), её подсвечивает террейн
// (CDLODTerrain::setHighlight, как цель кисти). Заново — когда сменился набор масок или сами маски (TerrainPipeline::maskRevision)
class SoundAreaOverlay
{
public:
	static constexpr const char* textureName = "sound_area";

	// Вид текстуры для масок по именам; nullptr — масок нет (ни одной у рельефа)
	const ShaderView* update( const TerrainPipeline& terrain, std::vector<std::string> masks );

private:
	std::vector<std::string> m_masks;	// по алфавиту, без повторов
	uint32_t m_maskRevision = ~0u;
	bool m_valid = false;
};

}
