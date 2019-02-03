#include "Layout.h"
#include "D3D\DMD3D.h"
#include <d3dcompiler.h>

Layout::Layout()
{

}

Layout::~Layout()
{

}

bool Layout::initLayouts()
{
	std::string inputSemantic = "\
	struct VertexInput\
	{\
		float4 position : POSITION0;\
		float2 tex : TEXCOORD0;\
		float3 normal : NORMAL0;\
		float3 tangent : TANGENT0;\
		float3 binormal : BINORMAL0;\
		uint instance_index : SV_InstanceID;\
	};";
	std::string shaderDump = " float4 main() : SV_POSITION {return float4(0,0,0,0);}";

	std::string pseudoShader = inputSemantic + shaderDump;

	ID3D10Blob* shaderBuffer;
	ID3D10Blob* errorMessage = nullptr;

	HRESULT result = D3DCompile( pseudoShader.data(),
											  sizeof( std::string::value_type) * pseudoShader.size(),
											  nullptr, 
											  nullptr,
											  nullptr,
											  "main",
											  "vs_5_0",
											  D3D10_SHADER_ENABLE_STRICTNESS,
											  0,
											  &shaderBuffer, &errorMessage );
	if( errorMessage )
	{
		MessageBox( 0, "Create layout error", (char*)( errorMessage->GetBufferPointer() ), MB_OK );
		return false;
	}
	
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	std::vector<D3D11_INPUT_ELEMENT_DESC> vertex_layout;

	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TEXCOORD";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "NORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "TANGENT";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	polygonLayout.SemanticName = "BINORMAL";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;

	vertex_layout.push_back( polygonLayout );

	ID3D11InputLayout* layout;
	result = DMD3D::instance().GetDevice()->CreateInputLayout( &vertex_layout[0], vertex_layout.size(), shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), &layout );
	if( FAILED( result ) )
	{
		return false;
	}

	LayoutItem item{ make_com_ptr<ID3D11InputLayout>( layout ), inputSemantic };
	m_layouts.insert( std::make_pair( "PTNTB", std::move( item ) ) );

	return true;
}

Layout::LayoutItem* Layout::get( const std::string& name )
{
	Layout::LayoutItem* result = nullptr;

	if( m_layouts.count( name ) )
	{
		result = &m_layouts[name];
	}

	return result;
}