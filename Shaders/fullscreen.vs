////////////////////////////////////////////////////////////////////////////////
// Filename: texture.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"

//////////////
// TYPEDEFS //
//////////////

struct VertexInputType
{
    uint vertex_id : SV_VertexID;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
	float2 uv: TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output = (PixelInputType)0;
	
	//generate clip space position
	output.position.x = (float)( input.vertex_id / 2 ) * 4.0 - 1.0f;
	output.position.y = (float)( input.vertex_id % 2 ) * 4.0 - 1.0f;
	output.position.z = 0.0f;
	output.position.w = 1.0f;

    //texture coorduinates
	output.uv.x = (float)( input.vertex_id / 2) * 2.0f;
	output.uv.y = 1.0f - (float)( input.vertex_id % 2) * 2.0f;
	
    return output;
}