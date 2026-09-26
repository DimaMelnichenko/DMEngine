#pragma once

#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainMaterial.h"
#include "TerrainHeightSource.h"
#include "Shaders\DMShader.h"
#include "D3D\DMStructuredBuffer.h"

namespace GS
{

// Террейн CDLOD (Continuous Distance-Dependent Level of Detail, F. Strugar, 2009).
// Квадродерево над картой высот: корень покрывает весь террейн, лист — gridDim текселей.
// У каждого уровня свой диапазон расстояний, вдвое больше предыдущего; узел, до которого камера ближе
// диапазона следующего уровня, делится на четыре. Выбранные узлы рисуются одним инстансным вызовом общего
// патча, а морфинг в вершинном шейдере (Shaders\cdlod.vs) сглаживает переходы между уровнями.
// Материал — TerrainMaterial (Shaders\terrain.ps), настройки — строка таблицы Terrain в base.db3, на которую
// ссылается уровень, слои материала — таблица TerrainLayers
class CDLODTerrain : public SceneObject, public TerrainHeightSource
{
public:
	CDLODTerrain();

	// terrainId — строка таблицы Terrain
	bool initialize( uint32_t terrainId );
	// Карта высот и её масштаб: по ним стоят расстановка травы и декора и частицы
	TerrainHeight terrainHeight() const override;

	void update( const FrameContext& frame ) override;
	void render( const FrameContext& frame ) override;
	PropertyContainer* properties() override;

private:
	static constexpr uint32_t gridDim = 32;			// квадов в стороне узла
	static constexpr uint32_t patchDim = gridDim / 2;	// квадов в стороне патча: узел рисуется четырьмя патчами-четвертями
	static constexpr uint32_t maxLevels = 16;
	static constexpr uint32_t maxPatches = 8192;
	static constexpr float morphStartRatio = 0.66f;	// с какой доли диапазона уровня начинается морфинг

	struct PatchInstance
	{
		XMFLOAT2 origin;
		float size;
		float level;
	};

	struct alignas( 16 ) Parameters
	{
		float worldSize;
		float heightMultipler;
		float heightOffset;
		float gridDim;
		XMFLOAT4 morphConsts[maxLevels];
		XMFLOAT4 layerScale;
		float texelSize;
		float triplanarSharpness;
		float heightBlendDepth;
		float padding;
	};

	struct NodeBox
	{
		XMFLOAT3 min;
		XMFLOAT3 max;
	};

	bool loadSettings( uint32_t terrainId, float& heightMultipler, std::string& splatMap );
	bool createShader();
	// Копия карты высот с мипами для вершинного шейдера и минимум / максимум высоты каждого узла по мипам его уровня
	bool buildHeightBounds();
	void calcRanges();
	NodeBox nodeBox( uint32_t level, uint32_t x, uint32_t z ) const;
	float nodeSize( uint32_t level ) const;
	// false — узел дальше диапазона своего уровня, и его площадь рисует родитель
	bool selectNode( const FrameContext& frame, uint32_t level, uint32_t x, uint32_t z );
	void addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter );

private:
	std::string m_heightMapName;	// карта высот в хранилище текстур
	float m_worldSize = 0.0f;
	float m_texelSize = 1.0f;
	float m_heightOffset = 0.0f;
	float m_heightMultipler = 1.0f;
	uint32_t m_levelCount = 0;
	std::vector<uint32_t> m_nodesPerSide;
	std::vector<std::vector<XMFLOAT2>> m_heightBounds;	// по уровням: нормированные min / max высоты узлов
	com_unique_ptr<ID3D11ShaderResourceView> m_heightMap;	// копия t_heightmap в R32_FLOAT с полной цепочкой мипов
	float m_ranges[maxLevels] = {};
	XMFLOAT4 m_morphConsts[maxLevels] = {};

	std::vector<PatchInstance> m_patches;
	GridMesh m_patch;
	DMShader m_shader;
	TerrainMaterial m_material;
	DMStructuredBuffer m_patchBuffer;
	com_unique_ptr<ID3D11Buffer> m_constantBuffer;
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
