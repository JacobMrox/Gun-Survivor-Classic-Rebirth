#pragma once

#include "d3d8.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

struct VideoState {
	AVFormatContext* pFormatCtx = nullptr;
	AVCodecContext* pCodecCtx = nullptr;
	AVFrame* pFrame = nullptr;
	AVFrame* pFrameRGB = nullptr;
	struct SwsContext* sws_ctx = nullptr;
	int videoStream = -1;
	uint8_t* buffer = nullptr;
	bool isPlaying = false;
};

// Declare the global instance (defined in AviFfmpegHook.cpp)
extern VideoState g_Video;

// D3DLOCKED_RECT comes from d3d9types.h (d3d8.h -> d3d8types.hpp -> d3d9.h).
struct IDirect3DTexture9;
extern IDirect3DTexture9* g_MovieTex;

// Draw helper used across translation units
void DrawTexturedQuad(LPDIRECT3DDEVICE8 dev, LPDIRECT3DTEXTURE8 tex, float x, float y, float w, float h);
