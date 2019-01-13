#include "ShaderUtils.h"

namespace GS
{

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11VertexShader>& shader )
{
	ID3D11VertexShader* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreateVertexShader( buffer->GetBufferPointer(), buffer->GetBufferSize(), nullptr, &rawPointer );

	shader.reset( rawPointer );

	return result;
}

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11GeometryShader>& shader )
{
	ID3D11GeometryShader* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreateGeometryShader( buffer->GetBufferPointer(), buffer->GetBufferSize(), nullptr, &rawPointer );

	shader.reset( rawPointer );

	return result;
}

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11HullShader>& shader )
{
	ID3D11HullShader* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreateHullShader( buffer->GetBufferPointer(), buffer->GetBufferSize(), nullptr, &rawPointer );

	shader.reset( rawPointer );

	return result;
}

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11DomainShader>& shader )
{
	ID3D11DomainShader* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreateDomainShader( buffer->GetBufferPointer(), buffer->GetBufferSize(), nullptr, &rawPointer );

	shader.reset( rawPointer );

	return result;
}

HRESULT createShader( com_unique_ptr<ID3DBlob>& buffer, com_unique_ptr<ID3D11PixelShader>& shader )
{
	ID3D11PixelShader* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreatePixelShader( buffer->GetBufferPointer(), buffer->GetBufferSize(), nullptr, &rawPointer );

	shader.reset( rawPointer );

	return result;
}

HRESULT createInputLayout( std::vector<D3D11_INPUT_ELEMENT_DESC>& vertex_layout, com_unique_ptr<ID3DBlob>& shaderBuffer, com_unique_ptr<ID3D11InputLayout>& layout )
{
	ID3D11InputLayout* rawPointer = nullptr;
	HRESULT result = DMD3D::instance().GetDevice()->CreateInputLayout( &vertex_layout[0], vertex_layout.size(), shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &rawPointer );

	layout.reset( rawPointer );

	return result;
}

}