////////////////////////////////////////////////////////////////////////////////
// Константы дальних гор вокруг карты (FarTerrain, Shaders/far_terrain.sh). Параметры карты, таблица дальностей и слои — общие
// с рельефом карты (CDLODBuffer, cdlod_constants.h). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_FAR_TERRAIN_CONSTANTS_H
#define DM_FAR_TERRAIN_CONSTANTS_H

#include "shader_constants.h"

// Уклоны — косинусы угла нормали с вертикалью: от — меньше наклон (больше косинус), до — круче
#define DM_FIELD_PREFIX g_
DM_CONSTANTS( FarTerrainBuffer, b4 )
{
	DM_FIELD( float2, farOrigin );			// угол карты гор с наименьшими x, z, м
	DM_FIELD( float, farExtent );			// её сторона, м
	DM_FIELD( float, farMipBias );			// мип карты гор у уровня L — L − farMipBias (её тексель — квад этого уровня)
	DM_FIELD( float, farBlendDistance );	// от края карты: высота карты → высота гор, м
	DM_FIELD( float, farMaterialBlend );	// от края карты: слои карты → правила, м
	DM_FIELD( float2, rockSlope );			// скала по уклону
	DM_FIELD( float2, screeSlope );			// осыпи по уклону…
	DM_FIELD( float2, screeHeight );		// …выше этой высоты, м
	DM_FIELD( float2, meadowHeight );		// трава долины → сухой луг, м
	DM_FIELD( float2, snowHeight );			// граница снега на склоне к югу, м
	DM_FIELD( float2, snowSlope );			// круче — снег не держится
	DM_FIELD( float, snowNorth );			// на склоне к северу снег ниже, м
	DM_FIELD( float, snowNoise );			// граница снега гуляет пятнами, м
	DM_FIELD( float2, forestLine );			// лес ниже, к верху полосы сходит на нет, м
	DM_FIELD( float2, forestSlope );		// круче — леса нет
	DM_FIELD( float3, forestColor );		// цвет леса издали, линейный
	DM_FIELD( int, grassWetLayer );			// слои террейна по ролям (TerrainLayers.role): −1 — нет
	DM_FIELD( int, grassDryLayer );
	DM_FIELD( int, talusLayer );
	DM_FIELD( int, rockLayer );
	DM_FIELD( int, snowLayer );
	DM_FIELD( float2, macroDistance );		// от x до y м от камеры текстуры слоёв переходят в третий масштаб…
	DM_FIELD( float, macroTextureScale );	// …во столько раз крупнее дальнего
	DM_FIELD( float, glacierFirnLine );		// ледник наших дней (g_farIce): выше — фирн (снег), ниже — голый лёд, м
	DM_FIELD( float3, glacierIceTint );		// голый лёд — альбедо слоя снега × это
	DM_FIELD( float, farPadding );
	DM_FIELD( float2, glacierCover );		// толщина льда, м: от x до y — ледник набирает полную силу
	DM_FIELD( float2, moraineCover );		// доля линий обломков: от x до y — морена (слой осыпей) набирает силу
	DM_FIELD( float3, crevasseColor );		// альбедо в трещине, линейное
	DM_FIELD( float, crevasseSpacing );		// шаг трещин вдоль течения льда, м
	DM_FIELD( float2, crevasseSlope );		// трещины по уклону поверхности льда
	// Кольцо деревьев на горах (слой расстановки far_forest): x — дальняя граница, м (0 — деревьев нет), y — её исчезание, м.
	// Тон леса — там, где деревьев уже нет
	DM_FIELD( float2, forestRing );
	// Лес по рельефу (far_forest_density.cs): водосбор долины, log10 м² (от x до y — дно долины и нижние склоны гуще), плотность
	// вдали от долин и пологого, доля полян, мип льда ледников с текселем около полосы без леса у них
	DM_FIELD( float2, forestValleyArea );
	DM_FIELD( float, forestUpland );
	DM_FIELD( float, forestGlades );
	DM_FIELD( float, forestGlacierMip );
	// Тени леса дальше дистанции теней (far_terrain.ps): доля земли под кронами при полной плотности (0 — без теней) и высота
	// деревьев, м
	DM_FIELD( float, forestShadow );
	DM_FIELD( float, forestHeight );
	DM_FIELD( float, forestPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
