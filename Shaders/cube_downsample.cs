////////////////////////////////////////////////////////////////////////////////
// Мип cubemap из предыдущего: среднее 2 × 2 текселей грани (box, как GenerateMips D3D11 у степеней двойки). В D3D12
// GenerateMips нет, поэтому мипы куба неба и панорамы строит этот compute-проход, по вызову на мип: SRV — мип N
// (6 граней как массив), UAV — мип N + 1. Группа 8 × 8 потоков на грань (z — грань), класс SkyLight (buildMips)
////////////////////////////////////////////////////////////////////////////////

Texture2DArray<float4> g_source : register( t0 );		// мип N: один мип как массив из 6 граней
RWTexture2DArray<float4> g_target : register( u0 );	// мип N + 1

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	uint width, height, faces;
	g_target.GetDimensions( width, height, faces );
	if( id.x >= width || id.y >= height )
		return;

	const int3 source = int3( id.xy * 2, id.z );
	const float4 sum = g_source.Load( int4( source, 0 ) ) +
					   g_source.Load( int4( source.x + 1, source.y, source.z, 0 ) ) +
					   g_source.Load( int4( source.x, source.y + 1, source.z, 0 ) ) +
					   g_source.Load( int4( source.x + 1, source.y + 1, source.z, 0 ) );
	g_target[id] = sum * 0.25f;
}
