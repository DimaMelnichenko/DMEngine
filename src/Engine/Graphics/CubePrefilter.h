#pragma once

#include "FullscreenShader.h"
#include "ComputeShader.h"
#include "D3D\CubeTarget.h"

namespace GS
{

// Префильтр кубической карты окружения для отражений (split-sum, Karis 2013): мип m результата — окружение, свёрнутое с
// распределением GGX шероховатости m / (мипов − 1) (Shaders/cube_prefilter.ps, выборка по важности с мипами источника —
// filtered importance sampling). Общий у освещения неба (SkyLight) и карт отражений места (ReflectionCaptures)
class CubePrefilter
{
public:
	// resultFormat — формат результата (цели граней)
	bool initialize( DXGI_FORMAT resultFormat );
	// Мипы 1… источника из мипа 0: среднее 2 × 2 в compute (Shaders/cube_downsample.cs), проход на мип; источник создан с
	// computeMips (CubeTarget::create). До prefilterFace: выборка по важности берёт мипы источника
	void buildMips( const CubeTarget& source );
	// Грань face результата, все его мипы; outputScale — множитель результата
	void prefilterFace( const CubeTarget& source, const CubeTarget& result, int32_t face, float outputScale = 1.0f );

private:
	FullscreenShader m_shader;
	ComputeShader m_mipShader;
	Buffer m_constantBuffer;
};

}
