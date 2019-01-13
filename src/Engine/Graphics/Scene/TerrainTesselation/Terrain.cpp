#include "Terrain.h"
#include "D3D/DMD3D.h"
#include "Logger/Logger.h"



Terrain::Terrain()
{

}

Terrain::~Terrain()
{

}


bool Terrain::initialize()
{
	std::string buffer = R"(cbuffer FrameConstantBuffer : register( b0 )
	{
		matrix cb_viewMatrix;
		matrix cb_viewInverseMatrix;
		matrix cb_projectionMatrix;
		matrix cb_viewProjectionMatrix;
		float3 cb_cameraPosition;
		float  cb_appTime;
		float3 cb_viewDirection;
		float  cb_elapsedTime;
		float  cb_lightCount;
		float3 fcb_dump;
	};

	cbuffer WorldBuffer : register( b1 )
	{
		matrix cb_worldMatrix;
	}; 

	cbuffer cbQuadTesselation : register( b2 )
	{
		float4      g_f4TessFactors;            
		float4      g_f2Modes;
	};

	//=================================================================================================================================
	// Functions
	//=================================================================================================================================
	float3 bilerpUV(float3 v[4], float2 uv)
	{
		// bilerp the texture coordinates    
		float3 bottom = lerp( v[0], v[1], uv.x );
		float3 top = lerp( v[3], v[2], uv.x );
		float3 result = lerp( bottom, top, uv.y );
	
		return result;    
	}

	//=================================================================================================================================
	// Shader structures
	//=================================================================================================================================

	struct Position_Input
	{
		float3 f3Position   : POSITION0;  
	};

	struct HS_ConstantOutput
	{
		float fTessFactor[3]    : SV_TessFactor;
		float fInsideTessFactor : SV_InsideTessFactor;
	};

	struct HS_ConstantOutput_Quad
	{
		float fTessFactor[4]       : SV_TessFactor;
		float fInsideTessFactor[2] : SV_InsideTessFactor;
	};

	struct DS_Output
	{
		float4 f4Position   : SV_Position;
	};

	struct PS_RenderSceneInput
	{
		float4 f4Position   : SV_Position;
	};

	struct PS_RenderOutput
	{
		float4 f4Color      : SV_Target0;
	};

	Position_Input VS_RenderSceneWithTessellation( Position_Input I )
	{
		Position_Input O;
    
		// Pass through world space position
		O.f3Position = I.f3Position;
    
		return O;    
	}

	HS_ConstantOutput_Quad HS_QuadsConstant( InputPatch<Position_Input, 4> I )
	{
		HS_ConstantOutput_Quad O = (HS_ConstantOutput_Quad)0;
    
		float2 ritf,itf; float4 rtf;
		uint mode = (uint)g_f2Modes.x;

		//Process2DQuadTessFactorsMax( float4( 4.0, 4.0, 4.0, 4.0 ), 1.0, rtf, ritf, itf);



		switch (mode)
		{
			case 0: Process2DQuadTessFactorsMax( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			case 1: Process2DQuadTessFactorsMin( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			case 2: Process2DQuadTessFactorsAvg( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			default: Process2DQuadTessFactorsMax( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
		}

		O.fTessFactor[0] = rtf.x;
		O.fTessFactor[1] = rtf.y;
		O.fTessFactor[2] = rtf.z;
		O.fTessFactor[3] = rtf.w;
		O.fInsideTessFactor[0] = ritf.x;
		O.fInsideTessFactor[1] = ritf.y;

		return O;
	}

	[domain("quad")]
	[partitioning("fractional_even")]
	[outputtopology("triangle_cw")]
	[patchconstantfunc("HS_QuadsConstant")]
	[outputcontrolpoints(4)]
	Position_Input HS_Quads_fractionaleven( InputPatch<Position_Input, 4> I, uint uCPID : SV_OutputControlPointID )
	{
		Position_Input O = (Position_Input)0;

		O.f3Position = I[uCPID].f3Position; 
    
		return O;
	}

	[domain("quad")]
	PS_RenderSceneInput DS_Quads( HS_ConstantOutput_Quad HSConstantData, const OutputPatch<Position_Input, 4> I, float2 uv : SV_DomainLocation )
	{
		PS_RenderSceneInput O = (PS_RenderSceneInput)0;
		float3 f3Position; 

		float3 p[4]; 
		[unroll]
		for (uint i=0; i<4; i++)
		{
			p[i]=I[i].f3Position;
		}
		f3Position = bilerpUV(p, uv);

		O.f4Position = float4( f3Position.xyz, 1.0 );

		//O.f4Position = mul(O.f4Position, cb_worldMatrix);
		O.f4Position = mul(O.f4Position, cb_viewMatrix);
		O.f4Position = mul(O.f4Position, cb_projectionMatrix);
        
		return O;
	}

	PS_RenderOutput PS_SolidColor( PS_RenderSceneInput I )
	{
		PS_RenderOutput O;

		O.f4Color = float4( 0.9, 0.9, 0.0, 1.0 );

		return O;
	}

	)";

	

	std::vector<D3D11_INPUT_ELEMENT_DESC> vertexLayout;
	D3D11_INPUT_ELEMENT_DESC polygonLayout;
	polygonLayout.SemanticName = "POSITION";
	polygonLayout.SemanticIndex = 0;
	polygonLayout.Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout.InputSlot = 0;
	polygonLayout.AlignedByteOffset = 0;
	polygonLayout.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout.InstanceDataStepRate = 0;
	vertexLayout.push_back( polygonLayout );

	m_shader.setLayoutDesc( std::move( vertexLayout ) );

	if( !m_shader.addShaderPassFromMem( SRVType::vs, "VS_RenderSceneWithTessellation", buffer ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromMem( SRVType::hs, "HS_Quads_fractionaleven", buffer ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromMem( SRVType::ds, "DS_Quads", buffer ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.addShaderPassFromMem( SRVType::ps, "PS_SolidColor", buffer ) )
	{
		LOG( "Не получилось скомпилить шейдер" );
		return false;
	}

	if( !m_shader.createPhase( 0, 0, -1, 0, 0 ) )
		return false;


	if( !m_path.reinitializeBuffers() )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Param ), m_constBuffer ) )
		return true;

	return true;
}

void Terrain::render()
{

	Param param;
	param.tessFactors = XMFLOAT4( 4.0, 4.0, 4.0, 4.0 );
	param.modes = XMFLOAT4( 0.0, 1.0, 0.0, 0.0 );

	Device::updateResource<Terrain::Param>( m_constBuffer.get(), param );

	DMD3D::instance().setConstantBuffer( SRVType::ds, 2, m_constBuffer );
	DMD3D::instance().setConstantBuffer( SRVType::hs, 2, m_constBuffer );

	UINT Stride = sizeof( XMFLOAT3 );
	UINT Offset = 0;

	ID3D11Buffer* vertexBuffer = m_path.vertexBuffer();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &vertexBuffer, &Stride, &Offset );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_path.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );


	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST );
	m_shader.setDrawType( GS::DMShader::by_index );
	m_shader.setPass( 0 );
	m_shader.render( m_path.indexCount() );
}

