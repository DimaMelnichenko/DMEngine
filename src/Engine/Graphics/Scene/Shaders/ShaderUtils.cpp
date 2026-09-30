#include "ShaderUtils.h"
#include <d3dcompiler.h>

namespace GS
{

UINT shaderCompileFlags()
{
#ifdef _DEBUG
	return D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	return D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
}

}
