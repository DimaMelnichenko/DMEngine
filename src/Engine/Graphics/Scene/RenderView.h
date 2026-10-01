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
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX projection;
	DirectX::XMMATRIX viewProjection;
	DirectX::XMMATRIX viewInverse;
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 direction;
	// Откуда считаются LOD и морфинг террейна. У главного вида — положение камеры; у каскада теней тоже положение
	// главной камеры: иначе тень строилась бы по другой геометрии, и поверхности затеняли бы сами себя
	DirectX::XMFLOAT3 lodOrigin;
	float nearPlane = 0.0f;	// расстояния до ближней и дальней плоскостей, м (глубина в буфере — обратная: 1 у ближней)
	float farPlane = 0.0f;
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
		view.viewProjection = DirectX::XMMatrixMultiply( view.view, view.projection );
		view.viewInverse = DirectX::XMMatrixInverse( nullptr, view.view );
		view.position = camera.position();
		camera.viewDirection( &view.direction );
		view.lodOrigin = view.position;
		view.nearPlane = camera.nearPlane();
		view.farPlane = camera.farPlane();
		view.frustum = DMFrustum( view.viewProjection );
		return view;
	}
};

}
