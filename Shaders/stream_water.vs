////////////////////////////////////////////////////////////////////////////////
// Ручей — лента воды вдоль оси (StreamRibbons, WaterSimulation в режиме static): вершины уже в мире на уровне воды,
// края ленты уходят под берег — урез даёт проверка глубины. Течение и пена — из вершин
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "bindless.sh"

struct VertexInputType
{
	float3 position : POSITION;
	float4 flow : TEXCOORD0;	// скорость по X и Z мира, м/с; пена 0…1; поперёк ленты −1…1
};

struct StreamPixelInput
{
	float4 position : SV_POSITION;
	float3 worldPosition : WORLDPOS0;
	float4 flow : TEXCOORD0;
};

StreamPixelInput main( VertexInputType input )
{
	StreamPixelInput output;
	output.position = mul( float4( input.position, 1.0f ), cb_viewProjectionMatrix );
	output.worldPosition = input.position;
	output.flow = input.flow;
	return output;
}
