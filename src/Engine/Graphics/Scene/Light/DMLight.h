#pragma once

#include "..\DMSceneObject.h"
#include "..\TextureObjects\DMRenderTexture.h"

class DMLight : public DMSceneObject
{
public:
	enum LightType { Dir, Point, Spot };

public:
	DMLight( LightType );
	DMLight( DMLight& );
	~DMLight(void);
	void update( float );

	
	float attenuation;
	ID3D11ShaderResourceView* proj_texture;
	
	void setEnabled( bool );
	bool enabled() const;

	LightType type( ) const;

	void setColor( float r, float g, float b );
	void setColor( D3DXVECTOR3& );
	D3DXVECTOR3 color() const;

	void setDirection( D3DXVECTOR3& );
	void setDirection( float, float, float );
	D3DXVECTOR3 direction() const;

	void generateMatrices();

	void setCastShadow( bool cast_shadow );
	float texelSize() const;
	void setMask( ID3D11ShaderResourceView* );
	void setShadowParam( float fov_spot_angle, unsigned int texture_size );
	const DMRenderTexture& shadow_rt() const;
	ID3D11ShaderResourceView* mask_srv() const;
	bool castShadow() const;
	const DMCamera& shadowCamera() const;
	ID3D11ShaderResourceView* shadow_map() const;
	float spotAngle() const;

private:
	void updateCamera();

private:
	bool m_enabled;
	bool m_cast_shadow;
	D3DXVECTOR3 m_color;
	LightType m_type;
	D3DXVECTOR3 m_direction;
	float m_spot_angle;
	DMCamera m_camera_shadow;
	ID3D11ShaderResourceView* m_mask_srv;
	float m_texel_size;
	DMRenderTexture m_shadow_rt;
};

