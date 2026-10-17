////////////////////////////////////////////////////////////////////////////////
// Пределы видов кадра — общие для C++ и HLSL (как slots.h): RenderView.h (maxRenderViews) и Shaders/scatter.cs (MAX_VIEWS)
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_VIEW_LIMITS_H
#define DM_VIEW_LIMITS_H

// Видов в кадре: главный, четыре каскада теней солнца, дальний каскад (ShadowCascades) и съёмка сцены с другой камеры
#define DM_MAX_RENDER_VIEWS 7

// Тип вида (RenderView::Kind) — у расстановки в FrustumBuffer::viewParams.x
#define DM_VIEW_MAIN 0
#define DM_VIEW_SHADOW 1
#define DM_VIEW_FAR_SHADOW 2
#define DM_VIEW_CAPTURE 3

#endif
