#pragma once

#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Camera\DMFrustum.h"

namespace GS
{

// Видов в кадре: главный и четыре каскада теней солнца
constexpr uint32_t maxRenderViews = 5;

// Вид, с которого рисуется кадр, — как FSceneView в UE: матрицы, положение и пирамида видимости. Главный вид —
// камера, ещё четыре — каскады теней солнца (ShadowCascades). Объекты сцены видят кадр только через вид
struct RenderView
{
	XMMATRIX view;
	XMMATRIX projection;
	XMMATRIX viewProjection;
	XMMATRIX viewInverse;
	XMFLOAT3 position;
	XMFLOAT3 direction;
	// Откуда считаются LOD и морфинг террейна. У главного вида — положение камеры; у каскада теней тоже положение
	// главной камеры: иначе тень строилась бы по другой геометрии, и поверхности затеняли бы сами себя
	XMFLOAT3 lodOrigin;
	float farPlane;		// расстояние до дальней плоскости, м
	DMFrustum frustum;
	uint32_t index = 0;	// номер вида в кадре: 0 — главный, 1…4 — каскады теней (данные объектов на вид, например узлы террейна)
	// Каскад теней: на каких расстояниях от главной камеры точка может попасть в каскад, м. Каскад покрывает глубину
	// взгляда nᵢ…fᵢ (SplitNear / SplitFar в UE), а у края кадра расстояние больше глубины, поэтому дальняя граница —
	// fᵢ / cos(угла до угла кадра). Объект, все инстансы которого вне этого диапазона, в каскад не рисуется. У главного — 0
	float cascadeNear = 0.0f;
	float cascadeFar = 0.0f;

	static RenderView fromCamera( const DMCamera& camera )
	{
		RenderView view;
		camera.viewMatrix( &view.view );
		camera.projectionMatrix( &view.projection );
		view.viewProjection = XMMatrixMultiply( view.view, view.projection );
		view.viewInverse = XMMatrixInverse( nullptr, view.view );
		view.position = camera.position();
		camera.viewDirection( &view.direction );
		view.lodOrigin = view.position;
		// Перспективная проекция LH: _33 = f / (f − n), _43 = −n·f / (f − n)
		XMFLOAT4X4 projection;
		XMStoreFloat4x4( &projection, view.projection );
		view.farPlane = projection._43 / ( 1.0f - projection._33 );
		view.frustum = DMFrustum( view.viewProjection );
		return view;
	}
};

}
