#pragma once

#include <string>
#include "Shaders\FullscreenShader.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

// Постобработка кадра: экспозиция и тонмаппинг HDR-буфера сцены в задний буфер (Shaders/fullscreen.vs +
// Shaders/tonemap.ps), как Exposure и Tonemapper в Post Process Volume UE. Настройки — строка таблицы
// PostProcessSettings, на которую ссылается уровень (Levels.post_process), меняются в GUI («Post process»)
class PostProcess
{
public:
	enum class Tonemapper : int32_t
	{
		none = 0,	// только ограничение 0…1 — для отладки
		aces = 1,	// ACES (подгонка RRT + ODT, S. Hill) — основа Filmic tonemapper UE
		agx = 2		// AgX — стандартное отображение Blender 4+, мягче уводит яркие цвета в белый
	};

	// Строка PostProcessSettings; без неё — 0 EV и AgX
	struct Settings
	{
		float exposureCompensation = 0.0f;	// EV: +1 — вдвое ярче
		Tonemapper tonemapper = Tonemapper::agx;
	};
	// Имя тонмаппинга в базе: None, ACES, AgX (другое — AgX)
	static Tonemapper tonemapperFromName( const std::string& name );
	static const char* tonemapperName( Tonemapper tonemapper );

	bool initialize( const Settings& settings );
	// Текущие значения из GUI — для сохранения уровня
	Settings settings();
	// Рисует тонмаппинг сцены в задний буфер и оставляет его привязанным для GUI
	void render();
	PropertyContainer* properties();

private:
	// Константный буфер PS b2, раскладка как у PostProcessBuffer в Shaders/tonemap.ps
	struct alignas( 16 ) Parameters
	{
		float exposure;		// множитель 2^EV
		int32_t tonemapper;
		float padding[2];
	};

	FullscreenShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	PropertyContainer m_properties;
};

}
