
float2 hash( float2 p )
{
	p = float2( dot( p, float2( 127.1, 311.7 ) ),
			  dot( p, float2( 269.5, 183.3 ) ) );

	return -1.0 + 2.0 * frac( sin( p ) * 43758.5453123 );
}

float3 hash3( float3 p )
{
	p = float3( dot( p, float3( 127.1, 311.7, 3.3 ) ),
			    dot( p, float3( 269.5, 183.3, 109.5 ) ),
				dot( p, float3( 173.3, 89.5, 379.1 ) )			  
			);

	return -1.0 + 2.0 * frac( sin( p ) * 43758.5453123 );
}

float noise( float2 p )
{
    float2 i = floor( p );
    float2 f = frac( p );
	
	float2 u = f * f * f * ( f * ( f * 6 - 15 ) + 10 );

    return lerp( lerp( dot( hash( i + float2(0.0,0.0) ), f - float2(0.0,0.0) ), 
                     dot( hash( i + float2(1.0,0.0) ), f - float2(1.0,0.0) ), u.x),
                lerp( dot( hash( i + float2(0.0,1.0) ), f - float2(0.0,1.0) ), 
                     dot( hash( i + float2(1.0,1.0) ), f - float2(1.0,1.0) ), u.x), u.y);
}

float noise3( float3 p )
{
    float3 i = floor( p );
    float3 f = frac( p );
	
	float3 u = f * f * f * ( f * ( f * 6 - 15 ) + 10 );

    return lerp( 
				lerp( lerp(  dot( hash3( i + float3( 0.0, 0.0, 0.0 ) ), f - float3( 0.0, 0.0, 0.0 ) ), 
							 dot( hash3( i + float3( 1.0, 0.0, 0.0 ) ), f - float3( 1.0, 0.0, 0.0 ) ), u.x ),
					   lerp( dot( hash3( i + float3( 0.0, 1.0, 0.0 ) ), f - float3( 0.0, 1.0, 0.0 ) ), 
						     dot( hash3( i + float3( 1.0, 1.0, 0.0 ) ), f - float3( 1.0, 1.0, 0.0 ) ), u.x ), u.y),
			    lerp( lerp(  dot( hash3( i + float3( 0.0, 0.0, 1.0 ) ), f - float3( 0.0, 0.0, 1.0 ) ), 
						     dot( hash3( i + float3( 1.0, 0.0, 1.0 ) ), f - float3( 1.0, 0.0, 1.0 ) ), u.x ),
					  lerp(  dot( hash3( i + float3( 0.0, 1.0, 1.0 ) ), f - float3( 0.0, 1.0, 1.0 ) ), 
						     dot( hash3( i + float3( 1.0, 1.0, 1.0 ) ), f - float3( 1.0, 1.0, 1.0 ) ), u.x ), u.y), 
				u.z );
}

float PerlinNoise3D( float3 p, uint octaves )
{
	p *= 5.0;
	
	float f = 0.0;
	
	float3x3 m = float3x3(	1.6,  1.2, 2.9,
							-1.2, 1.6, 1.1,
							-0.9, -1.1, 1.6);
	
	float amplitude = 0.5;
	
	while( octaves > 0 )
	{
		octaves -= 1;
		f += amplitude * noise3( p ); 
		p = mul( p, m );
		
		amplitude *= 0.5f;
	}
	
	f = 0.5 + 0.5 * f;
	
    //f *= lerp( 0.0, 0.005, abs(p.x-0.6) );
	
	return f;
}


float PerlinNoise2D( float2 p, uint octaves )
{
	p *= 5.0;
	
	float f = 0.0;
	
	float2x2 m = float2x2( 1.6,  1.2, -1.2,  1.6 );
	
	float amplitude = 0.5;
	
	while( octaves > 0 )
	{
		octaves -= 1;
		f += amplitude * noise( p ); 
		p = mul( p, m );
		
		amplitude *= 0.5f;
	}
	
	f = 0.5 + 0.5 * f;
	
    //f *= lerp( 0.0, 0.005, abs(p.x-0.6) );
	
	return f;
}
/*
static const float PI = 3.14159265f;

double stripes(double x, double f) {
	double t = .5 + .5 * sin( f * 2 * PI * x );
	return t * t - .5;
}*/










