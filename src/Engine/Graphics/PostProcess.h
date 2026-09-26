#pragma once

#include <string>
#include "Shaders\DMShader.h"
#include "Properties\PropertyContainer.h"

namespace GS
{

// Постобработка кадра: экспозиция и тонмаппинг HDR-буфера сцены в задний буфер (Shaders/fullscreen.vs +
// Shaders/tonemap.ps), как Exposure и Tonemapper в Post Process Volume UE. Настройки — секция [PostProcess]
// в Scene\Lights.ini (ExposureCompensation в EV, Tonemapper: None / ACES / AgX), меняются в GUI («Post process»)
class PostProcess
{
public:
	enum class Tonemapper : int32_t
	{
		none = 0,	// только ограничение 0…1 — для отладки
		aces = 1,	// ACES (подгонка RRT + ODT, S. Hill) — основа Filmic tonemapper UE
		agx = 2		// AgX — стандартное отображение Blender 4+, мягче уводит яркие цвета в белый
	};

	bool initialize( const std::string& settingsFile );
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

	DMShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	PropertyContainer m_properties;
};

}
