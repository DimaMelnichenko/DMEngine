#include "AudioDevice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include "FrameStats.h"
#include "Logger\Logger.h"
#include "Engine\Console\ConsoleCommands.h"

namespace GS
{

namespace
{

template<typename T>
void writeValue( std::ofstream& file, T value )
{
	file.write( reinterpret_cast<const char*>( &value ), sizeof( value ) );
}

}

AudioDevice::AudioDevice()
{
	m_properties.setName( "Audio" );
}

bool AudioDevice::initialize( bool enabled, float volume, bool muted )
{
	Property* volumeProperty = m_properties.insert( "Volume", std::clamp( volume, 0.0f, 1.0f ) );
	volumeProperty->setLow( 0.0f );
	volumeProperty->setHigh( 1.0f );
	volumeProperty->setControlType( GUIControlType::SLIDER );
	volumeProperty->setTooltip( "Master volume of the output; starts from [Audio] Volume of settings.ini" );
	m_properties.insert( "Mute", muted )->setTooltip( "Silent output (-mute); the mixer and audio record keep working" );
	m_properties.insert( "Show sources", false )->setTooltip(
		"Sound sources in the viewport (View > Debug views): singers and places of water as icons (a click selects the layer), "
		"areas of the parts and layers with Show area - rings around the listener and habitat masks on the ground" );
	m_properties.addButton( "Clear solo", "Turn off Solo of every part of the level sound (Ambient sound, Soundscape layers)",
							[this]() -> PropertyContainer::ButtonAction
	{
		m_mixer.audition().clearSolo();
		return {};
	} );
	if( !enabled )
	{
		LOG( "Audio: disabled in settings.ini ([Audio] Enabled=false)" );
		return false;
	}
	return m_output.initialize( m_mixer );
}

void AudioDevice::shutdown()
{
	m_output.shutdown();
}

void AudioDevice::update( FrameStats& stats )
{
	m_mixer.audition().beginFrame();
	if( !m_output.active() )
		return;
	if( m_properties.revision() != m_appliedRevision )
	{
		m_appliedRevision = m_properties.revision();
		m_output.setVolume( m_properties["Mute"].data<bool>() ? 0.0f : m_properties["Volume"].data<float>() );
	}
	const AudioMixer::Stats mixer = m_mixer.stats();
	stats.add( "Audio voices = %.0f", static_cast<float>( mixer.voices ) );
	stats.add( "Audio mixer load = %.1f %%", mixer.load * 100.0f );
}

void AudioDevice::registerCommands( ConsoleCommands& console )
{
	console.registerCommand( "audio", "record <file.wav> <seconds> - the mix (before the volume and Mute of the output) into a 16-bit "
							 "stereo WAV; stats - voices, mixer load, clipped samples, frames quietened by the limiter",
							 [this, &console]( const std::vector<std::string>& args, const ConsoleReplyPtr& reply )
	{
		if( args.size() == 1 && args[0] == "stats" )
		{
			const AudioMixer::Stats stats = m_mixer.stats();
			char text[256];
			std::snprintf( text, sizeof( text ), "output %s, voices %u, mixer load %.2f %%, clipped samples %llu, limited frames %llu, "
						   "blocks %llu", m_output.active() ? "active" : "none", stats.voices, stats.load * 100.0f,
						   static_cast<unsigned long long>( stats.clipped ), static_cast<unsigned long long>( stats.limited ),
						   static_cast<unsigned long long>( stats.blocks ) );
			return reply->ok( text );
		}
		float seconds = 0.0f;
		if( args.size() != 3 || args[0] != "record" || sscanf_s( args[2].c_str(), "%f", &seconds ) != 1 || seconds <= 0.0f )
			return reply->error( "usage: audio record <file.wav> <seconds> | audio stats" );
		if( !m_output.active() )
			return reply->error( "no audio output: nothing renders the mix" );
		if( m_mixer.recording() )
			return reply->error( "a recording is already running" );
		m_mixer.startRecording( seconds );
		const std::string path = args[1];
		// Запись идёт в нити микшера; готова — в файл (в главном потоке, в начале кадра)
		console.addFrameTask( [this, path, reply]
		{
			if( !m_mixer.recordingFinished() )
				return false;
			const std::vector<float> samples = m_mixer.takeRecording();
			if( writeWav( path, samples ) )
				reply->ok( "written " + path + ", " + std::to_string( samples.size() / AudioMixer::channels ) + " frames" );
			else
				reply->error( "can`t write " + path );
			return true;
		} );
	} );
}

bool AudioDevice::writeWav( const std::string& path, const std::vector<float>& samples )
{
	std::ofstream file( path, std::ios::binary );
	if( !file )
		return false;
	const uint32_t dataBytes = static_cast<uint32_t>( samples.size() * sizeof( int16_t ) );
	const uint16_t channels = static_cast<uint16_t>( AudioMixer::channels );
	const uint32_t rate = AudioMixer::sampleRate;
	file.write( "RIFF", 4 );
	writeValue<uint32_t>( file, 36 + dataBytes );
	file.write( "WAVEfmt ", 8 );
	writeValue<uint32_t>( file, 16 );
	writeValue<uint16_t>( file, 1 );	// PCM
	writeValue<uint16_t>( file, channels );
	writeValue<uint32_t>( file, rate );
	writeValue<uint32_t>( file, rate * channels * sizeof( int16_t ) );
	writeValue<uint16_t>( file, static_cast<uint16_t>( channels * sizeof( int16_t ) ) );
	writeValue<uint16_t>( file, 16 );
	file.write( "data", 4 );
	writeValue<uint32_t>( file, dataBytes );
	for( float sample : samples )
		writeValue<int16_t>( file, static_cast<int16_t>( std::lround( std::clamp( sample, -1.0f, 1.0f ) * 32767.0f ) ) );
	return static_cast<bool>( file );
}

}
