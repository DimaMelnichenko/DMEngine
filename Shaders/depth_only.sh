// Вариант «только глубина» материала PBR (как FDepthOnlyVS в UE): выход вершинного шейдера с define DEPTH_ONLY
// (Shaders/LightShader.vs) и вход пиксельного шейдера отсечения Masked (mainDepth в Shaders/PBRLit.ps). Нужен проходам
// без цвета — глубине каскадов теней. Позиция — precise, как INVARIANT_OUTPUT у SV_Position в UE: компилятор не
// переставляет операции, и глубина этого варианта совпадает с полным шейдером до бита (depth prepass и проверка EQUAL)
struct DepthOnlyVertexOutput
{
	precise float4 position : SV_POSITION;
	float2 tex : TEXCOORD0;	// для clip у Masked; непрозрачные рисуют глубину без пиксельного шейдера
};
