////////////////////////////////////////////////////////////////////////////////
// Цвет земли у растения расстановки: scatter.cs кладёт его в экземпляр (ScatterItem::ground, упаковка — packScatterGround),
// вершинный шейдер материала (LightShader.vs) и импостер (impostor.ps) подмешивают его к своему цвету — как трава
// ландшафта UE берёт цвет слоя через RVT. Нули — растение своего цвета (постоянные слои, камни русел, слой без настроек)
////////////////////////////////////////////////////////////////////////////////

#ifndef SCATTER_GROUND_SH
#define SCATTER_GROUND_SH

// rgb и разброс — половинками: x — r | g, y — b | (яркость − 1), z — доля у корня | высота перехода, м, w — доля вдали (float)
uint4 packScatterGround( float3 color, float brightness, float rootShare, float height, float farShare )
{
	return uint4( f32tof16( color.r ) | ( f32tof16( color.g ) << 16 ), f32tof16( color.b ) | ( f32tof16( brightness - 1.0f ) << 16 ),
				  f32tof16( rootShare ) | ( f32tof16( height ) << 16 ), asuint( farShare ) );
}

// Точка растения на высоте height над корнем, м: ground.rgb — цвет земли, ground.a — его доля (у корня — доля слоя, к высоте
// перехода — нет; вдали — не меньше доли вдали по всей высоте); brightness — множитель своего цвета (пятна яркости земли и
// разброс по экземплярам). Цвет точки — lerp( свой × brightness, ground.rgb, ground.a )
void scatterGround( uint4 packed, float height, out float4 ground, out float brightness )
{
	ground.rgb = float3( f16tof32( packed.x ), f16tof32( packed.x >> 16 ), f16tof32( packed.y ) );
	brightness = 1.0f + f16tof32( packed.y >> 16 );
	const float rootShare = f16tof32( packed.z );
	const float transition = f16tof32( packed.z >> 16 );
	ground.a = saturate( max( rootShare * saturate( 1.0f - height / max( transition, 1e-3f ) ), asfloat( packed.w ) ) );
}

#endif
