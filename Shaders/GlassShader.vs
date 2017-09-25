////////////////////////////////////////////////////////////////////////////////
// Filename: glass.vs
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
    float4 refractionPosition : TEXCOORD1;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType GlassVertexShader(VertexInputType input)
{
    PixelInputType output;
    matrix viewProjectWorld;


    // Change the position vector to be 4 units for proper matrix calculations.
    input.position.w = 1.0f;

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(input.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    // Store the texture coordinates for the pixel shader.
    output.tex = input.tex;
	
	  // Create the view projection world matrix for refraction.
    viewProjectWorld = mul(viewMatrix, cb_projectionMatrix);
    viewProjectWorld = mul(worldMatrix, viewProjectWorld);
	
	 // Calculate the input position against the viewProjectWorld matrix.
    output.refractionPosition = mul(input.position, viewProjectWorld);

    return output;
}