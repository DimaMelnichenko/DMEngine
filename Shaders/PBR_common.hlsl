////////////////////////////////////////////////////////////////////////////////
// Filename: light.ps
////////////////////////////////////////////////////////////////////////////////

#include "samplers.sh"
#include "common.vs"

static const float PI = 3.1415926535897932384626433832795;

float GGX_PartialGeometry( float cosThetaN, float alpha ) 
{
    float cosTheta_sqr = saturate( cosThetaN * cosThetaN );
    float tan2 = ( 1.0f - cosTheta_sqr ) / cosTheta_sqr;
    float GP = 2.0f / ( 1.0f + sqrt( 1.0f + alpha * alpha * tan2 ) );
    return GP;
}

float GGX_Distribution( float cosThetaNH, float alpha ) 
{
    float alpha2 = alpha * alpha;
    float NH_sqr = saturate(cosThetaNH * cosThetaNH);
    float den = NH_sqr * alpha2 + (1.0 - NH_sqr);
    return alpha2 / ( PI * den * den );
}

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0 - F0) * pow(1.0 - saturate(cosTheta), 5.0);
}

float3 CookTorrance_GGX(float3 n, float3 l, float3 v, float3 f0, float roughness, float3 albedo ) 
{
    n = normalize(n);
    v = normalize(v);
    l = normalize(l);
    float3 h = normalize( l + v );
    //precompute dots
	
    float NL = dot(n, l);
    if (NL <= 0.0) 
		return 0.0;
		
    float NV = dot(n, v);
	
   /* if (NV <= 0.0) 
		return float3( 0.0,0.0,1.0 );*/
    float NH = dot(n, h);
    float HV = dot(h, v);
    
    //precompute roughness square
    float roug_sqr = roughness * roughness;
    
    //calc coefficients
    float G = GGX_PartialGeometry( NV, roug_sqr ) * GGX_PartialGeometry( NL, roug_sqr );	
    float D = GGX_Distribution( NH, roug_sqr );	
    float3 F = FresnelSchlick( f0, HV );
    
    //mix
    float3 specK = G * D * F * 0.25 / (NV);
    float3 diffK = saturate( 1.0 - F ) / PI;
    return max(0.0, albedo * diffK * NL + specK );
}

