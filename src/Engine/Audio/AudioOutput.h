#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace GS
{

class AudioMixer;

// Вывод звука — XAudio2 2.9 из Windows SDK (как AudioMixerPlatformXAudio2 в UE: XAudio2 только выводит, смесь считает свой
// микшер). Мастер-голос и один стерео-голос float; своя нить по событию конца буфера считает следующий блок смеси
// (AudioMixer::render) и ставит его в очередь — в очереди queuedBlocks блоков по blockFrames (задержка ~30 мс). Громкость и
// «Mute» — у мастер-голоса: запись смеси их не видит
class AudioOutput
{
public:
	static constexpr uint32_t blockFrames = 480;	// 10 мс при 48 кГц
	static constexpr uint32_t queuedBlocks = 3;

	AudioOutput();
	~AudioOutput();
	// Устройство вывода и нить микшера; false — нет устройства или XAudio2 (строка в лог), движок работает без звука
	bool initialize( AudioMixer& mixer );
	// Нить стоит, голоса и XAudio2 освобождены; повторный вызов ничего не делает
	void shutdown();
	bool active() const { return m_voice != nullptr; }
	// Громкость мастер-голоса 0…1 (0 — тишина)
	void setVolume( float volume );

private:
	class Callback;
	void run();
	void submit( uint32_t buffer );

	AudioMixer* m_mixer = nullptr;
	IXAudio2* m_xaudio = nullptr;
	IXAudio2MasteringVoice* m_master = nullptr;
	IXAudio2SourceVoice* m_voice = nullptr;
	std::unique_ptr<Callback> m_callback;
	void* m_event = nullptr;			// HANDLE: буфер доигран
	std::thread m_thread;
	std::atomic<bool> m_stop{ false };
	std::vector<float> m_buffers[queuedBlocks];
	uint32_t m_nextBuffer = 0;
	bool m_comInitialized = false;
};

}
