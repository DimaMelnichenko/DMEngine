#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include "DirectX.h"

namespace GS
{

// Окно подробного течения у камеры (NearWaterFlow, Shaders/water_flow.cs с FLOW_NEAR): ячейки сетки мира с шагом step от
// угла мира (x — по X, y — по Z мира), окно size² вокруг камеры, привязанное к миру. Тексель ячейки — по модулю стороны
// окна: окно сдвигается, а ячейка остаётся в том же текселе, и в текстурах окна меняются только вошедшие ячейки (как
// тороидальный clipmap). Чистая логика без устройства — с тестом (tests/test_water_near.cpp); тот же адрес — cellAddress
// в шейдере
struct NearWindow
{
	// Ячеек по стороне окна стороной side, м, с ячейкой step: кратно 8 (группа потоков — блок 8 × 8), не меньше 8
	static uint32_t cells( float side, float step )
	{
		const uint32_t count = static_cast<uint32_t>( std::ceil( side / std::max( step, 1e-3f ) ) );
		return std::max( ( count + 7u ) / 8u * 8u, 8u );
	}

	// Середина окна — там, куда смотрит камера: луч от глаза шагами по 0,25 м до земли, не дальше reach по горизонтали (вода у
	// ног или в десятке метров впереди — в окне, а не позади камеры). Не попал (смотрит вверх или вдаль) — перед камерой на
	// половину окна side. height( x, z, h ) — высота земли в точке, false — земли нет
	template<class Height>
	static DirectX::XMFLOAT2 focus( const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& direction, float reach, float side,
									const Height& height )
	{
		const float horizontal = std::sqrt( direction.x * direction.x + direction.z * direction.z );
		const float length = std::sqrt( horizontal * horizontal + direction.y * direction.y );
		if( length < 1e-6f )
			return DirectX::XMFLOAT2( eye.x, eye.z );
		const float stepLength = 0.25f;
		for( float t = stepLength; t * horizontal / length <= reach; t += stepLength )
		{
			const float x = eye.x + direction.x / length * t;
			const float y = eye.y + direction.y / length * t;
			const float z = eye.z + direction.z / length * t;
			float ground = 0.0f;
			if( height( x, z, ground ) && y <= ground )
				return DirectX::XMFLOAT2( x, z );
		}
		if( horizontal < 1e-3f )
			return DirectX::XMFLOAT2( eye.x, eye.z );
		return DirectX::XMFLOAT2( eye.x + direction.x / horizontal * 0.5f * side, eye.z + direction.z / horizontal * 0.5f * side );
	}

	// Первая ячейка окна size² с точкой (x, z мира) посередине
	static DirectX::XMINT2 origin( float cameraX, float cameraZ, float step, uint32_t size )
	{
		return DirectX::XMINT2( static_cast<int32_t>( std::floor( cameraX / step ) ) - static_cast<int32_t>( size / 2 ),
								static_cast<int32_t>( std::floor( cameraZ / step ) ) - static_cast<int32_t>( size / 2 ) );
	}

	// Тексель ячейки в текстурах окна — по модулю стороны (и у отрицательных)
	static DirectX::XMUINT2 address( DirectX::XMINT2 cell, uint32_t size )
	{
		const int32_t n = static_cast<int32_t>( size );
		return DirectX::XMUINT2( static_cast<uint32_t>( ( cell.x % n + n ) % n ), static_cast<uint32_t>( ( cell.y % n + n ) % n ) );
	}

	// Ячейка в окне с первой ячейкой origin
	static bool contains( DirectX::XMINT2 cell, DirectX::XMINT2 origin, uint32_t size )
	{
		const int32_t n = static_cast<int32_t>( size );
		return cell.x >= origin.x && cell.y >= origin.y && cell.x < origin.x + n && cell.y < origin.y + n;
	}

	// Окно сдвинулось дальше своей стороны (телепорт): общих ячеек нет — всё окно начинается заново
	static bool jumped( DirectX::XMINT2 origin, DirectX::XMINT2 previous, uint32_t size )
	{
		const int32_t n = static_cast<int32_t>( size );
		return std::abs( origin.x - previous.x ) >= n || std::abs( origin.y - previous.y ) >= n;
	}
};

}
