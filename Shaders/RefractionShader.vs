////////////////////////////////////////////////////////////////////////////////
// Filename: refraction.vs
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

cbuffer ClipPlaneBuffer
{
    float4 clipPlane;
};


//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
    float clip : SV_ClipDistance0;
};

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType RefractionVertexShader(VertexInputType input)
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
    
    // Calculate the normal vector against the world matrix only.
    output.normal = mul(input.normal, (float3x3)worldMatrix);
	
    // Normalize the normal vector.
    output.normal = normalize(output.normal);
	
	// Set the clipping plane.
    output.clip = dot(mul(input.position, cb_worldMatrix), clipPlane);

    return output;
}