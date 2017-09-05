#pragma once

#include "DM3DObject.h"
#include "DMCamera.h"
#include "DMRenderTexture.h"
#include "DMRenderFilter.h"


class DMHDR : public DM3DObject
{
public:
	DMHDR( DMD3D* );
	~DMHDR();
	void Initialize( float width, float height );

	void begin();
	void postprocess_and_end( DMCamera* camera, DMRenderFilter* );

	ID3D11ShaderResourceView* mainTexture();
	ID3D11ShaderResourceView* brightTexture();
	ID3D11ShaderResourceView* debugTexture();

private:
	std::unique_ptr<DMRenderTexture> m_main_rt;
	std::unique_ptr<DMRenderTexture> m_bright_rt;
	std::unique_ptr<DMRenderTexture> m_blur_rt;
	std::unique_ptr<DMRenderTexture> m_downsample_rt;
	std::vector<std::unique_ptr<DMRenderTexture>> m_luminance_rt;
	std::unique_ptr<DMCamera> m_bright_camera;
	std::unique_ptr<DMCamera> m_blur_camera;
	ID3D11ShaderResourceView* m_debug_texture = nullptr;
};