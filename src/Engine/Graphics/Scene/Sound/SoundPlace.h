#pragma once

#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Engine\Audio\AudioMixer.h"
#include "Properties\PropertyContainer.h"

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

// Прослушивание части звука (подокно «Ambient sound», слой «Soundscape») — AudioAudition: флажки «Mute» и «Solo» окна части
// (редакторские, не сохраняются)
void addAuditionProperties( PropertyContainer& part );
// Множитель громкости части в этом кадре: включена — по флажкам (Solo отмечается у прослушивания), выключена — 0
float auditionGain( AudioAudition& audition, const PropertyContainer& part, bool enabled );
// «Clear solo» окна «Audio»: номер сброса у прослушивания сменился против seen (true — раз на номер, seen — новый) — объект
// снимает Solo своих частей (clearSolo)
bool takeClearSolo( const AudioAudition& audition, uint32_t& seen );
void clearSolo( PropertyContainer& part );
// «Show area» части или слоя (редакторский, не сохраняется): в режиме вьюпорта «Sound sources» — кольцо дальности вокруг
// слушателя и маски места на земле (SoundDebug.h)
void addAreaProperty( PropertyContainer& part );
bool showArea( const PropertyContainer& part );

}
