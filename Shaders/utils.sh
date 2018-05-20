/*******************************
Sample :


if (x == 0) {
  y += 5;
}

This can be transformed to the following.

y += 5 * when_eq(x, 0)

********************************
*/


float4 when_eq(float4 x, float4 y) 
{
  return 1.0 - abs(sign(x - y));
}

float4 when_neq(float4 x, float4 y) 
{
  return abs(sign(x - y));
}

float4 when_gt(float4 x, float4 y) 
{
  return max(sign(x - y), 0.0);
}

float4 when_lt(float4 x, float4 y) 
{
  return max(sign(y - x), 0.0);
}

float4 when_ge(float4 x, float4 y) 
{
  return 1.0 - when_lt(x, y);
}

float4 when_le( float4 x, float4 y) 
{
  return 1.0 - when_gt(x, y);
}