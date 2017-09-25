////////////////////////////////////////////////////////////////////////////////
// Filename: depth.vs
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
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 depthPosition : TEXTURE0;
};

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType DepthVertexShader(VertexInputType input)
{
   PixelInputType output;
    
    
	// Change the position vector to be 4 units for proper matrix calculations.
    input.position.w = 1.0f;

	// Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(input.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);

	// Store the position value in a second input value for depth value calculations.
	output.depthPosition = output.position;
	
	return output;

}
	
	