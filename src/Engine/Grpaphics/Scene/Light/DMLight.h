#pragma once


#include "DMSceneObject.h"
#include "DMCamera.h"
#include "DMRenderTexture.h"

class DMLight : public DMSceneObject
{
public:
	enum LightType { Dir, Point, Spot };

public:
	DMLight( DMD3D*, LightType );
	~DMLight(void);
	void update( float );

	
	float attenuation;
	ID3D11ShaderResourceView* proj_texture;
	
	void setEnabled( bool );
	bool enabled();

	LightType type( );

	void setColor( float r, float g, float b );
	void setColor( D3DXVECTOR3& );
	D3DXVECTOR3 color();

	void setDirection( D3DXVECTOR3& );
	void setDirection( float, float, float );
	D3DXVECTOR3 direction();

	void generateMatrices();

	void setCastShadow( bool cast_shadow );
	float texelSize();
	void setMask( ID3D11ShaderResourceView* );
	void setShadowParam( float fov_spot_angle, unsigned int texture_size );
	DMRenderTexture* shadow_rt();
	ID3D11ShaderResourceView* mask_srv();
	bool castShadow();
	DMCamera* shadowCamera();
	ID3D11ShaderResourceView* shadow_map();
	float spotAngle();


private:
	bool m_enabled;
	bool m_cast_shadow;
	D3DXVECTOR3 m_color;
	LightType m_type;
	D3DXVECTOR3 m_direction;
	float m_spot_angle;
	std::unique_ptr<DMCamera> m_camera_shadow;
	ID3D11ShaderResourceView* m_mask_srv;
	float m_texel_size;
	std::unique_ptr<DMRenderTexture> m_shadow_rt;
};

