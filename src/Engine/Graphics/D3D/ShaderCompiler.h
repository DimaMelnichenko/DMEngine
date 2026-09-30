#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "DM3DUtils.h"

struct IDxcCompiler3;
struct IDxcUtils;
struct IDxcIncludeHandler;

// Компиляция HLSL в DXIL компилятором DXC в процессе (dxcompiler.dll рядом с exe; dxil.dll подписывает байткод —
// без подписи рантайм D3D12 шейдер не примет). Цель — Shader Model 6.6 (bindless: ResourceDescriptorHeap), HLSL 2021;
// в Debug — отладочная информация без оптимизации (исходник виден в PIX), в Release — полная оптимизация. Кэш DXIL
// на диске (cache/shaders/<хэш>.dxil): ключ — файл, точка входа, профиль, defines, флаги и отпечаток всех файлов
// Shaders\ (имена, размеры, время правки), так что правка любого шейдера или include пересобирает всё
class ShaderCompiler
{
public:
	static ShaderCompiler& instance();

	// file — путь к HLSL (Shaders\x.ps), entry — точка входа, profile — vs_6_6 / ps_6_6 / gs_6_6 / cs_6_6,
	// defines — "A=1,B" (как у DMShader::addShaderPassFromFile). Ошибки — в лог и shader-error.txt
	bool compile( const std::string& file, const std::string& entry, const std::string& profile, const std::string& defines,
				  std::vector<uint8_t>& bytecode );
	// Профиль стадии для compile
	static std::string profile( SRVType type );
	// Сколько шейдеров скомпилировано и сколько взято из кэша с начала работы — строка в лог
	void logSummary() const;

private:
	ShaderCompiler() = default;
	bool initialize();
	uint64_t sourcesStamp() const;

	IDxcCompiler3* m_compiler = nullptr;
	IDxcUtils* m_utils = nullptr;
	IDxcIncludeHandler* m_includeHandler = nullptr;
	bool m_initialized = false;
	bool m_failed = false;
	uint64_t m_stamp = 0;
	uint32_t m_compiled = 0;
	uint32_t m_fromCache = 0;
};
