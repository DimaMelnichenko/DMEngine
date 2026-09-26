////////////////////////////////////////////////////////////////////////////////
// Выборка по важности для GGX — для префильтра отражений и таблицы BRDF (B. Karis, «Real Shading in Unreal
// Engine 4», 2013)
////////////////////////////////////////////////////////////////////////////////

#ifndef IBL_SAMPLING_SH
#define IBL_SAMPLING_SH

// Последовательность Хаммерсли: равномерно распределённые точки квадрата без случайного шума
float2 hammersley( uint index, uint count )
{
	return float2( (float)index / count, reversebits( index ) * 2.3283064365386963e-10f );
}

// Полувектор h, распределённый по GGX с шероховатостью alpha = roughness², вокруг нормали n
float3 importanceSampleGGX( float2 xi, float alpha, float3 n )
{
	float phi = 2.0f * 3.14159265f * xi.x;
	float cosTheta = sqrt( ( 1.0f - xi.y ) / ( 1.0f + ( alpha * alpha - 1.0f ) * xi.y ) );
	float sinTheta = sqrt( 1.0f - cosTheta * cosTheta );
	float3 h = float3( sinTheta * cos( phi ), sinTheta * sin( phi ), cosTheta );

	float3 up = abs( n.z ) < 0.999f ? float3( 0.0f, 0.0f, 1.0f ) : float3( 1.0f, 0.0f, 0.0f );
	float3 tangentX = normalize( cross( up, n ) );
	float3 tangentY = cross( n, tangentX );
	return tangentX * h.x + tangentY * h.y + n * h.z;
}

#endif
