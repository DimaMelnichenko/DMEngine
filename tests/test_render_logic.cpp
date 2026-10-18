// Логика рендера и сцены без устройства: ключи сортировки команд (DrawSortKey.h), пересадка экземпляров на новую землю
// (ModelReseat.h) и ствол у земли для капсулы коллизии (TrunkCollision.h)
#include <doctest/doctest.h>
#include "DrawSortKey.h"
#include "Model\ModelReseat.h"
#include "Model\TrunkCollision.h"

using namespace GS;
using namespace DirectX;

TEST_CASE( "opaqueSortKey: object order first, then material, phase, raster, group" )
{
	CHECK( opaqueSortKey( 1, 900, 9, 9, 999 ) < opaqueSortKey( 2, 0, 0, 0, 0 ) );		// объекты — в порядке сцены
	CHECK( opaqueSortKey( 1, 5, 9, 9, 999 ) < opaqueSortKey( 1, 6, 0, 0, 0 ) );		// внутри — по материалу
	CHECK( opaqueSortKey( 1, 5, 2, 9, 999 ) < opaqueSortKey( 1, 5, 3, 0, 0 ) );		// по фазе
	CHECK( opaqueSortKey( 1, 5, 2, 1, 999 ) < opaqueSortKey( 1, 5, 2, 2, 0 ) );		// по растеризатору
	CHECK( opaqueSortKey( 1, 5, 2, 1, 7 ) < opaqueSortKey( 1, 5, 2, 1, 8 ) );			// группа — последней
	// Поля не залезают друг на друга: самая большая группа меньше следующего растеризатора
	CHECK( opaqueSortKey( 1, 5, 2, 1, 0x0FFFFFFF ) < opaqueSortKey( 1, 5, 2, 2, 0 ) );
}

TEST_CASE( "transparentSortKey: far before near, then object order and sequence" )
{
	CHECK( transparentSortKey( 100.0f, 9, 9 ) < transparentSortKey( 10.0f, 0, 0 ) );
	CHECK( transparentSortKey( 10.0f, 1, 9 ) < transparentSortKey( 10.0f, 2, 0 ) );
	CHECK( transparentSortKey( 10.0f, 1, 3 ) < transparentSortKey( 10.0f, 1, 4 ) );
	CHECK( transparentSortKey( -5.0f, 1, 0 ) == transparentSortKey( 0.0f, 1, 0 ) );	// за камерой — как в ней
}

TEST_CASE( "shadow and custom keys keep the object order" )
{
	CHECK( shadowSortKey( 1, 900, 9, 999 ) < shadowSortKey( 2, 0, 0, 0 ) );
	CHECK( shadowSortKey( 1, 5, 2, 999 ) < shadowSortKey( 1, 5, 3, 0 ) );
	CHECK( customSortKey( 1 ) < customSortKey( 2 ) );
	CHECK( customSortKey( 3 ) == opaqueSortKey( 3, 0, 0, 0, 0 ) );		// свой вызов — в начале своего объекта
}

TEST_CASE( "reseatPosition: moves by the terrain shift under the model, ignores less than a millimetre" )
{
	XMFLOAT3 position( 10.0f, 5.0f, 20.0f );
	float radiusSeen = 0.0f;
	CHECK( reseatPosition( position, XMFLOAT3( 4.0f, 1.0f, 2.0f ), [&]( float x, float z, float radius )
	{
		radiusSeen = radius;
		return x == 10.0f && z == 20.0f ? 0.75f : 0.0f;
	} ) );
	CHECK( position.y == doctest::Approx( 5.75f ) );
	CHECK( radiusSeen == doctest::Approx( 2.0f ) );		// половина наибольшего масштаба по горизонтали

	CHECK_FALSE( reseatPosition( position, XMFLOAT3( 0.5f, 1.0f, 0.5f ), [&]( float, float, float radius )
	{
		radiusSeen = radius;
		return 0.0005f;
	} ) );
	CHECK( position.y == doctest::Approx( 5.75f ) );
	CHECK( radiusSeen == doctest::Approx( 1.0f ) );		// маленькая модель — не меньше метра
}

TEST_CASE( "trunkBase: the trunk axis and width at the ground, low branches on one side don't move it" )
{
	// Ствол — кольца радиусом 0,3 м вокруг начала координат от −0,3 до 20 м; нижняя лапа — с одного бока, кончик на 0,5 м
	std::vector<XMFLOAT3> points;
	for( float y = -0.3f; y <= 20.0f; y += 0.5f )
		for( int i = 0; i < 12; ++i )
			points.push_back( XMFLOAT3( 0.3f * std::cos( i * XM_2PI / 12.0f ), y, 0.3f * std::sin( i * XM_2PI / 12.0f ) ) );
	for( float x = 0.5f; x <= 3.5f; x += 0.25f )
		points.push_back( XMFLOAT3( x, 0.5f + 0.1f * x, 0.2f ) );
	TrunkBase base;
	REQUIRE( trunkBase( points, 0.25f, base ) );
	CHECK( base.center.x == doctest::Approx( 0.0f ).epsilon( 0.01 ) );
	CHECK( base.center.y == doctest::Approx( 0.0f ).epsilon( 0.01 ) );
	CHECK( base.radius == doctest::Approx( 0.3f ).epsilon( 0.01 ) );
	CHECK( base.bottom == doctest::Approx( -0.3f ) );
	CHECK( base.height == doctest::Approx( 20.0f ) );	// кольца до 19,7 м
	CHECK_FALSE( trunkBase( {}, 0.25f, base ) );
}
