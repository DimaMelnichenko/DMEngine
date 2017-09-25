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
    float4 color : COLOR;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
    

    // Change the position vector to be 4 units for proper matrix calculations.
    input.position.w = 1.0f;

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(input.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    // Store the input color for the pixel shader to use.
    output.color = input.color;
    
    return output;
}