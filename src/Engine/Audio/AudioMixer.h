#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "AudioAudition.h"

namespace GS
{

// Источник звука голоса — моно: блок отсчётов с частотой AudioMixer::sampleRate. Зовёт нить микшера; управление (controls:
// скорость ветра, сила шелеста) приходит из главного потока целями голоса и плавно меняется от блока к блоку
class AudioGenerator
{
public:
	static constexpr uint32_t controlCount = 3;
	virtual ~AudioGenerator() = default;
	virtual void render( float* out, uint32_t frames, const float* controls ) = 0;
};

// Цели голоса на кадр: громкость левого и правого канала, срез фильтра нижних частот (поглощение воздухом, тень головы),
// управление генератором. Нить микшера ведёт голос к ним плавно (без щелчков), какой бы ни была частота кадров
struct AudioVoiceTarget
{
	float left = 0.0f;
	float right = 0.0f;
	float lowpass = 20000.0f;	// Гц
	float controls[AudioGenerator::controlCount] = {};
};

// Слушатель — камера: где стоит и куда смотрит (вправо — для стерео)
struct AudioListener
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT3 forward = DirectX::XMFLOAT3( 0.0f, 0.0f, 1.0f );
	DirectX::XMFLOAT3 right = DirectX::XMFLOAT3( 1.0f, 0.0f, 0.0f );
};

// Громкость каналов от направления на источник (мир, к источнику) у слушателя: стерео равной мощностью по азимуту в плоскости
// горизонта, сзади — тише и глуше (тень головы). gain — общая громкость; срез фильтра — меньший из прежнего и тени головы;
// spread 0…1 — источник вокруг слушателя (стоит в нём): 1 — посередине, без тени головы
void spatialize( const AudioListener& listener, const DirectX::XMFLOAT3& direction, float gain, AudioVoiceTarget& target,
				 float spread = 0.0f );

// Микшер звука — как Audio Mixer в UE: свой расчёт смеси, вывод — отдельно (AudioOutput). Голоса — генераторы с целями
// громкости по каналам и фильтра; главный поток добавляет и убирает голоса и задаёт цели, нить вывода зовёт render за
// блоком смеси. Убранный голос затихает и уходит в нити микшера. Запись смеси в память (до громкости и «Mute» мастер-голоса)
// — для проверки без наушников (команда audio record, Tools/audio_check.py)
class AudioMixer
{
public:
	static constexpr uint32_t sampleRate = 48000;
	static constexpr uint32_t channels = 2;
	// Сглаживание целей: громкость и управление идут к цели с этой постоянной времени, с; убранный голос затихает за fadeSeconds
	static constexpr float smoothingSeconds = 0.05f;
	static constexpr float fadeSeconds = 0.1f;
	// Ограничитель пиков смеси (как Limiter мастер-шины в UE): громче порога — усиление сразу вниз до порога, обратно — с этой
	// постоянной времени, с. Пересечение многих громких голосов (перекат рядом с кронами в бурю) не уходит в перегрузку
	static constexpr float limiterThreshold = 0.7f;
	static constexpr float limiterRelease = 0.15f;
	using VoiceId = uint32_t;

	// Главный поток
	VoiceId addVoice( std::unique_ptr<AudioGenerator> generator );
	void removeVoice( VoiceId id );
	void setTarget( VoiceId id, const AudioVoiceTarget& target );
	// Запись смеси: seconds секунд с ближайшего блока; готова — recordingFinished, отсчёты (каналы через один) — takeRecording
	void startRecording( float seconds );
	bool recording() const;
	bool recordingFinished() const;
	std::vector<float> takeRecording();
	struct Stats
	{
		uint32_t voices = 0;
		float load = 0.0f;			// доля реального времени на расчёт блока
		uint64_t clipped = 0;		// отсчётов за ±1 с начала
		uint64_t limited = 0;		// кадров, приглушённых ограничителем, с начала
		uint64_t blocks = 0;
	};
	Stats stats() const;
	// Прослушивание в редакторе (Mute и Solo частей звука уровня) — главный поток: части множат на него громкость целей
	AudioAudition& audition() { return m_audition; }

	// Нить вывода: блок frames кадров смеси, каналы через один
	void render( float* stereo, uint32_t frames );

private:
	struct Voice
	{
		VoiceId id = 0;
		std::unique_ptr<AudioGenerator> generator;
		AudioVoiceTarget target;	// последняя цель из главного потока
		AudioVoiceTarget current;	// где голос сейчас
		float lowpassState = 0.0f;
		bool fading = false;
	};

	// Общее с главным потоком — под мьютексом
	mutable std::mutex m_mutex;
	std::vector<Voice> m_added;
	std::vector<VoiceId> m_removed;
	std::unordered_map<VoiceId, AudioVoiceTarget> m_targets;
	VoiceId m_nextId = 1;
	std::vector<float> m_record;
	size_t m_recordFrames = 0;		// сколько записать; 0 — запись не идёт

	// Только нить микшера
	std::vector<Voice> m_voices;
	std::vector<float> m_scratch;

	std::atomic<uint32_t> m_voiceCount{ 0 };
	std::atomic<float> m_load{ 0.0f };
	std::atomic<uint64_t> m_clipped{ 0 };
	std::atomic<uint64_t> m_limited{ 0 };
	float m_limiterGain = 1.0f;
	std::atomic<uint64_t> m_blocks{ 0 };

	// Только главный поток
	AudioAudition m_audition;
};

}
