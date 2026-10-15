#pragma once

#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Camera\DMFrustum.h"
#include "Shaders\view_limits.h"

namespace GS
{

// Видов в кадре: главный, четыре каскада теней солнца и дальний каскад (лес до краёв карты, ShadowCascades) — общий
// с шейдерами предел (Shaders/view_limits.h)
constexpr uint32_t maxRenderViews = DM_MAX_RENDER_VIEWS;

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
	// На сколько ниже точки LOD ноги (ходьба, WalkMode::eyeAboveFeet; в полёте 0). Террейн считает LOD от отрезка «глаза —
	// ноги»: земля под ногами — рядом, а не в росте от глаз (CDLODTerrain, lodDistance в cdlod.vs). У каскадов — главного вида
	float lodEyeHeight = 0.0f;
	float nearPlane = 0.0f;	// расстояния до ближней и дальней плоскостей, м (глубина в буфере — обратная: 1 у ближней)
	float farPlane = 0.0f;
	DMFrustum frustum;
	uint32_t index = 0;	// номер вида в кадре: 0 — главный, 1…4 — каскады теней, 5 — дальний (данные объектов на вид, например узлы террейна)
	// Дальний каскад теней — дальше Dynamic Shadow Distance на всю карту: в нём только то, что отбрасывает дальнюю тень
	// (лес импостерами); рельефа нет — его тень по карте края тени (TerrainSunShadow), моделей уровня — тоже
	bool farShadow = false;
	// Каскад теней: на каких расстояниях от главной камеры точка может попасть в каскад, м. Каскад покрывает глубину
	// взгляда nᵢ…fᵢ (SplitNear / SplitFar в UE) и полосу смешения в конце предыдущего (Cascade Transition Fraction) —
	// с неё начинается cascadeNear; у края кадра расстояние больше глубины, поэтому дальняя граница — fᵢ / cos(угла до
	// угла кадра). Объект, все инстансы которого вне этого диапазона, в каскад не рисуется. Точнее — по глубине взгляда
	// cascadeNear…cascadeDepthFar (fᵢ), как каскад выбирает пиксель (Shaders/shadows.sh). У главного — 0
	float cascadeNear = 0.0f;
	float cascadeFar = 0.0f;
	float cascadeDepthFar = 0.0f;

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
