#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>
#include "SceneObject.h"
#include "Level\LevelSettings.h"
#include "Engine\Audio\AudioClip.h"
#include "Engine\Audio\AudioMixer.h"

class DMLightDriver;

namespace GS
{

class TerrainPipeline;

// Певцы уровня — как Soundscape в UE (docs/audio.md, «Певцы»): слои SoundscapeLayers, у слоя — запись (строка Sounds, её фразы
// собрал Tools/pack_sound.py) и правила. Певец садится вокруг слушателя — в полосе расстояний слоя, на высоте над землёй, там,
// где есть его место по маскам «habitat» (до 16 попыток), — поёт фразы записи с паузами весь свой «bout», потом улетает, и
// появляется другой. Певцов сразу — «singers» в свои часы суток (SunPosition), у краёв часов — меньше. Голос — на фразу
// (ClipGenerator), у певца своя высота; громкость — по расстоянию от 10 м, поглощение воздухом и заглушение гребнем
// (SoundPlace.h), как у ручьёв. Окно «Soundscape» — подокно на слой; флажок видимости в Outliner — тихо
class Soundscape : public SceneObject
{
public:
	using Layer = SoundscapeLayerSettings;

	Soundscape();
	~Soundscape() override;
	// sounds — записи слоёв (строки Sounds); нет файла — строка в лог, слой молчит; lights — время суток (SunPosition)
	bool initialize( const std::vector<Layer>& layers, const std::vector<SoundRecord>& sounds, AudioMixer& mixer,
					 const TerrainPipeline* terrain, DMLightDriver& lights );
	bool initialized() const { return m_mixer != nullptr; }

	void update( const FrameContext& frame ) override;
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	// Слои со значениями окна — для «Save level» (строки SoundscapeLayers)
	std::vector<Layer> layerSettings() const;

private:
	struct Singer
	{
		DirectX::XMFLOAT3 position;
		float pitch = 1.0f;
		float boutLeft = 0.0f;		// с: столько ещё поёт с этого места
		float nextPhrase = 0.0f;	// с: пауза до следующей фразы
		AudioMixer::VoiceId voice = 0;	// звучащая фраза; 0 — молчит
		float voiceLeft = 0.0f;		// с: до конца фразы
	};
	struct LayerState
	{
		Layer settings;
		PropertyContainer properties;
		std::shared_ptr<const AudioClip> clip;
		std::vector<Singer> singers;
		float spawnDelay = 0.0f;	// с: до следующего певца (прилетают не все сразу)
	};

	Layer current( const LayerState& layer ) const;
	// Активность слоя 0…1 в час суток hour: в часах слоя — 1, у краёв — плавно за 45 минут
	static float activity( const DirectX::XMFLOAT2& hours, float hour );
	// Место нового певца; false — места по маскам рядом нет
	bool placeSinger( const Layer& settings, const DirectX::XMFLOAT3& listener, Singer& singer );
	float uniform( float from, float to );

	PropertyContainer m_properties;
	std::vector<std::unique_ptr<LayerState>> m_layers;
	AudioMixer* m_mixer = nullptr;
	const TerrainPipeline* m_terrain = nullptr;
	DMLightDriver* m_lights = nullptr;
	std::mt19937 m_random{ 0x5EEDu };
};

}
