#pragma once

#include <cstdint>
#include <vector>

namespace GS
{

// Круг кроны на карте полога: центр в мире (x, z), радиус, м; лиственное ли дерево (под ним подстилка из листьев, а не
// хвоя — Models.broadleaf)
struct CanopyStamp
{
	float x = 0.0f;
	float z = 0.0f;
	float radius = 0.0f;
	bool broadleaf = false;
};

// Карта полога — где земля под кронами деревьев (как разность с отпечатками деревьев в PCG UE; docs/scatter.md, «Полог»):
// по кругам крон леса (постоянные слои расстановки с ScatterLayers.canopy) и высоких моделей уровня. Покрытие 0…1: полное
// до innerShare радиуса, к краю кроны — плавно до нуля; перекрытие — наибольшее. Ядро без устройства: карту на GPU держит
// конвейер рельефа (TerrainPipeline::setCanopy), читают слои расстановки (ScatterLayers.under_canopy, Shaders/canopy.sh)
struct TerrainCanopy
{
	static constexpr float texel = 0.5f;		// м на тексель
	static constexpr uint32_t maxSize = 4096;
	static constexpr float innerShare = 0.6f;	// доля радиуса кроны с полным пологом

	// Сторона карты на мир worldSize, м: по texel, не больше maxSize
	static uint32_t mapSize( float worldSize );
	// Карта size² байт (0…255), строка 0 — дальний край по z, как у карты высот (v = 1 − z / worldSize)
	static std::vector<uint8_t> build( const std::vector<CanopyStamp>& stamps, uint32_t size, float worldSize );
	// Две карты подряд в тексель (R8G8): R — все кроны (build), G — только лиственные; доля лиственного полога — G / R
	static std::vector<uint8_t> buildWithBroadleaf( const std::vector<CanopyStamp>& stamps, uint32_t size, float worldSize );
	// Покрытие одного круга на расстоянии distance от центра, 0…1
	static float cover( float distance, float radius );
	// Покрытие в точке мира по карте (тексель, в который она попала); вне карты — 0
	static float sample( const std::vector<uint8_t>& map, uint32_t size, float worldSize, float x, float z );
};

}
