#pragma once

#include "D3D/DMD3D.h"

namespace GS
{

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11VertexShader>& shader );

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11GeometryShader>& shader );

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11HullShader>& shader );

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11DomainShader>& shader );

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11PixelShader>& shader );

HRESULT createInputLayout( std::vector<D3D11_INPUT_ELEMENT_DESC>& vertex_layout, com_unique_ptr<ID3DBlob>& shaderBuffer, com_unique_ptr<ID3D11InputLayout>& layout );

}
