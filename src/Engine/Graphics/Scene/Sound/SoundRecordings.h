#pragma once

#include <memory>
#include <string>
#include "Engine\Audio\AudioClip.h"

namespace GS
{

// Записи звука уровня по имени (строки Sounds) — для звуков, которые сами записей не держат (ручьи AmbientSound), как
// TerrainHeightSource у рельефа. Реализует Soundscape: у него записи, их звук (читается при первой нужде) и громкость окна
// «Sounds» — правка окна слышна и у тех, кто взял запись отсюда
class SoundRecordings
{
public:
	virtual ~SoundRecordings() = default;
	// Звук записи; nullptr — нет такой записи или она не читается
	virtual std::shared_ptr<const AudioClip> recording( const std::string& name ) = 0;
	// Громкость записи (окно «Sounds», Sounds.volume); нет записи — 1
	virtual float recordingVolume( const std::string& name ) const = 0;
};

}
