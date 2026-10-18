#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "AudioMixer.h"
#include "AudioOutput.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

class ConsoleCommands;
struct FrameStats;

// Звук движка — как FAudioDevice в UE (docs/audio.md): микшер (AudioMixer) и вывод (AudioOutput, XAudio2). Голоса заводят
// объекты сцены (AmbientSound, Soundscape). Окно «Audio» (раздел Audio в Outliner) — громкость и «Mute» пользователя, а не
// уровня: начальные — settings.ini ([Audio] Volume) и -mute, в базу не пишутся; «Clear solo» снимает Solo частей звука
// уровня (AudioAudition). Команды: audio record — смесь в WAV для проверки без наушников, audio stats
// Прослушивание (AudioAudition) — с начала кадра, в update: Solo частей за прошлый кадр — в силе
class AudioDevice
{
public:
	AudioDevice();
	// enabled — [Audio] Enabled в settings.ini (false — звука нет совсем); muted — -mute: вывод молчит, а микшер и запись
	// работают (проверки не шумят). false — вывода нет (нет устройства): голоса можно заводить, их никто не считает
	bool initialize( bool enabled, float volume, bool muted );
	void shutdown();
	bool active() const { return m_output.active(); }
	AudioMixer& mixer() { return m_mixer; }
	// Раз в кадр: правки окна — в громкость мастер-голоса, сводка — в «Statistic»
	void update( FrameStats& stats );
	PropertyContainer* properties() { return &m_properties; }
	// Режим вьюпорта «Sound sources» (флажок окна; View → Debug views): метки звука уровня
	bool showSources() const { return m_properties["Show sources"].data<bool>(); }
	void registerCommands( ConsoleCommands& console );

	// Стерео 16 бит с частотой микшера: samples — каналы через один, −1…1
	static bool writeWav( const std::string& path, const std::vector<float>& samples );

private:
	AudioMixer m_mixer;		// раньше вывода: вывод останавливает нить до микшера
	AudioOutput m_output;
	PropertyContainer m_properties;
	uint64_t m_appliedRevision = ~0ull;
};

}
