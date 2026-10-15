#pragma once

#include "DirectX.h"
#include "D3D\GpuResources.h"
#include "DMComputeShader.h"
#include "Terrain\TerrainHeightSource.h"

namespace GS
{

// Тень рельефа от источника теней (солнце, ночью — луна) дальше каскадов — часть дальних теней (ShadowCascades, как
// Far Shadow в UE, но для рельефа — по карте высот, а не каскадом): для каждой точки карты — высота края тени над ней
// и расстояние до заслона (ширина полутени). Считается в compute по итоговой карте высот (Shaders/terrain_sun_shadow.cs),
// только когда источник сдвинулся или карта сменилась (перестройка русел); читают Shaders/shadows.sh (terrainSunShadow):
// поверхности, деревья и туман — точка ниже края в тени. Тексель — вдвое крупнее карты высот: тень вдали мягкая
class TerrainSunShadow
{
public:
	bool initialize();
	// Пересчёт, если источник сдвинулся больше чем на changeAngle или рельеф сменился; без рельефа карты нет
	void update( const TerrainHeight& terrain, const DirectX::XMFLOAT3& toLight );
	bool valid() const { return m_valid; }
	const ShaderView& shaderView() const { return m_shaderView; }
	// 1 / сторона карты, м⁻¹ (UV точки мира: x / сторона, 1 − z / сторона); 0 — карты нет
	float inverseWorldSize() const { return m_valid ? 1.0f / m_worldSize : 0.0f; }
	// Число пересчётов с запуска — для проверки, что карта не считается каждый кадр
	uint32_t updates() const { return m_updates; }

private:
	// cbuffer TerrainSunShadowBuffer в Shaders/terrain_sun_shadow.cs
	struct alignas( 16 ) Parameters
	{
		DirectX::XMFLOAT2 sunAzimuth;
		float sunSlope;
		float texelSize;
		float heightMultiplier;
		float heightOffset;
		float worldSize;
		float maxHeight;
		float mapTexel;
		float startDistance;
		uint32_t size;
		float padding;
	};
	static_assert( sizeof( Parameters ) == 48, "TerrainSunShadowBuffer layout" );

	// Сдвиг источника, после которого карта считается заново: край тени горы в 500 м смещается на ~0,4 м — меньше текселя
	static constexpr float changeAngle = 0.05f;	// градусы

	bool createTexture( uint32_t size );

	DMComputeShader m_shader;
	Buffer m_constants;
	Texture m_texture;
	ShaderView m_shaderView;
	StorageView m_storageView;
	uint32_t m_size = 0;
	float m_worldSize = 0.0f;
	bool m_valid = false;
	// Для чего посчитана карта: направление источника и рельеф (номер итоговой карты, множитель и сдвиг высоты)
	DirectX::XMFLOAT3 m_direction = { 0.0f, 0.0f, 0.0f };
	uint32_t m_revision = 0;
	float m_heightMultiplier = 0.0f;
	float m_heightOffset = 0.0f;
	uint32_t m_updates = 0;
};

}
