#pragma once

#include <string>
#include <vector>
#include "SceneObject.h"
#include "Wind\WindParameters.h"
#include "Level\LevelSettings.h"
#include "Engine\Audio\AudioMixer.h"
#include "SoundDebug.h"

namespace GS
{

class Wind;
class TerrainPipeline;

// Звуки окружения уровня — как Ambient Sound и Soundscape в UE, но синтезом (Audio/AudioSynth.h, docs/audio.md): строка
// AmbientSound уровня (Levels.ambient_sound), окно «Ambient sound». Мешей нет: каждый кадр по камере — цели голосов микшера:
// - ветер — два голоса, слева и справа от камеры (разный шум — звук широкий): скорость ветра уровня × поле ветра по рельефу ×
//   порыв в точке (Wind::gust — волны те же, что на траве), выше над землёй — сильнее;
// - трава — два голоса так же: маски травы вокруг камеры × ветер, высоко над землёй не слышна;
// - кроны леса — голос на сторону света: маски леса в секторе стороны × ветер в нём, звук — с той стороны, где лес
//   (AudioMixer, spatialize), волна порыва проходит по лесу слышимо;
// - ручьи — места воды: клетки ручьёв растра статичной воды (TerrainHydrology::Result::staticWater) блоками streamBlock,
//   громкость — мощность, которую теряет течение в блоке (расход × перепад уровня: перекат громче плёса), по самому шумному
//   месту уровня; слышны streamVoices самых громких у камеры (голос — генератор пузырьков BrookGenerator, остальные места
//   молчат, как виртуальные голоса UE), с затуханием по расстоянию, поглощением воздухом и заглушением гребнем между
//   камерой и водой (луч по карте высот). Блоки — заново, когда сменилась итоговая карта (перестройка русел).
// Ветра нет (сила 0, -nowind) — ветер, трава и кроны молчат, ручьи — нет; флажок видимости в Outliner — тихо всё.
// У части (подокна) — «Enabled» (сохраняется: выключенная голосов не держит) и прослушивание «Mute» / «Solo» (AudioAudition)
class AmbientSound : public SceneObject
{
public:
	using Settings = AmbientSoundSettings;
	static constexpr uint32_t crownSides = 4;
	// Место воды ручья — квадрат со стороной столько, м
	static constexpr float streamBlock = 8.0f;

	AmbientSound();
	~AmbientSound() override;
	// terrain — маски травы и леса, поле ветра, высота земли; без террейна — только ветер
	bool initialize( const Settings& settings, AudioMixer& mixer, Wind& wind, const TerrainPipeline* terrain );
	bool initialized() const { return m_mixer != nullptr; }

	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	bool audio() const override { return true; }
	// Значения окна — для «Save level» (строка AmbientSound)
	Settings settings() const;
	// Метки режима «Sound sources»: места воды ближе «Distance» (звучащие — ярче), области частей с «Show area»
	void soundDebug( SoundDebug& out );

private:
	// Ветер у земли в точке: скорость (ветер уровня × поле по рельефу × порыв), м/с, и порывистость поля 0…1
	struct WindSample
	{
		float speed = 0.0f;
		float turbulence = 0.0f;
	};
	WindSample windAt( const WindParameters& wind, float x, float z, float time ) const;

	// Место воды ручья: середина над водой, мощность (доля самого шумного места уровня), перекат 0…1 (уклон воды)
	struct StreamBlock
	{
		DirectX::XMFLOAT3 position;
		float power = 0.0f;
		float rapids = 0.0f;
	};
	// Места воды по растру ручьёв конвейера; нет воды — пусто
	void buildStreamBlocks();
	// Слышные места воды — голоса, их цели; volume — общая громкость с прослушиванием части
	void updateStreams( const Settings& settings, const AudioListener& listener, float volume );
	// Голоса ветра, травы и крон — по «Enabled» частей: включена — заведены, выключена — убраны
	void syncVoices( const Settings& settings );

	PropertyContainer m_properties;
	PropertyContainer m_windProperties;
	PropertyContainer m_grassProperties;
	PropertyContainer m_crownProperties;
	PropertyContainer m_streamProperties;
	Settings m_settings;
	AudioMixer* m_mixer = nullptr;
	Wind* m_wind = nullptr;
	const TerrainPipeline* m_terrain = nullptr;
	// 0 — голоса нет (часть выключена)
	AudioMixer::VoiceId m_windVoices[2] = {};		// слева, справа
	AudioMixer::VoiceId m_grassVoices[2] = {};
	AudioMixer::VoiceId m_crownVoices[crownSides] = {};	// север, восток, юг, запад
	uint32_t m_clearSolo = 0;		// номер «Clear solo», по который сняты Solo частей
	DirectX::XMFLOAT3 m_listener = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// слушатель прошлого update
	std::vector<StreamBlock> m_streamBlocks;
	uint32_t m_streamRevision = ~0u;	// номер итоговой карты, по которой собраны места воды
	// Слышные места воды: голос на место (уходит — затихает, новое — заводит свой)
	struct StreamVoice
	{
		size_t block = 0;
		AudioMixer::VoiceId voice = 0;
	};
	std::vector<StreamVoice> m_streamVoices;
	uint32_t m_nextSeed = 0;
};

}
