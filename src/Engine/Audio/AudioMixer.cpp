#include "AudioMixer.h"
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;

// Срез фильтра нижних частот выше этого — фильтра нет
constexpr float lowpassBypass = 18000.0f;
// Тень головы: источник прямо сзади тише на столько и глуше до этой частоты
constexpr float rearAttenuation = 0.3f;
constexpr float rearLowpass = 5000.0f;

float lerp( float a, float b, float t )
{
	return a + ( b - a ) * t;
}

}

void spatialize( const AudioListener& listener, const XMFLOAT3& direction, float gain, AudioVoiceTarget& target )
{
	// Азимут в плоскости горизонта: вправо и вперёд слушателя; прямо сверху или снизу — посередине
	const float x = direction.x * listener.right.x + direction.z * listener.right.z;
	const float z = direction.x * listener.forward.x + direction.z * listener.forward.z;
	const float length = std::sqrt( x * x + z * z );
	const float pan = length > 1e-4f ? x / length : 0.0f;
	const float rear = length > 1e-4f ? std::max( -z / length, 0.0f ) : 0.0f;
	// Равная мощность: посередине — оба канала на −3 дБ
	const float angle = ( pan + 1.0f ) * 0.25f * pi;
	const float shadowed = gain * ( 1.0f - rearAttenuation * rear );
	target.left = shadowed * std::cos( angle );
	target.right = shadowed * std::sin( angle );
	target.lowpass = std::min( target.lowpass, lerp( 20000.0f, rearLowpass, rear ) );
}

AudioMixer::VoiceId AudioMixer::addVoice( std::unique_ptr<AudioGenerator> generator )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	Voice voice;
	voice.id = m_nextId++;
	voice.generator = std::move( generator );
	m_targets[voice.id] = AudioVoiceTarget();
	const VoiceId id = voice.id;
	m_added.push_back( std::move( voice ) );
	return id;
}

void AudioMixer::removeVoice( VoiceId id )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	m_targets.erase( id );
	m_removed.push_back( id );
}

void AudioMixer::setTarget( VoiceId id, const AudioVoiceTarget& target )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	const auto found = m_targets.find( id );
	if( found != m_targets.end() )
		found->second = target;
}

void AudioMixer::startRecording( float seconds )
{
	std::lock_guard<std::mutex> lock( m_mutex );
	m_record.clear();
	m_recordFrames = static_cast<size_t>( std::max( seconds, 0.0f ) * sampleRate );
	m_record.reserve( m_recordFrames * channels );
}

bool AudioMixer::recording() const
{
	std::lock_guard<std::mutex> lock( m_mutex );
	return m_recordFrames > 0;
}

bool AudioMixer::recordingFinished() const
{
	std::lock_guard<std::mutex> lock( m_mutex );
	return m_recordFrames > 0 && m_record.size() >= m_recordFrames * channels;
}

std::vector<float> AudioMixer::takeRecording()
{
	std::lock_guard<std::mutex> lock( m_mutex );
	m_recordFrames = 0;
	return std::move( m_record );
}

AudioMixer::Stats AudioMixer::stats() const
{
	Stats stats;
	stats.voices = m_voiceCount.load();
	stats.load = m_load.load();
	stats.clipped = m_clipped.load();
	stats.blocks = m_blocks.load();
	return stats;
}

void AudioMixer::render( float* stereo, uint32_t frames )
{
	const auto start = std::chrono::steady_clock::now();
	// Новое из главного потока: голоса, убранные, цели
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		for( Voice& voice : m_added )
			m_voices.push_back( std::move( voice ) );
		m_added.clear();
		for( VoiceId id : m_removed )
			for( Voice& voice : m_voices )
				if( voice.id == id )
					voice.fading = true;
		m_removed.clear();
		for( Voice& voice : m_voices )
		{
			const auto found = m_targets.find( voice.id );
			if( found != m_targets.end() )
				voice.target = found->second;
		}
	}

	const float blockSeconds = static_cast<float>( frames ) / sampleRate;
	const float smoothing = 1.0f - std::exp( -blockSeconds / smoothingSeconds );
	const float fading = 1.0f - std::exp( -blockSeconds / fadeSeconds );
	std::fill( stereo, stereo + frames * channels, 0.0f );
	m_scratch.resize( frames );
	for( Voice& voice : m_voices )
	{
		// Громкость — от прежней к новой линейно по блоку; новая — шаг к цели (убранный — к тишине)
		const float previousLeft = voice.current.left;
		const float previousRight = voice.current.right;
		const float step = voice.fading ? fading : smoothing;
		voice.current.left += ( ( voice.fading ? 0.0f : voice.target.left ) - voice.current.left ) * step;
		voice.current.right += ( ( voice.fading ? 0.0f : voice.target.right ) - voice.current.right ) * step;
		voice.current.lowpass += ( voice.target.lowpass - voice.current.lowpass ) * smoothing;
		for( uint32_t c = 0; c < AudioGenerator::controlCount; ++c )
			voice.current.controls[c] += ( voice.target.controls[c] - voice.current.controls[c] ) * smoothing;
		// Тихий голос не считается: генератор стоит (шум одинаков, пропуск не слышен)
		if( std::max( { previousLeft, previousRight, voice.current.left, voice.current.right } ) < 1e-5f )
			continue;

		voice.generator->render( m_scratch.data(), frames, voice.current.controls );
		const bool lowpass = voice.current.lowpass < lowpassBypass;
		const float coefficient = 1.0f - std::exp( -2.0f * pi * voice.current.lowpass / sampleRate );
		const float inverse = 1.0f / frames;
		for( uint32_t i = 0; i < frames; ++i )
		{
			float sample = m_scratch[i];
			if( lowpass )
			{
				voice.lowpassState += ( sample - voice.lowpassState ) * coefficient;
				sample = voice.lowpassState;
			}
			const float t = ( i + 1 ) * inverse;
			stereo[i * 2 + 0] += sample * lerp( previousLeft, voice.current.left, t );
			stereo[i * 2 + 1] += sample * lerp( previousRight, voice.current.right, t );
		}
	}
	// Затихшие убранные голоса — прочь (генератор уничтожается здесь же)
	m_voices.erase( std::remove_if( m_voices.begin(), m_voices.end(), []( const Voice& voice )
	{
		return voice.fading && std::max( voice.current.left, voice.current.right ) < 1e-4f;
	} ), m_voices.end() );

	uint64_t clipped = 0;
	for( uint32_t i = 0; i < frames * channels; ++i )
	{
		if( std::abs( stereo[i] ) > 1.0f )
		{
			stereo[i] = std::clamp( stereo[i], -1.0f, 1.0f );
			++clipped;
		}
	}
	m_clipped += clipped;
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		if( m_recordFrames > 0 && m_record.size() < m_recordFrames * channels )
		{
			const size_t count = std::min<size_t>( frames * channels, m_recordFrames * channels - m_record.size() );
			m_record.insert( m_record.end(), stereo, stereo + count );
		}
	}

	m_voiceCount = static_cast<uint32_t>( m_voices.size() );
	++m_blocks;
	const float seconds = std::chrono::duration<float>( std::chrono::steady_clock::now() - start ).count();
	m_load = m_load.load() * 0.95f + ( seconds / blockSeconds ) * 0.05f;
}

}
