#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <DirectXMath.h>

class PropertyContainer;

namespace GS
{

// Метки звука уровня для вьюпорта — режим «Sound sources» (View → Debug views, флажок «Show sources» окна «Audio»; docs/audio.md,
// «Окна редактора»). Объекты звука (AmbientSound, Soundscape) отдают их по своему состоянию кадра, вьюпорт рисует, не зная
// объектов, как значки эмиттеров частиц

// Позиционный источник — значок: певец, место воды. Клик — окно properties в Details
struct SoundMarker
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 color;			// цвет слоя или части
	std::string label;					// подпись у значка; пусто — без подписи (места воды — их много)
	bool sounding = false;				// звучит сейчас: фраза певца, голос места воды
	float size = 1.0f;					// доля размера значка
	PropertyContainer* properties = nullptr;
};

// Область части или слоя («Show area»): кольцо вокруг слушателя, маски места — подсветкой земли (Scene, SoundAreaOverlay),
// точки места (берег озера)
struct SoundArea
{
	DirectX::XMFLOAT3 color;
	DirectX::XMFLOAT2 ring = DirectX::XMFLOAT2( 0.0f, 0.0f );	// от — до вокруг слушателя, м; до 0 — кольца нет
	std::vector<std::string> masks;		// маски рельефа по именам (TerrainPipeline::maskValues)
	const std::vector<DirectX::XMFLOAT3>* points = nullptr;	// точки места; nullptr — нет
	std::string label;
	PropertyContainer* properties = nullptr;
};

// Метки кадра; слушатель — камера вида: кольца областей — вокруг неё
struct SoundDebug
{
	std::vector<SoundMarker> markers;
	std::vector<SoundArea> areas;
};

// Цвет метки по номеру (слой певцов): оттенки через золотой угол — соседние номера различимы, яркость 1
inline DirectX::XMFLOAT3 soundColor( uint32_t index )
{
	const float hue = std::fmod( 0.08f + index * 0.618034f, 1.0f ) * 6.0f;
	const float saturation = 0.65f;
	const float f = hue - std::floor( hue );
	const float p = 1.0f - saturation;
	const float q = 1.0f - saturation * f;
	const float t = 1.0f - saturation * ( 1.0f - f );
	switch( static_cast<int>( hue ) % 6 )
	{
	case 0:
		return DirectX::XMFLOAT3( 1.0f, t, p );
	case 1:
		return DirectX::XMFLOAT3( q, 1.0f, p );
	case 2:
		return DirectX::XMFLOAT3( p, 1.0f, t );
	case 3:
		return DirectX::XMFLOAT3( p, q, 1.0f );
	case 4:
		return DirectX::XMFLOAT3( t, p, 1.0f );
	default:
		return DirectX::XMFLOAT3( 1.0f, p, q );
	}
}

}
