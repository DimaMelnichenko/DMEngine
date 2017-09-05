#pragma once

/////////////
// LINKING //
/////////////
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

//////////////
// INCLUDES //
//////////////
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <stdio.h>
#include "DMObject.h"


class DMSound :	public DMObject
{
private:
	struct WaveHeaderType
	{
		char chunkId[4];
		unsigned long chunkSize;
		char format[4];
		char subChunkId[4];
		unsigned long subChunkSize;
		unsigned short audioFormat;
		unsigned short numChannels;
		unsigned long sampleRate;
		unsigned long bytesPerSecond;
		unsigned short blockAlign;
		unsigned short bitsPerSample;
		char dataChunkId[4];
		unsigned long dataSize;
	};

public:
	DMSound(  );
	~DMSound();

	bool Initialize( HWND );
	void Shutdown( );

private:
	bool InitializeDirectSound( HWND );
	void ShutdownDirectSound( );

	bool LoadWaveFile( char*, IDirectSoundBuffer8**, IDirectSound3DBuffer** );
	void ShutdownWaveFile( IDirectSoundBuffer8**, IDirectSound3DBuffer** );

	bool PlayWaveFile( );

private:
	IDirectSound8* m_DirectSound;
	IDirectSoundBuffer* m_primaryBuffer;

	IDirectSound3DListener* m_listener;
	IDirectSoundBuffer8* m_secondaryBuffer1;

	IDirectSound3DBuffer* m_secondary3DBuffer1;
};

