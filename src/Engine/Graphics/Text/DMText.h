#pragma once

#include "DMFont.h"
#include "DMFontShader.h"
#include <vector>


class DMText
{
private:
	struct SentenceType
	{
		com_unique_ptr<ID3D11Buffer> vertexBuffer;
		com_unique_ptr<ID3D11Buffer> indexBuffer;
		std::vector<DMFont::VertexType> raw_vertices;
		std::vector<unsigned int> raw_inices;
		unsigned int max_length;
		float red, green, blue;
	};

	/*struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
	};*/

public:
	DMText();
	~DMText();

	bool Initialize( unsigned int, unsigned int );
	bool Render( D3DXMATRIX worldMatrix, const DMCamera& camera );

	bool SetFps( unsigned int );
	bool SetCpu( unsigned int );
	bool SetVideoCardInfo( char*, unsigned int );
	bool SetCameraPosition( float, float, float );
	bool SetCameraRotation( float, float, float );
	bool SetRenderCount( unsigned int );


private:
	bool InitializeSentence( SentenceType&, unsigned int );
	bool UpdateSentence( SentenceType&, char*, int, int, float, float, float );
	bool RenderSentence( const SentenceType&, const DMCamera& );

private:
	DMFont m_Font;
	DMFontShader m_FontShader;
	unsigned int m_screenWidth, m_screenHeight;
	std::vector<SentenceType> m_sentences;
	const unsigned char m_idx_fps = 0;
	const unsigned char m_idx_cpu = 1;
	const unsigned char m_idx_video = 2;
	const unsigned char m_idx_camera_pos = 3;
	const unsigned char m_idx_camera_rot = 4;
	const unsigned char m_idx_vertex_num = 5;
	const unsigned char m_sentence_count = 6;
	
};

