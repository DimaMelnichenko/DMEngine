#pragma once

#include "DirectX.h"
#include "D3D\GpuResources.h"
#include "ComputeShader.h"
#include "Terrain\TerrainHeightSource.h"
#include "Shaders\terrain_sun_shadow_constants.h"

namespace GS
{

// Тень рельефа от источника теней (солнце, ночью — луна) дальше каскадов — часть дальних теней (ShadowCascades, как
// Far Shadow в UE, но для рельефа — по карте высот, а не каскадом): для каждой точки области — высота края тени над ней
// и расстояние до заслона (ширина полутени). Считается в compute по итоговой карте высот и горам вокруг неё
// (Shaders/terrain_sun_shadow.cs), только когда источник сдвинулся или рельеф сменился (перестройка русел); читают
// Shaders/shadows.sh (terrainSunShadow, mountainShadow): поверхности, деревья и туман — точка ниже края в тени. Область —
// карта уровня (тексель вдвое крупнее карты высот: тень вдали мягкая; заслоны за её краем — горы — отдельной картой:
// их тени нет в каскадах) или всё кольцо гор (FarTerrain, тексель — «Shadow resolution» гор)
class TerrainSunShadow
{
public:
	// mountains — область: всё кольцо гор; иначе — карта уровня
	bool initialize( bool mountains );
	// Пересчёт, если источник сдвинулся больше чем на changeAngle или рельеф сменился; без рельефа области карты нет
	void update( const TerrainHeight& terrain, const FarTerrainHeight& farTerrain, const DirectX::XMFLOAT3& toLight );
	bool valid() const { return m_valid; }
	const ShaderView& shaderView() const { return m_shaderView; }
	// Тень от заслонов за краем карты (горы): RG — запас земли над краем тени, м, и расстояние до заслона, м; есть, если
	// при последнем пересчёте были горы (только у карты уровня)
	const ShaderView& externalView() const { return m_externalView; }
	bool hasExternal() const { return m_valid && m_external; }
	// Область: угол, м, и 1 / сторона, м⁻¹ (UV точки мира: ( x − угол x ) / сторона, 1 − ( z − угол z ) / сторона)
	DirectX::XMFLOAT2 origin() const { return m_origin; }
	float inverseExtent() const { return m_valid ? 1.0f / m_extent : 0.0f; }
	// Число пересчётов с запуска — для проверки, что карта не считается каждый кадр
	uint32_t updates() const { return m_updates; }

private:

	// Сдвиг источника, после которого карта считается заново: край тени горы в 500 м смещается на ~0,4 м — меньше текселя
	static constexpr float changeAngle = 0.05f;	// градусы

	bool createTextures( uint32_t size );

	bool m_mountains = false;
	ComputeShader m_shader;
	Buffer m_constants;
	Texture m_texture;
	ShaderView m_shaderView;
	StorageView m_storageView;
	Texture m_externalTexture;
	ShaderView m_externalView;
	StorageView m_externalStorage;
	uint32_t m_size = 0;
	DirectX::XMFLOAT2 m_origin = DirectX::XMFLOAT2( 0.0f, 0.0f );
	float m_extent = 0.0f;
	bool m_valid = false;
	bool m_external = false;
	// Для чего посчитана карта: направление источника и рельеф (номер итоговой карты, множитель и сдвиг высоты, горы)
	DirectX::XMFLOAT3 m_direction = { 0.0f, 0.0f, 0.0f };
	uint32_t m_revision = 0;
	float m_heightMultiplier = 0.0f;
	float m_heightOffset = 0.0f;
	const ShaderView* m_farMap = nullptr;
	uint32_t m_farRevision = 0;
	uint32_t m_updates = 0;
};

}
