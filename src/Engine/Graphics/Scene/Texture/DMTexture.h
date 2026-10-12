#pragma once

#include <DirectXTex.h>
#include "D3D\GpuResources.h"
#include "Storage\DMResource.h"

namespace GS
{

// Текстура хранилища System::textures(): ресурс GPU и вид для шейдеров. Откуда данные — файл (DMTextureStorage::load,
// ImageFile::load), заглушка или процедурная текстура (DMTextureStorage::createPlaceholder / createDefaults), — текстуре
// всё равно: копию картинки на CPU она не держит (если нужна — GpuImages::captureTexture)
class DMTexture : public DMResource
{
public:
	DMTexture( uint32_t id, const std::string& name );

	// Из картинки со всеми мипами и срезами (GpuImages::createTexture); имя ресурса GPU — имя текстуры
	bool create( const DirectX::ScratchImage& image );
	// Из данных одного мипа: процедурные текстуры
	bool create( const TextureDesc& desc, const TextureData& data );

	const ShaderView& srv() const { return m_srv; }
	const Texture& texture() const { return m_texture; }
	uint32_t width() const { return m_texture.width(); }
	uint32_t height() const { return m_texture.height(); }

	// Отпечаток источника: файл (путь, размер, время правки) и как он прочитан (sRGB, мипы) — DMTextureStorage::load;
	// 0 — процедурная или запечённая текстура. Ключ кэшей, которые зависят от картинки (кадры импостеров)
	uint64_t sourceStamp() const { return m_sourceStamp; }
	void setSourceStamp( uint64_t stamp ) { m_sourceStamp = stamp; }

private:
	Texture m_texture;
	ShaderView m_srv;
	uint64_t m_sourceStamp = 0;
};

}
