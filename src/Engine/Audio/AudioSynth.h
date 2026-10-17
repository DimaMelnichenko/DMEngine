#pragma once

#include <cstdint>
#include <vector>
#include "AudioMixer.h"

namespace GS
{

// Синтез звуков окружения (docs/audio.md, «Синтез»): шум и фильтры, генераторы голосов микшера. Ветер и шелест — по
// A. Farnell, «Designing Sound» (2010), гл. «Wind» и «Rustling leaves»: шум в полосе, которая растёт с силой ветра, и
// редкие щелчки шума в полосе листьев и иголок

// Белый шум (xorshift32): у каждого голоса своё зерно — голоса не звучат одинаково
class NoiseSource
{
public:
	explicit NoiseSource( uint32_t seed ) : m_state( seed ? seed : 0x9E3779B9u ) {}
	// −1…1
	float white()
	{
		m_state ^= m_state << 13;
		m_state ^= m_state >> 17;
		m_state ^= m_state << 5;
		return static_cast<float>( static_cast<int32_t>( m_state ) ) * ( 1.0f / 2147483648.0f );
	}
	// 0…1
	float uniform() { return white() * 0.5f + 0.5f; }

private:
	uint32_t m_state;
};

// Розовый шум из белого — фильтр П. Келлета (−3 дБ на октаву, как шум ветра и воды)
class PinkNoise
{
public:
	float next( float white );

private:
	float m_b[7] = {};
};

// Фильтр с переменным состоянием (TPT SVF, В. Завалишин, «The Art of VA Filter Design»): полоса, низкие и высокие частоты;
// устойчив при смене частоты на ходу
class StateVariableFilter
{
public:
	void set( float frequency, float q );
	struct Output
	{
		float low;
		float band;		// на середине полосы — единица
		float high;
	};
	Output process( float input );

private:
	float m_k = 1.0f;
	float m_a1 = 0.0f;
	float m_a2 = 0.0f;
	float m_a3 = 0.0f;
	float m_ic1 = 0.0f;
	float m_ic2 = 0.0f;
};

// Медленная случайная кривая −1…1: новое случайное значение каждые 1 / rate с, между ними — плавно (косинус)
class SmoothNoise
{
public:
	explicit SmoothNoise( float rate ) : m_rate( rate ) {}
	float next( NoiseSource& noise );

private:
	float m_rate;
	float m_phase = 1.0f;
	float m_from = 0.0f;
	float m_to = 0.0f;
};

// Ветер у слушателя — гул, шум и свист воздуха (без растений): розовый шум в полосах, середина которых растёт со скоростью ветра,
// громкость и полоса качаются порывистостью. controls: [0] — скорость ветра у слушателя, м/с; [1] — порывистость 0…1;
// [2] — середина полосы гула в штиль, Гц (её подбирают в окне «Ambient sound»)
class WindGenerator : public AudioGenerator
{
public:
	explicit WindGenerator( uint32_t seed );
	void render( float* out, uint32_t frames, const float* controls ) override;

private:
	NoiseSource m_noise;
	PinkNoise m_pink;
	StateVariableFilter m_body;		// гул
	StateVariableFilter m_air;		// шум воздуха выше гула
	StateVariableFilter m_whistle;	// свист на порывах
	SmoothNoise m_slow{ 0.25f };
	SmoothNoise m_fast{ 1.3f };
	uint32_t m_filterCountdown = 0;
};

// Шелест растений: шипение шума в полосе (трава — высокая, кроны ели — ниже) с трепетом громкости и редкие щелчки — касания
// травинок, иголок, листьев. controls: [0] — сила: нагрузка ветра на растения 0…~2 (щелчков — с её квадратом); [1] — середина
// полосы шипения, Гц (её подбирают в окне «Ambient sound»)
struct FoliagePreset
{
	float hissQ = 0.7f;
	float flutterRate = 6.0f;		// трепет громкости, Гц
	float flutterDepth = 0.5f;
	float crackleRatio = 1.4f;		// середина полосы щелчков — во столько раз выше шипения
	float crackleQ = 1.2f;
	float crackleRate = 50.0f;		// щелчков в секунду при силе 1
	float crackleDecay = 0.002f;	// затухание щелчка, с
	float crackleShare = 0.3f;		// доля щелчков в смеси
};

class FoliageGenerator : public AudioGenerator
{
public:
	FoliageGenerator( uint32_t seed, const FoliagePreset& preset );
	void render( float* out, uint32_t frames, const float* controls ) override;

private:
	NoiseSource m_noise;
	FoliagePreset m_preset;
	StateVariableFilter m_hiss;
	StateVariableFilter m_crackle;
	SmoothNoise m_flutter;
	float m_envelope = 0.0f;
	float m_decay;
	uint32_t m_filterCountdown = 0;
};

// Ручей — пузырьки (K. van den Doel, «Physically-based models for liquid sounds», ACM TAP 2(4), 2005): пузырёк, захваченный
// течением, звенит затухающей синусоидой с частотой Миннарта (~3,26 кГц / радиус в мм), частота растёт по мере всплытия;
// радиусы — по степенному закону, их поток — по силе течения; на перекате пузырьков больше и они крупнее, и шумит поток.
// controls: [0] — сила 0…~2 (частота пузырьков; громкость от неё не растёт — её задаёт голос); [1] — перекат 0…1 (крупные
// пузырьки и шум потока)
class BrookGenerator : public AudioGenerator
{
public:
	explicit BrookGenerator( uint32_t seed );
	void render( float* out, uint32_t frames, const float* controls ) override;

private:
	// Пузырёк: вращение фазора (без синуса на каждый отсчёт), амплитуда с затуханием, частота растёт со временем
	struct Bubble
	{
		float re = 0.0f;
		float im = 0.0f;
		float amplitude = 0.0f;
		float decay = 1.0f;			// множитель амплитуды за отсчёт
		float frequency = 0.0f;		// начальная, Гц
		float rise = 0.0f;			// рост частоты, доля за секунду
		float age = 0.0f;			// с
	};
	static constexpr uint32_t maxBubbles = 48;
	// Пузырёк с отсчёта start блока до конца
	void renderBubble( Bubble& bubble, float* out, uint32_t start, uint32_t frames );

	NoiseSource m_noise;
	PinkNoise m_pink;
	StateVariableFilter m_rush;
	Bubble m_bubbles[maxBubbles];
	uint32_t m_count = 0;
	std::vector<uint32_t> m_spawns;
};

}
