#pragma once

// Состояния растеризатора, глубины и блендинга — часть объекта пайплайна (GpuPipeline.h). Код объектов задаёт их через
// DMD3D::setState / ScopedRenderState, а в контекст они попадают вместе с шейдерами при DMD3D::setPipeline
enum class RasterState
{
	solid, frontCulling, noCulling, wireframe,
	// Для зеркальных мешей (отрицательный определитель мировой матрицы): лицевые грани обходятся против часовой
	// стрелки — меняются и отсечение, и признак SV_IsFrontFace, так что нормали двусторонних не разворачиваются зря
	solidMirrored, noCullingMirrored,
	// Глубина каскадов теней: без отсечения граней, наклонное смещение глубины, без отсечения по глубине (pancaking)
	csmShadowDepth
};

enum class DepthState
{
	enabled,			// проверка и запись
	readOnly,			// только проверка: полупрозрачные
	readOnlyNearOrEqual,	// проверка «ближе или равно» без записи: фон на дальней плоскости (небо) — где ничего нет
	readOnlyEqual,		// проверка «равно» без записи: непрозрачные после depth prepass — освещается только ближайшая поверхность
	disabled
};

enum class BlendState
{
	opaque,		// без смешивания
	alpha,		// полупрозрачные: src · a + dst · (1 − a)
	additive	// сложение (ONE, ONE): уровни bloom, накопление света
};

// Смещение глубины растеризатора — у глубины каскадов теней (Shadow Slope Bias солнца, DMLight::ShadowSettings): наклонное
// — пропорционально наклону поверхности к свету, предел — в единицах глубины; постоянного нет (у D32_FLOAT оно зависит
// от порядка числа). Знак — «от света»: глубина обратная, поэтому в растеризатор D3D12 значения идут с минусом
struct DepthBias
{
	float slopeScaled = 0.0f;
	float clamp = 0.0f;

	bool operator==( const DepthBias& other ) const { return slopeScaled == other.slopeScaled && clamp == other.clamp; }
	bool operator!=( const DepthBias& other ) const { return !( *this == other ); }
};

struct RenderState
{
	RasterState raster = RasterState::solid;
	DepthState depth = DepthState::enabled;
	BlendState blend = BlendState::opaque;
	DepthBias depthBias;	// часть пайплайна: состояние с другим смещением — другой PSO

	bool operator==( const RenderState& other ) const
	{
		return raster == other.raster && depth == other.depth && blend == other.blend && depthBias == other.depthBias;
	}
	bool operator!=( const RenderState& other ) const { return !( *this == other ); }
};
