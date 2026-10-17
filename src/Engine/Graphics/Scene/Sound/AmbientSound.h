#pragma once

#include <string>
#include <vector>
#include "SceneObject.h"
#include "ConstantBuffers.h"
#include "Level\LevelSettings.h"
#include "Engine\Audio\AudioMixer.h"

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
//   (AudioMixer, spatialize), волна порыва проходит по лесу слышимо.
// Ветра нет (сила 0, -nowind) — тихо; флажок видимости в Outliner — тоже
class AmbientSound : public SceneObject
{
public:
	using Settings = AmbientSoundSettings;
	static constexpr uint32_t crownSides = 4;

	AmbientSound();
	~AmbientSound() override;
	// terrain — маски травы и леса, поле ветра, высота земли; без террейна — только ветер
	bool initialize( const Settings& settings, AudioMixer& mixer, Wind& wind, const TerrainPipeline* terrain );
	bool initialized() const { return m_mixer != nullptr; }

	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	// Значения окна — для «Save level» (строка AmbientSound)
	Settings settings() const;

private:
	// Ветер у земли в точке: скорость (ветер уровня × поле по рельефу × порыв), м/с, и порывистость поля 0…1
	struct WindSample
	{
		float speed = 0.0f;
		float turbulence = 0.0f;
	};
	WindSample windAt( const WindParameters& wind, float x, float z, float time ) const;
	// Сумма масок в точке мира, не больше 1; вне карты — 0
	float maskAt( const std::vector<const std::vector<float>*>& masks, float x, float z ) const;
	std::vector<const std::vector<float>*> masks( const char* const* names, size_t count ) const;

	PropertyContainer m_properties;
	PropertyContainer m_windProperties;
	PropertyContainer m_grassProperties;
	PropertyContainer m_crownProperties;
	Settings m_settings;
	AudioMixer* m_mixer = nullptr;
	Wind* m_wind = nullptr;
	const TerrainPipeline* m_terrain = nullptr;
	AudioMixer::VoiceId m_windVoices[2] = {};		// слева, справа
	AudioMixer::VoiceId m_grassVoices[2] = {};
	AudioMixer::VoiceId m_crownVoices[crownSides] = {};	// север, восток, юг, запад
};

}
