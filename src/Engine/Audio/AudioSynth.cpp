#include "AudioSynth.h"
#include <algorithm>
#include <cmath>

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;
// Коэффициенты фильтров — раз в столько отсчётов: управление меняется медленно, tan() на каждый отсчёт не нужен
constexpr uint32_t filterInterval = 32;

// Громкость слоёв на силе 1 — около −20 дБ FS (среднеквадратичное ~0,1): громкость в смеси задаёт голос (AudioMixer)
constexpr float windNormalization = 2.5f;
constexpr float hissNormalization = 1.2f;
constexpr float crackleNormalization = 3.0f;

}

float PinkNoise::next( float white )
{
	m_b[0] = 0.99886f * m_b[0] + white * 0.0555179f;
	m_b[1] = 0.99332f * m_b[1] + white * 0.0750759f;
	m_b[2] = 0.96900f * m_b[2] + white * 0.1538520f;
	m_b[3] = 0.86650f * m_b[3] + white * 0.3104856f;
	m_b[4] = 0.55000f * m_b[4] + white * 0.5329522f;
	m_b[5] = -0.7616f * m_b[5] - white * 0.0168980f;
	const float pink = m_b[0] + m_b[1] + m_b[2] + m_b[3] + m_b[4] + m_b[5] + m_b[6] + white * 0.5362f;
	m_b[6] = white * 0.115926f;
	return pink * 0.11f;
}

void StateVariableFilter::set( float frequency, float q )
{
	const float clamped = std::clamp( frequency, 10.0f, 0.45f * AudioMixer::sampleRate );
	const float g = std::tan( pi * clamped / AudioMixer::sampleRate );
	m_k = 1.0f / std::max( q, 0.05f );
	m_a1 = 1.0f / ( 1.0f + g * ( g + m_k ) );
	m_a2 = g * m_a1;
	m_a3 = g * m_a2;
}

StateVariableFilter::Output StateVariableFilter::process( float input )
{
	const float v3 = input - m_ic2;
	const float v1 = m_a1 * m_ic1 + m_a2 * v3;
	const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
	m_ic1 = 2.0f * v1 - m_ic1;
	m_ic2 = 2.0f * v2 - m_ic2;
	return { v2, m_k * v1, input - m_k * v1 - v2 };
}

float SmoothNoise::next( NoiseSource& noise )
{
	m_phase += m_rate / AudioMixer::sampleRate;
	if( m_phase >= 1.0f )
	{
		m_phase -= std::floor( m_phase );
		m_from = m_to;
		m_to = noise.white();
	}
	return m_from + ( m_to - m_from ) * ( 0.5f - 0.5f * std::cos( pi * m_phase ) );
}

WindGenerator::WindGenerator( uint32_t seed ) : m_noise( seed )
{
}

void WindGenerator::render( float* out, uint32_t frames, const float* controls )
{
	const float speed = std::max( controls[0], 0.0f );
	const float turbulence = std::clamp( controls[1], 0.0f, 1.0f );
	const float baseFrequency = controls[2] > 0.0f ? controls[2] : 120.0f;
	for( uint32_t i = 0; i < frames; ++i )
	{
		// Порывы внутри порыва: медленное и быстрое качание громкости и полосы
		const float flutter = std::max( 1.0f + turbulence * ( 0.45f * m_slow.next( m_noise ) + 0.25f * m_fast.next( m_noise ) ), 0.15f );
		if( m_filterCountdown == 0 )
		{
			// Середина полосы гула растёт со скоростью: штиль — низкий гул, сильный ветер — выше и шире
			const float frequency = baseFrequency * ( 1.0f + speed / 6.0f ) * ( 0.85f + 0.15f * flutter );
			m_body.set( frequency, 0.9f );
			m_air.set( frequency * 4.0f, 0.5f );
			m_whistle.set( frequency * 2.7f, 5.0f );
			m_filterCountdown = filterInterval;
		}
		--m_filterCountdown;
		const float pink = m_pink.next( m_noise.white() );
		const float body = m_body.process( pink ).band;
		const float air = m_air.process( pink ).band;
		const float whistle = m_whistle.process( pink ).band;
		out[i] = ( body + 0.35f * air + 0.12f * turbulence * whistle ) * flutter * std::sqrt( flutter ) * windNormalization;
	}
}

FoliageGenerator::FoliageGenerator( uint32_t seed, const FoliagePreset& preset ) :
	m_noise( seed ), m_preset( preset ), m_flutter( preset.flutterRate ),
	m_decay( std::exp( -1.0f / ( std::max( preset.crackleDecay, 1e-4f ) * AudioMixer::sampleRate ) ) )
{
}

void FoliageGenerator::render( float* out, uint32_t frames, const float* controls )
{
	const float strength = std::max( controls[0], 0.0f );
	const float bounded = std::min( strength, 2.0f );
	const float frequency = controls[1] > 0.0f ? controls[1] : 2000.0f;
	// Щелчки — с квадратом силы: в штиль редкие касания, в порыв — дробь
	const float crackleChance = m_preset.crackleRate * strength * strength / AudioMixer::sampleRate;
	for( uint32_t i = 0; i < frames; ++i )
	{
		if( m_filterCountdown == 0 )
		{
			// Сильнее ветер — шелест чуть выше
			const float shift = 0.85f + 0.15f * bounded;
			m_hiss.set( frequency * shift, m_preset.hissQ );
			m_crackle.set( frequency * m_preset.crackleRatio * shift, m_preset.crackleQ );
			m_filterCountdown = filterInterval;
		}
		--m_filterCountdown;
		const float flutter = std::max( 1.0f + m_preset.flutterDepth * std::min( bounded, 1.5f ) * m_flutter.next( m_noise ), 0.0f );
		const float hiss = m_hiss.process( m_noise.white() ).band * flutter * hissNormalization;
		if( m_noise.uniform() < crackleChance )
			m_envelope = std::max( m_envelope, 0.4f + 0.6f * m_noise.uniform() );
		m_envelope *= m_decay;
		const float crackle = m_crackle.process( m_noise.white() * m_envelope ).band * crackleNormalization;
		out[i] = hiss * ( 1.0f - m_preset.crackleShare ) + crackle * m_preset.crackleShare;
	}
}

}
