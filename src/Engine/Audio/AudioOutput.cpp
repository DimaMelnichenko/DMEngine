#include "AudioOutput.h"
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include "AudioMixer.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

std::string hresultText( HRESULT result )
{
	char text[16];
	sprintf_s( text, "0x%08X", static_cast<unsigned>( result ) );
	return text;
}

}

// Конец буфера голоса — событие нити микшера: пора считать следующий блок. Зовёт нить XAudio2: только сигнал
class AudioOutput::Callback : public IXAudio2VoiceCallback
{
public:
	explicit Callback( HANDLE event ) : m_event( event ) {}
	void STDMETHODCALLTYPE OnBufferEnd( void* ) override { SetEvent( m_event ); }
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart( UINT32 ) override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
	void STDMETHODCALLTYPE OnStreamEnd() override {}
	void STDMETHODCALLTYPE OnBufferStart( void* ) override {}
	void STDMETHODCALLTYPE OnLoopEnd( void* ) override {}
	void STDMETHODCALLTYPE OnVoiceError( void*, HRESULT ) override {}

private:
	HANDLE m_event;
};

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput()
{
	shutdown();
}

bool AudioOutput::initialize( AudioMixer& mixer )
{
	m_mixer = &mixer;
	// XAudio2 — свободнопоточный: ему достаточно многопоточного COM (главный поток уже в STA — работает и так)
	const HRESULT com = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
	m_comInitialized = SUCCEEDED( com );
	HRESULT result = XAudio2Create( &m_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
	if( FAILED( result ) )
	{
		LOG( "Audio: XAudio2 is not created (" + hresultText( result ) + "), no sound" );
		shutdown();
		return false;
	}
	// Мастер-голос — устройство по умолчанию в его каналах и частоте: стерео смеси XAudio2 разводит и пересчитывает сам
	result = m_xaudio->CreateMasteringVoice( &m_master );
	if( FAILED( result ) )
	{
		LOG( "Audio: no output device (" + hresultText( result ) + "), no sound" );
		shutdown();
		return false;
	}
	m_event = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	m_callback = std::make_unique<Callback>( static_cast<HANDLE>( m_event ) );
	WAVEFORMATEX format = {};
	format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	format.nChannels = static_cast<WORD>( AudioMixer::channels );
	format.nSamplesPerSec = AudioMixer::sampleRate;
	format.wBitsPerSample = 32;
	format.nBlockAlign = static_cast<WORD>( format.nChannels * format.wBitsPerSample / 8 );
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	result = m_xaudio->CreateSourceVoice( &m_voice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, m_callback.get() );
	if( FAILED( result ) )
	{
		LOG( "Audio: source voice is not created (" + hresultText( result ) + "), no sound" );
		m_voice = nullptr;
		shutdown();
		return false;
	}
	for( std::vector<float>& buffer : m_buffers )
		buffer.assign( blockFrames * AudioMixer::channels, 0.0f );
	// Очередь — сразу полная, затем нить держит её полной
	for( uint32_t i = 0; i < queuedBlocks; ++i )
		submit( i );
	m_voice->Start( 0 );
	m_stop = false;
	m_thread = std::thread( [this] { run(); } );

	XAUDIO2_VOICE_DETAILS details = {};
	m_master->GetVoiceDetails( &details );
	LOG( "Audio: XAudio2, output " + std::to_string( details.InputChannels ) + " channels at " + std::to_string( details.InputSampleRate ) +
		 " Hz, mix " + std::to_string( AudioMixer::sampleRate ) + " Hz, block " + std::to_string( blockFrames ) + " frames" );
	return true;
}

void AudioOutput::shutdown()
{
	if( m_thread.joinable() )
	{
		m_stop = true;
		SetEvent( static_cast<HANDLE>( m_event ) );
		m_thread.join();
	}
	if( m_voice )
	{
		m_voice->Stop( 0 );
		m_voice->DestroyVoice();
		m_voice = nullptr;
	}
	if( m_master )
	{
		m_master->DestroyVoice();
		m_master = nullptr;
	}
	if( m_xaudio )
	{
		m_xaudio->Release();
		m_xaudio = nullptr;
	}
	m_callback.reset();
	if( m_event )
	{
		CloseHandle( static_cast<HANDLE>( m_event ) );
		m_event = nullptr;
	}
	if( m_comInitialized )
	{
		CoUninitialize();
		m_comInitialized = false;
	}
}

void AudioOutput::setVolume( float volume )
{
	if( m_master )
		m_master->SetVolume( std::clamp( volume, 0.0f, 1.0f ) );
}

void AudioOutput::submit( uint32_t buffer )
{
	std::vector<float>& samples = m_buffers[buffer];
	m_mixer->render( samples.data(), blockFrames );
	XAUDIO2_BUFFER description = {};
	description.AudioBytes = static_cast<UINT32>( samples.size() * sizeof( float ) );
	description.pAudioData = reinterpret_cast<const BYTE*>( samples.data() );
	m_voice->SubmitSourceBuffer( &description );
}

void AudioOutput::run()
{
	SetThreadDescription( GetCurrentThread(), L"Audio mixer" );
	// Блок опоздал — щелчок: нить микшера впереди нитей кадра
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST );
	while( !m_stop )
	{
		WaitForSingleObject( static_cast<HANDLE>( m_event ), 100 );
		XAUDIO2_VOICE_STATE state = {};
		m_voice->GetState( &state, XAUDIO2_VOICE_NOSAMPLESPLAYED );
		// Буфер, который доиграл, свободен: очередь — по кругу (в очереди всегда последние queuedBlocks)
		for( uint32_t queued = state.BuffersQueued; queued < queuedBlocks && !m_stop; ++queued )
		{
			submit( m_nextBuffer );
			m_nextBuffer = ( m_nextBuffer + 1 ) % queuedBlocks;
		}
	}
}

}
