#include "AudioClip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include "Utils\utilites.h"

namespace GS
{

namespace
{

// Плавный вход и выход фразы, с: край записи не щёлкает, даже если фраза вырезана посреди звука
constexpr double phraseFade = 0.02;

uint32_t readU32( const uint8_t* bytes )
{
	return bytes[0] | ( bytes[1] << 8 ) | ( bytes[2] << 16 ) | ( static_cast<uint32_t>( bytes[3] ) << 24 );
}

uint16_t readU16( const uint8_t* bytes )
{
	return static_cast<uint16_t>( bytes[0] | ( bytes[1] << 8 ) );
}

}

std::shared_ptr<const AudioClip> AudioClip::load( const std::string& path, const std::string& phrases, std::string& reason )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		reason = "no file " + path;
		return nullptr;
	}
	const std::vector<uint8_t> data( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	if( data.size() < 12 || std::memcmp( data.data(), "RIFF", 4 ) != 0 || std::memcmp( data.data() + 8, "WAVE", 4 ) != 0 )
	{
		reason = path + " is not a WAV";
		return nullptr;
	}
	// Части RIFF: fmt (формат, каналы, частота, бит) и data
	uint16_t format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	const uint8_t* samples = nullptr;
	size_t sampleBytes = 0;
	for( size_t position = 12; position + 8 <= data.size(); )
	{
		const uint32_t size = readU32( data.data() + position + 4 );
		const uint8_t* body = data.data() + position + 8;
		const size_t available = std::min<size_t>( size, data.size() - position - 8 );
		if( std::memcmp( data.data() + position, "fmt ", 4 ) == 0 && available >= 16 )
		{
			format = readU16( body );
			channels = readU16( body + 2 );
			rate = readU32( body + 4 );
			bits = readU16( body + 14 );
			// WAVE_FORMAT_EXTENSIBLE: настоящий формат — в начале подформата
			if( format == 0xFFFE && available >= 26 )
				format = readU16( body + 24 );
		}
		else if( std::memcmp( data.data() + position, "data", 4 ) == 0 )
		{
			samples = body;
			sampleBytes = available;
		}
		position += 8 + size + ( size & 1 );
	}
	const bool pcm16 = format == 1 && bits == 16;
	const bool float32 = format == 3 && bits == 32;
	if( !samples || channels == 0 || ( !pcm16 && !float32 ) )
	{
		reason = path + ": expected 16-bit PCM or 32-bit float WAV";
		return nullptr;
	}
	if( rate != AudioMixer::sampleRate )
	{
		reason = path + ": " + std::to_string( rate ) + " Hz, the mixer needs " + std::to_string( AudioMixer::sampleRate ) +
				 " (Tools/pack_sound.py)";
		return nullptr;
	}

	auto clip = std::make_shared<AudioClip>();
	const size_t frames = sampleBytes / ( channels * ( bits / 8 ) );
	clip->samples.resize( frames );
	for( size_t i = 0; i < frames; ++i )
	{
		float sum = 0.0f;
		for( uint16_t c = 0; c < channels; ++c )
		{
			const uint8_t* sample = samples + ( i * channels + c ) * ( bits / 8 );
			if( pcm16 )
				sum += static_cast<int16_t>( readU16( sample ) ) / 32768.0f;
			else
			{
				float value;
				std::memcpy( &value, sample, sizeof( value ) );
				sum += value;
			}
		}
		clip->samples[i] = sum / channels;
	}

	// Фразы: «начало-конец» в секундах через «;»
	std::vector<std::string> parts;
	str_split( phrases, parts, ";" );
	for( const std::string& part : parts )
	{
		float from = 0.0f, to = 0.0f;
		if( sscanf_s( part.c_str(), "%f-%f", &from, &to ) != 2 || to <= from )
			continue;
		Phrase phrase;
		phrase.start = static_cast<uint32_t>( std::min<double>( from * AudioMixer::sampleRate, static_cast<double>( frames ) ) );
		phrase.end = static_cast<uint32_t>( std::min<double>( to * AudioMixer::sampleRate, static_cast<double>( frames ) ) );
		if( phrase.end > phrase.start )
			clip->phrases.push_back( phrase );
	}
	if( clip->phrases.empty() && frames > 0 )
		clip->phrases.push_back( { 0, static_cast<uint32_t>( frames ) } );
	return clip;
}

ClipGenerator::ClipGenerator( std::shared_ptr<const AudioClip> clip, const AudioClip::Phrase& phrase, float pitch ) :
	m_clip( std::move( clip ) ), m_position( phrase.start ), m_end( phrase.end ), m_start( phrase.start ),
	m_step( std::clamp( pitch, 0.25f, 4.0f ) )
{
}

float ClipGenerator::seconds() const
{
	return static_cast<float>( ( m_end - m_start ) / m_step / AudioMixer::sampleRate );
}

void ClipGenerator::render( float* out, uint32_t frames, const float* )
{
	const std::vector<float>& samples = m_clip->samples;
	const double fade = phraseFade * AudioMixer::sampleRate;
	for( uint32_t i = 0; i < frames; ++i )
	{
		if( m_position >= m_end - 1.0 )
		{
			out[i] = 0.0f;
			continue;
		}
		const size_t index = static_cast<size_t>( m_position );
		const float t = static_cast<float>( m_position - index );
		const float value = samples[index] + ( samples[index + 1] - samples[index] ) * t;
		const double edge = std::min( m_position - m_start, m_end - m_position );
		out[i] = value * static_cast<float>( std::min( edge / fade, 1.0 ) );
		m_position += m_step;
	}
}

}
