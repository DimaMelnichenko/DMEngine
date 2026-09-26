#pragma once

#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Camera\DMFrustum.h"

namespace GS
{

// Вид, с которого рисуется кадр, — как FSceneView в UE: матрицы, положение и пирамида видимости. Сейчас один —
// главная камера; каскады теней солнца станут ещё видами. Объекты сцены видят кадр только через вид
struct RenderView
{
	XMMATRIX view;
	XMMATRIX projection;
	XMMATRIX viewProjection;
	XMMATRIX viewInverse;
	XMFLOAT3 position;
	XMFLOAT3 direction;
	// Откуда считаются LOD и морфинг террейна. У главного вида — положение камеры; у каскада теней тоже будет
	// положение главной камеры: иначе тень строилась бы по другой геометрии, и поверхности затеняли бы сами себя
	XMFLOAT3 lodOrigin;
	float farPlane;		// расстояние до дальней плоскости, м
	DMFrustum frustum;

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
