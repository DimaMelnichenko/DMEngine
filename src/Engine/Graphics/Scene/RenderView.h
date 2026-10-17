#pragma once

#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Camera\DMFrustum.h"
#include "Shaders\view_limits.h"

namespace GS
{

// Видов в кадре: главный, четыре каскада теней солнца, дальний каскад (лес до краёв карты, ShadowCascades) и съёмка
// сцены с другой камеры — общий с шейдерами предел (Shaders/view_limits.h)
constexpr uint32_t maxRenderViews = DM_MAX_RENDER_VIEWS;

// Вид, с которого рисуется кадр, — как FSceneView в UE: матрицы, положение и пирамида видимости. Главный вид —
// камера, ещё четыре — каскады теней солнца (ShadowCascades). Объекты сцены видят кадр только через вид
struct RenderView
{
	// Что за вид — по нему объект решает, что в него рисовать (а не по номеру): главная камера, каскад теней, дальний
	// каскад (только лес импостерами), съёмка сцены с другой камеры (как SceneCapture в UE: свои цели, без ресурсов
	// главного вида — затенения окружением по экрану, объёма тумана, облаков; LOD — как у главного)
	enum class Kind : uint8_t
	{
		main,
		shadow,
		farShadow,
		capture,
	};
	static_assert( static_cast<uint32_t>( Kind::main ) == DM_VIEW_MAIN && static_cast<uint32_t>( Kind::shadow ) == DM_VIEW_SHADOW &&
				   static_cast<uint32_t>( Kind::farShadow ) == DM_VIEW_FAR_SHADOW && static_cast<uint32_t>( Kind::capture ) == DM_VIEW_CAPTURE,
				   "RenderView::Kind - DM_VIEW_* in Shaders/view_limits.h" );
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
	// Номер вида в кадре — место данных объектов на вид (узлы террейна, списки расстановки): 0 — главный, дальше — каскады
	// теней, дальний каскад и съёмка подряд, сколько их в кадре (FrameContext::views)
	uint32_t index = 0;
	// Дальний каскад теней (farShadow) — дальше Dynamic Shadow Distance на всю карту: в нём только то, что отбрасывает
	// дальнюю тень (лес импостерами); рельефа нет — его тень по карте края тени (TerrainSunShadow), моделей уровня — тоже
	Kind kind = Kind::main;
	// Каскад теней или дальний каскад: свои LOD теней, рисуется только то, что отбрасывает тень
	bool shadow() const { return kind == Kind::shadow || kind == Kind::farShadow; }
	// Тип вида для шейдеров — DM_VIEW_* (Shaders/view_limits.h)
	uint32_t shaderKind() const { return static_cast<uint32_t>( kind ); }
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

	// Перспективный вид без DMCamera — съёмка сцены (SceneCapture): положение, направление взгляда и верх (нормированные),
	// поле зрения по вертикали (радианы), ширина / высота, плоскости, м; глубина обратная, как у DMCamera
	static RenderView perspective( const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up,
								   float fieldOfView, float aspect, float nearPlane, float farPlane )
	{
		RenderView view;
		view.view = DirectX::XMMatrixLookToLH( DirectX::XMLoadFloat3( &position ), DirectX::XMLoadFloat3( &forward ),
											   DirectX::XMLoadFloat3( &up ) );
		view.projection = DirectX::XMMatrixPerspectiveFovLH( fieldOfView, aspect, farPlane, nearPlane );
		view.viewProjection = DirectX::XMMatrixMultiply( view.view, view.projection );
		view.viewInverse = DirectX::XMMatrixInverse( nullptr, view.view );
		view.position = position;
		view.direction = forward;
		view.lodOrigin = position;
		view.nearPlane = nearPlane;
		view.farPlane = farPlane;
		view.frustum = DMFrustum( view.viewProjection );
		return view;
	}
};

}
