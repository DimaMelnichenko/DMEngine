#pragma once

#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Engine\Audio\AudioMixer.h"

namespace GS
{

class TerrainPipeline;
struct RenderView;

// Место звука на рельефе — общее у звуков окружения (AmbientSound) и певцов (Soundscape): слушатель по виду, маски в точке,
// поглощение воздухом и заглушение гребнем (docs/audio.md)

// Слушатель — камера вида: вправо — первая строка обратной матрицы вида (крена нет — горизонтально), вперёд — по горизонту
AudioListener listenerFromView( const RenderView& view );

// Маски по именам (TerrainPipeline::maskValues, сетка карты высот): нет маски — её нет и в списке. Указатели — до перестройки
// масок: брать заново каждый кадр
std::vector<const std::vector<float>*> terrainMasks( const TerrainPipeline* terrain, const std::vector<std::string>& names );
// Сумма масок в точке мира, не больше 1; вне карты — 0
float terrainMaskAt( const TerrainPipeline* terrain, const std::vector<const std::vector<float>*>& masks, float x, float z );

// Поглощение воздухом: срез фильтра нижних частот на расстоянии, Гц (в 100 м — ~4 кГц, как ISO 9613-1 на высоких)
float airAbsorptionLowpass( float distance );
// Заглушение гребнем 0…1: насколько рельеф поднимается над прямой from — to (луч по карте высот): до 0,5 м — ещё не гребень
// (бровки русла), на 3 м — заглушено полностью
float terrainOcclusion( const TerrainPipeline* terrain, const DirectX::XMFLOAT3& from, const DirectX::XMFLOAT3& to );
// Заглушённый источник — тише на долю и глуше: множитель громкости и срез фильтра по заглушению
float occlusionGain( float occlusion );
float occlusionLowpass( float occlusion );

}
