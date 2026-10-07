////////////////////////////////////////////////////////////////////////////////
// Сетка объёмного тумана — общее для C++ (VolumetricFog) и HLSL (Shaders/volumetric_fog.cs, Shaders/height_fog.sh)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_FOG_CONSTANTS_H
#define DM_FOG_CONSTANTS_H

#define VOLUMETRIC_FOG_TILE			16		// пикселей кадра на ячейку сетки по X и Y (GridPixelSize в UE)
#define VOLUMETRIC_FOG_DEPTH		64		// слоёв по глубине взгляда (GridSizeZ в UE)
#define VOLUMETRIC_FOG_DISTRIBUTION	32.0f	// слой s — на глубине (2^(s / D) − O) / B: у камеры гуще (DepthDistributionScale в UE)

#endif
