////////////////////////////////////////////////////////////////////////////////
// Плавная смена LOD дизерингом (Dithered LOD Transition в UE): в полосе перехода экземпляр рисуется обоими LOD, каждый —
// своей долей пикселей экрана, и маски дополняют друг друга. Вариант материала с define LOD_DITHER
////////////////////////////////////////////////////////////////////////////////

#ifndef LOD_DITHER_SH
#define LOD_DITHER_SH

// Порог пикселя 0…1 — interleaved gradient noise (Jimenez 2014): у соседних пикселей пороги сильно различаются, поэтому
// без TAA доля пикселей даёт мелкую рябь, а не пятна
float lodDitherThreshold( float2 pixel )
{
	return frac( 52.9829189f * frac( dot( floor( pixel ), float2( 0.06711056f, 0.00583715f ) ) ) );
}

// Как ClipLODTransition в UE. factor в (0; 1) — уходящий LOD: остаются пиксели с порогом выше factor; в (−1; 0) —
// приходящий, factor = t − 1 для той же доли перехода t: остаются пиксели с порогом не выше t. 0 — без отсечения
void clipLodTransition( float2 pixel, float factor )
{
	const float threshold = lodDitherThreshold( pixel );
	clip( factor < 0.0f ? factor + 1.0f - threshold : threshold - factor );
}

#endif
