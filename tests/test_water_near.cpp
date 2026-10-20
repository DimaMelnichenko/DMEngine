// Окно подробного течения у камеры (NearWindow): сторона кратна 8, камера посередине, тексель ячейки по модулю стороны —
// при сдвиге окна ячейка остаётся в своём текселе, вошедшие занимают тексели ушедших; телепорт — окно заново
#include <doctest/doctest.h>
#include <set>
#include <utility>
#include "Water\NearWindow.h"

using namespace GS;
using namespace DirectX;

TEST_CASE( "NearWindow: the side is a multiple of 8, the camera is in the middle" )
{
	CHECK( NearWindow::cells( 10.0f, 0.0625f ) == 160 );
	CHECK( NearWindow::cells( 10.0f, 0.07f ) % 8 == 0 );
	CHECK( NearWindow::cells( 0.1f, 0.0625f ) == 8 );
	const uint32_t size = 160;
	const XMINT2 origin = NearWindow::origin( 100.0f, 200.0f, 0.0625f, size );
	CHECK( origin.x == 1600 - 80 );
	CHECK( origin.y == 3200 - 80 );
	CHECK( NearWindow::contains( XMINT2( 1600, 3200 ), origin, size ) );
	CHECK_FALSE( NearWindow::contains( XMINT2( origin.x + 160, origin.y ), origin, size ) );
}

TEST_CASE( "NearWindow: the window is where the camera looks at the ground, ahead of the camera when it looks up" )
{
	auto flat = []( float, float, float& h ) { h = 0.0f; return true; };
	// С высоты 10 м под 45° вдоль +x — земля в 10 м впереди
	const XMFLOAT2 hit = NearWindow::focus( XMFLOAT3( 0.0f, 10.0f, 0.0f ), XMFLOAT3( 1.0f, -1.0f, 0.0f ), 30.0f, 10.0f, flat );
	CHECK( hit.x == doctest::Approx( 10.0f ).epsilon( 0.03 ) );
	CHECK( hit.y == doctest::Approx( 0.0f ) );
	// Вверх — перед камерой на половину окна
	const XMFLOAT2 up = NearWindow::focus( XMFLOAT3( 0.0f, 10.0f, 0.0f ), XMFLOAT3( 0.0f, 1.0f, 1.0f ), 30.0f, 10.0f, flat );
	CHECK( up.x == doctest::Approx( 0.0f ) );
	CHECK( up.y == doctest::Approx( 5.0f ) );
	// Земля дальше reach — тоже перед камерой
	const XMFLOAT2 beyond = NearWindow::focus( XMFLOAT3( 0.0f, 100.0f, 0.0f ), XMFLOAT3( 1.0f, -0.1f, 0.0f ), 30.0f, 10.0f, flat );
	CHECK( beyond.x == doctest::Approx( 5.0f ) );
}

TEST_CASE( "NearWindow: a cell keeps its texel when the window shifts, the entered cells take the texels of the left ones" )
{
	const uint32_t size = 16;
	const XMINT2 before( -5, 7 );
	const XMINT2 after( -2, 6 );		// на 3 ячейки по x, на 1 назад по y
	// Общая ячейка — тот же тексель
	const XMINT2 shared( 3, 10 );
	REQUIRE( NearWindow::contains( shared, before, size ) );
	REQUIRE( NearWindow::contains( shared, after, size ) );
	const XMUINT2 a = NearWindow::address( shared, size );
	CHECK( a.x < size );
	CHECK( a.y < size );
	// Тексели ячеек окна — все разные (биекция), у отрицательных ячеек тоже
	std::set<std::pair<uint32_t, uint32_t>> texels;
	size_t entered = 0;
	for( int32_t y = after.y; y < after.y + static_cast<int32_t>( size ); ++y )
		for( int32_t x = after.x; x < after.x + static_cast<int32_t>( size ); ++x )
		{
			const XMUINT2 t = NearWindow::address( XMINT2( x, y ), size );
			texels.insert( { t.x, t.y } );
			entered += NearWindow::contains( XMINT2( x, y ), before, size ) ? 0 : 1;
		}
	CHECK( texels.size() == size * size );
	// Вошли столбцы справа (3 × 16) и строка снизу (16), без угла, посчитанного дважды
	CHECK( entered == 3 * 16 + 16 - 3 );
	CHECK_FALSE( NearWindow::jumped( after, before, size ) );
	CHECK( NearWindow::jumped( XMINT2( before.x + 16, before.y ), before, size ) );
}
