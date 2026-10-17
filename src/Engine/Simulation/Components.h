#pragma once

#include <cstdint>
#include <DirectXMath.h>

namespace GS
{

// Компоненты сущностей мира симуляции (World, EnTT) — простые данные без поведения; поведение — у систем
// (SimulationSystem). Ядро не знает устройства: модель — только id хранилища, рисует её движок (WorldEntities)

// Место сущности: точка на земле (у существа — под ногами), поворот кватернионом, масштаб
struct Transform
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );
	float scale = 1.0f;
};

// Место до последнего шага: World копирует в него Transform перед шагом, отрисовка смешивает их по доле шага
// (World::interpolation) — движение плавное при любой частоте кадров
struct PreviousTransform
{
	Transform transform;
};

// Вид сущности — модель хранилища System::models(); низ модели поднят над точкой Transform на groundOffset (× масштаб):
// его считает движок по границам модели, точка остаётся на земле
struct ModelVisual
{
	uint32_t model = 0;
	float groundOffset = 0.0f;
};

// Место между прежним (t = 0) и новым (t = 1): положение и масштаб — линейно, поворот — по дуге (slerp)
inline Transform interpolate( const Transform& from, const Transform& to, float t )
{
	using namespace DirectX;
	Transform result;
	XMStoreFloat3( &result.position, XMVectorLerp( XMLoadFloat3( &from.position ), XMLoadFloat3( &to.position ), t ) );
	XMStoreFloat4( &result.rotation, XMQuaternionSlerp( XMLoadFloat4( &from.rotation ), XMLoadFloat4( &to.rotation ), t ) );
	result.scale = from.scale + ( to.scale - from.scale ) * t;
	return result;
}

}
