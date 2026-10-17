#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "AudioMixer.h"

namespace GS
{

// Запись звука в памяти — как Sound Wave в UE: моно float с частотой микшера и её фразы (строка Sounds: phrases — их собирает
// Tools/pack_sound.py по огибающей громкости). Читает WAV, который собрал тот же скрипт (16 бит или float32; каналы — в один)
struct AudioClip
{
	struct Phrase
	{
		uint32_t start = 0;		// отсчёты
		uint32_t end = 0;
	};
	std::vector<float> samples;
	std::vector<Phrase> phrases;

	// path — WAV; phrases — «начало-конец;…» в секундах (пусто — вся запись одной фразой). false — reason
	static std::shared_ptr<const AudioClip> load( const std::string& path, const std::string& phrases, std::string& reason );
};

// Фраза записи один раз — с плавным входом и выходом (без щелчка на краях) и сдвигом высоты (pitch: шаг чтения ≠ 1, линейная
// интерполяция; 1,05 — на полтона выше и короче). Дозвучала — тишина: голос убирает тот, кто его завёл, по seconds()
class ClipGenerator : public AudioGenerator
{
public:
	ClipGenerator( std::shared_ptr<const AudioClip> clip, const AudioClip::Phrase& phrase, float pitch );
	void render( float* out, uint32_t frames, const float* controls ) override;
	// Сколько звучит фраза, с
	float seconds() const;

private:
	std::shared_ptr<const AudioClip> m_clip;	// запись живёт, пока звучит голос (и после разрушения своего слоя)
	double m_position;
	double m_end;
	double m_start;
	float m_step;
};

}
