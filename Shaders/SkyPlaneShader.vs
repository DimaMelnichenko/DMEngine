////////////////////////////////////////////////////////////////////////////////
// Filename: skyplane.vs
////////////////////////////////////////////////////////////////////////////////


/////////////
// GLOBALS //
/////////////
cbuffer MatrixBuffer
{
    matrix cb_worldMatrix;
    matrix cb_viewMatrix;
    matrix cb_projectionMatrix;
};


//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION;
    float2 tex : TEXCOORD0;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType SkyPlaneVertexShader(VertexInputType input)
{
    PixelInputType output;
    

    // Change the position vector to be 4 units for proper matrix calculations.
    input.position.w = 1.0f;

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(input.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    // Store the texture coordinates for the pixel shader.
    output.tex = input.tex;
	
    return output;
}