/**
* Copyright (C) 2020 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include "globals.h"
#define D3D9_TYPES_ALLOWED
#include <d3d9.h>
#include <intrin.h>
#include "d3d8.h"
#include "survivor_pc.h"
#include "survivor_fix/log.h"

// somewhere global


HRESULT m_IDirect3DDevice8::QueryInterface(REFIID riid, LPVOID *ppvObj)
{
	if ((riid == IID_IDirect3DDevice8 || riid == IID_IUnknown) && ppvObj)
	{
		AddRef();

		*ppvObj = this;

		return S_OK;
	}

	HRESULT hr = ProxyInterface->QueryInterface(riid, ppvObj);

	if (SUCCEEDED(hr))
	{
		genericQueryInterface(riid, ppvObj, this);
	}

	return hr;
}

ULONG m_IDirect3DDevice8::AddRef()
{
	return ProxyInterface->AddRef();
}

ULONG m_IDirect3DDevice8::Release()
{
	ULONG ref = ProxyInterface->Release();

	if (ref == 0)
	{
		delete this;
	}

	return ref;
}

//HRESULT m_IDirect3DDevice8::Reset(D3DPRESENT_PARAMETERS8 *pPresentationParameters)
//{
//	return ProxyInterface->Reset(pPresentationParameters);
//}

//HRESULT m_IDirect3DDevice8::EndScene()
//{
//	return ProxyInterface->EndScene();
//}

void m_IDirect3DDevice8::SetCursorPosition(THIS_ UINT XScreenSpace, UINT YScreenSpace, DWORD Flags)
{
	return ProxyInterface->SetCursorPosition(XScreenSpace, YScreenSpace, Flags);
}

HRESULT m_IDirect3DDevice8::SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8 *pCursorBitmap)
{
	if (pCursorBitmap)
	{
		pCursorBitmap = static_cast<m_IDirect3DSurface8 *>(pCursorBitmap)->GetProxyInterface();
	}

	return ProxyInterface->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap);
}

BOOL m_IDirect3DDevice8::ShowCursor(BOOL bShow)
{
	return ProxyInterface->ShowCursor(bShow);
}

HRESULT m_IDirect3DDevice8::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS8 *pPresentationParameters, IDirect3DSwapChain8 **ppSwapChain)
{
	HRESULT hr = ProxyInterface->CreateAdditionalSwapChain(pPresentationParameters, ppSwapChain);

	if (SUCCEEDED(hr) && ppSwapChain)
	{
		*ppSwapChain = new m_IDirect3DSwapChain8(*ppSwapChain, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CreateCubeTexture(THIS_ UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture8** ppCubeTexture)
{
	HRESULT hr = ProxyInterface->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture);

	if (SUCCEEDED(hr) && ppCubeTexture)
	{
		*ppCubeTexture = new m_IDirect3DCubeTexture8(*ppCubeTexture, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CreateDepthStencilSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8** ppSurface)
{
	HRESULT hr = ProxyInterface->CreateDepthStencilSurface(Width, Height, Format, MultiSample, ppSurface);

	if (SUCCEEDED(hr) && ppSurface)
	{
		*ppSurface = new m_IDirect3DSurface8(*ppSurface, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CreateIndexBuffer(THIS_ UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8** ppIndexBuffer)
{
	HRESULT hr = ProxyInterface->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer);

	if (SUCCEEDED(hr) && ppIndexBuffer)
	{
		*ppIndexBuffer = new m_IDirect3DIndexBuffer8(*ppIndexBuffer, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CreateRenderTarget(THIS_ UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, BOOL Lockable, IDirect3DSurface8** ppSurface)
{
	HRESULT hr = ProxyInterface->CreateRenderTarget(Width, Height, Format, MultiSample, Lockable, ppSurface);

	if (SUCCEEDED(hr) && ppSurface)
	{
		*ppSurface = new m_IDirect3DSurface8(*ppSurface, this);
	}

	return hr;
}

/*HRESULT m_IDirect3DDevice8::CreateTexture(THIS_ UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture)
{
	HRESULT hr = ProxyInterface->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture);

	if (SUCCEEDED(hr) && ppTexture)
	{
		*ppTexture = new m_IDirect3DTexture8(*ppTexture, this);
	}

	return hr;
}*/


// Move this OUT of the global area and into the member function area
void m_IDirect3DDevice8::RenderMovie(UINT screenW, UINT screenH)
{
	if (!g_Video.isPlaying || !g_Video.buffer) return;
	if (!g_Video.pCodecCtx || !g_Video.pFormatCtx) return;

	// 1. Create the texture (uses ProxyInterface member)
	if (g_Video.resetTexture && g_MovieTex) {
		g_MovieTex->Release();
		g_MovieTex = nullptr;
		g_Video.resetTexture = false;
	}
	if (!g_MovieTex) {
		ProxyInterface->CreateTexture(g_Video.pCodecCtx->width, g_Video.pCodecCtx->height,
			1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, (IDirect3DTexture8**)&g_MovieTex);
	}

	// 2. Decode at the source frame rate. The old code decoded every EndScene,
	// which made 30/38 fps movies run at the display rate.
	const double elapsedMs = static_cast<double>(GetTickCount64() - g_Video.playbackStartedAtMs);
	for (int updates = 0; !g_Video.eof && elapsedMs + 0.1 >= g_Video.nextFrameAtMs && updates < 4; ++updates) {
		AVPacket packet{};
		bool gotFrame = false;
		while (!gotFrame) {
			if (av_read_frame(g_Video.pFormatCtx, &packet) < 0) {
				g_Video.eof = true;
				break;
			}
			if (packet.stream_index == g_Video.videoStream) {
				avcodec_send_packet(g_Video.pCodecCtx, &packet);
				gotFrame = avcodec_receive_frame(g_Video.pCodecCtx, g_Video.pFrame) == 0;
			}
			av_packet_unref(&packet);
		}
		if (!gotFrame) break;
				sws_scale(g_Video.sws_ctx, (uint8_t const* const*)g_Video.pFrame->data,
					g_Video.pFrame->linesize, 0, g_Video.pCodecCtx->height,
					g_Video.pFrameRGB->data, g_Video.pFrameRGB->linesize);

				// 3. Upload to D3D Texture
				D3DLOCKED_RECT rect;
				if (SUCCEEDED(g_MovieTex->LockRect(0, &rect, NULL, D3DLOCK_DISCARD))) {
					uint8_t* dest = (uint8_t*)rect.pBits;
					uint8_t* src = g_Video.buffer;
					int srcStride = g_Video.pFrameRGB->linesize[0];

					for (int y = 0; y < (int)g_Video.pCodecCtx->height; y++) {
						auto* dstRow = reinterpret_cast<uint32_t*>(dest + (y * rect.Pitch));
						const auto* srcRow = reinterpret_cast<const uint32_t*>(src + (y * srcStride));
						for (int x = 0; x < g_Video.pCodecCtx->width; ++x) dstRow[x] = srcRow[x] | 0xFF000000u;
					}
					g_MovieTex->UnlockRect(0);
				}
		g_Video.nextFrameAtMs += g_Video.frameIntervalMs;
	}

	// 4. Preserve the source aspect ratio. Capcom30.avi is 320x240; filling a
	// 16:9 backbuffer crops its sides instead of showing the complete logo.
	if (g_MovieTex && screenW && screenH) {
		static UINT lastMovieScreenW = 0, lastMovieScreenH = 0;
		if (screenW != lastMovieScreenW || screenH != lastMovieScreenH) {
			lastMovieScreenW = screenW;
			lastMovieScreenH = screenH;
			sf::Log("FFmpeg movie render: video %dx%d, back buffer %ux%u", g_Video.pCodecCtx->width, g_Video.pCodecCtx->height, screenW, screenH);
		}
		// The game leaves its 640x480 scene viewport active here.  XYZRHW movie
		// vertices are clipped to that inherited viewport, which cuts off the
		// right of 800x600 windowed movies (including the Capcom logo).
		D3DVIEWPORT8 oldViewport{};
		const bool restoreViewport = SUCCEEDED(ProxyInterface->GetViewport(&oldViewport));
		D3DVIEWPORT8 movieViewport{};
		movieViewport.X = 0;
		movieViewport.Y = 0;
		movieViewport.Width = screenW;
		movieViewport.Height = screenH;
		movieViewport.MinZ = 0.0f;
		movieViewport.MaxZ = 1.0f;
		ProxyInterface->SetViewport(&movieViewport);

		DWORD oldScissor = FALSE;
		const bool restoreScissor = SUCCEEDED(ProxyInterface->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor));
		ProxyInterface->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

		const float videoAspect = static_cast<float>(g_Video.pCodecCtx->width) / g_Video.pCodecCtx->height;
		const float screenAspect = static_cast<float>(screenW) / screenH;
		float w = static_cast<float>(screenW), h = static_cast<float>(screenH), x = 0.0f, y = 0.0f;
		if (screenAspect > videoAspect) {
			w = h * videoAspect;
			x = (static_cast<float>(screenW) - w) * 0.5f;
		} else {
			h = w / videoAspect;
			y = (static_cast<float>(screenH) - h) * 0.5f;
		}
		DrawTexturedQuad(ProxyInterface, (LPDIRECT3DTEXTURE8)g_MovieTex, x, y, w, h);

		if (restoreScissor) ProxyInterface->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);
		if (restoreViewport) ProxyInterface->SetViewport(&oldViewport);
	}
}

HRESULT m_IDirect3DDevice8::CreateVertexBuffer(THIS_ UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8** ppVertexBuffer)
{
	HRESULT hr = ProxyInterface->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer);

	if (SUCCEEDED(hr) && ppVertexBuffer)
	{
		*ppVertexBuffer = new m_IDirect3DVertexBuffer8(*ppVertexBuffer, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CreateVolumeTexture(THIS_ UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture8** ppVolumeTexture)
{
	HRESULT hr = ProxyInterface->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture);

	if (SUCCEEDED(hr) && ppVolumeTexture)
	{
		*ppVolumeTexture = new m_IDirect3DVolumeTexture8(*ppVolumeTexture, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::BeginStateBlock()
{
	return ProxyInterface->BeginStateBlock();
}

HRESULT m_IDirect3DDevice8::CreateStateBlock(THIS_ D3DSTATEBLOCKTYPE Type, DWORD* pToken)
{
	return ProxyInterface->CreateStateBlock(Type, pToken);
}

HRESULT m_IDirect3DDevice8::ApplyStateBlock(THIS_ DWORD Token)
{
	return ProxyInterface->ApplyStateBlock(Token);
}

HRESULT m_IDirect3DDevice8::CaptureStateBlock(THIS_ DWORD Token)
{
	return ProxyInterface->CaptureStateBlock(Token);
}

HRESULT m_IDirect3DDevice8::DeleteStateBlock(THIS_ DWORD Token)
{
	return ProxyInterface->DeleteStateBlock(Token);
}

HRESULT m_IDirect3DDevice8::EndStateBlock(THIS_ DWORD* pToken)
{
	return ProxyInterface->EndStateBlock(pToken);
}

HRESULT m_IDirect3DDevice8::GetClipStatus(D3DCLIPSTATUS8 *pClipStatus)
{
	return ProxyInterface->GetClipStatus(pClipStatus);
}

HRESULT m_IDirect3DDevice8::GetDisplayMode(THIS_ D3DDISPLAYMODE* pMode)
{
	return ProxyInterface->GetDisplayMode(pMode);
}

HRESULT m_IDirect3DDevice8::GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue)
{
	return ProxyInterface->GetRenderState(State, pValue);
}

HRESULT m_IDirect3DDevice8::GetRenderTarget(THIS_ IDirect3DSurface8** ppRenderTarget)
{
	HRESULT hr = ProxyInterface->GetRenderTarget(ppRenderTarget);

	if (SUCCEEDED(hr) && ppRenderTarget)
	{
		*ppRenderTarget = ProxyAddressLookupTable->FindAddress<m_IDirect3DSurface8>(*ppRenderTarget);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix)
{
	return ProxyInterface->GetTransform(State, pMatrix);
}

HRESULT m_IDirect3DDevice8::SetClipStatus(CONST D3DCLIPSTATUS8 *pClipStatus)
{
	return ProxyInterface->SetClipStatus(pClipStatus);
}

HRESULT m_IDirect3DDevice8::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
	SurvivorPc_OnSetRenderState(_ReturnAddress(), State, Value);
	if (SurvivorPc_SmoothModelLighting())
	{
		// The PC model stream carries fixed-point-sized normals. Without this,
		// fixed-function diffuse terms saturate and characters look flat-lit.
		if (State == D3DRS_NORMALIZENORMALS)
			Value = TRUE;
		if (State == D3DRS_LIGHTING && Value)
		{
			ProxyInterface->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
			ProxyInterface->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
		}
	}
	return ProxyInterface->SetRenderState(State, Value);
}

HRESULT m_IDirect3DDevice8::SetRenderTarget(THIS_ IDirect3DSurface8* pRenderTarget, IDirect3DSurface8* pNewZStencil)
{
	if (pRenderTarget)
	{
		pRenderTarget = static_cast<m_IDirect3DSurface8 *>(pRenderTarget)->GetProxyInterface();
	}

	if (pNewZStencil)
	{
		pNewZStencil = static_cast<m_IDirect3DSurface8 *>(pNewZStencil)->GetProxyInterface();
	}

	return ProxyInterface->SetRenderTarget(pRenderTarget, pNewZStencil);
}

HRESULT m_IDirect3DDevice8::SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix)
{
	return ProxyInterface->SetTransform(State, pMatrix);
}

void m_IDirect3DDevice8::GetGammaRamp(THIS_ D3DGAMMARAMP* pRamp)
{
	ProxyInterface->GetGammaRamp(pRamp);
}

void m_IDirect3DDevice8::SetGammaRamp(THIS_ DWORD Flags, CONST D3DGAMMARAMP* pRamp)
{
	ProxyInterface->SetGammaRamp(Flags, pRamp);
}

HRESULT m_IDirect3DDevice8::DeletePatch(UINT Handle)
{
	return ProxyInterface->DeletePatch(Handle);
}

HRESULT m_IDirect3DDevice8::DrawRectPatch(UINT Handle, CONST float *pNumSegs, CONST D3DRECTPATCH_INFO *pRectPatchInfo)
{
	return ProxyInterface->DrawRectPatch(Handle, pNumSegs, pRectPatchInfo);
}

HRESULT m_IDirect3DDevice8::DrawTriPatch(UINT Handle, CONST float *pNumSegs, CONST D3DTRIPATCH_INFO *pTriPatchInfo)
{
	return ProxyInterface->DrawTriPatch(Handle, pNumSegs, pTriPatchInfo);
}

HRESULT m_IDirect3DDevice8::GetIndices(THIS_ IDirect3DIndexBuffer8** ppIndexData, UINT* pBaseVertexIndex)
{
	HRESULT hr = ProxyInterface->GetIndices(ppIndexData, pBaseVertexIndex);

	if (SUCCEEDED(hr) && ppIndexData)
	{
		*ppIndexData = ProxyAddressLookupTable->FindAddress<m_IDirect3DIndexBuffer8>(*ppIndexData);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::SetIndices(THIS_ IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex)
{
	if (pIndexData)
	{
		pIndexData = static_cast<m_IDirect3DIndexBuffer8 *>(pIndexData)->GetProxyInterface();
	}

	return ProxyInterface->SetIndices(pIndexData, BaseVertexIndex);
}

UINT m_IDirect3DDevice8::GetAvailableTextureMem()
{
	return ProxyInterface->GetAvailableTextureMem();
}

HRESULT m_IDirect3DDevice8::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters)
{
	return ProxyInterface->GetCreationParameters(pParameters);
}

HRESULT m_IDirect3DDevice8::GetDeviceCaps(D3DCAPS8 *pCaps)
{
	return ProxyInterface->GetDeviceCaps(pCaps);
}

HRESULT m_IDirect3DDevice8::GetDirect3D(IDirect3D8 **ppD3D9)
{
	if (!ppD3D9)
	{
		return D3DERR_INVALIDCALL;
	}

	m_pD3D->AddRef();

	*ppD3D9 = m_pD3D;

	return D3D_OK;
}

HRESULT m_IDirect3DDevice8::GetRasterStatus(THIS_ D3DRASTER_STATUS* pRasterStatus)
{
	return ProxyInterface->GetRasterStatus(pRasterStatus);
}

HRESULT m_IDirect3DDevice8::GetLight(DWORD Index, D3DLIGHT8 *pLight)
{
	return ProxyInterface->GetLight(Index, pLight);
}

HRESULT m_IDirect3DDevice8::GetLightEnable(DWORD Index, BOOL *pEnable)
{
	return ProxyInterface->GetLightEnable(Index, pEnable);
}

HRESULT m_IDirect3DDevice8::GetMaterial(D3DMATERIAL8 *pMaterial)
{
	return ProxyInterface->GetMaterial(pMaterial);
}

HRESULT m_IDirect3DDevice8::LightEnable(DWORD LightIndex, BOOL bEnable)
{
	return ProxyInterface->LightEnable(LightIndex, bEnable);
}

HRESULT m_IDirect3DDevice8::SetLight(DWORD Index, CONST D3DLIGHT8 *pLight)
{

	return ProxyInterface->SetLight(Index, pLight);
}

HRESULT m_IDirect3DDevice8::SetMaterial(CONST D3DMATERIAL8 *pMaterial)
{
	return ProxyInterface->SetMaterial(pMaterial);
}

HRESULT m_IDirect3DDevice8::MultiplyTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix)
{
	return ProxyInterface->MultiplyTransform(State, pMatrix);
}

HRESULT m_IDirect3DDevice8::ProcessVertices(THIS_ UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer8* pDestBuffer, DWORD Flags)
{
	if (pDestBuffer)
	{
		pDestBuffer = static_cast<m_IDirect3DVertexBuffer8 *>(pDestBuffer)->GetProxyInterface();
	}

	return ProxyInterface->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, Flags);
}

HRESULT m_IDirect3DDevice8::TestCooperativeLevel()
{
	return ProxyInterface->TestCooperativeLevel();
}

HRESULT m_IDirect3DDevice8::GetCurrentTexturePalette(UINT *pPaletteNumber)
{
	return ProxyInterface->GetCurrentTexturePalette(pPaletteNumber);
}

HRESULT m_IDirect3DDevice8::GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY *pEntries)
{
	return ProxyInterface->GetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT m_IDirect3DDevice8::SetCurrentTexturePalette(UINT PaletteNumber)
{
	return ProxyInterface->SetCurrentTexturePalette(PaletteNumber);
}

HRESULT m_IDirect3DDevice8::SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY *pEntries)
{
	return ProxyInterface->SetPaletteEntries(PaletteNumber, pEntries);
}

HRESULT m_IDirect3DDevice8::CreatePixelShader(THIS_ CONST DWORD* pFunction, DWORD* pHandle)
{
	return ProxyInterface->CreatePixelShader(pFunction, pHandle);
}

HRESULT m_IDirect3DDevice8::GetPixelShader(THIS_ DWORD* pHandle)
{
	return ProxyInterface->GetPixelShader(pHandle);
}

HRESULT m_IDirect3DDevice8::SetPixelShader(THIS_ DWORD Handle)
{
	return ProxyInterface->SetPixelShader(Handle);
}

HRESULT m_IDirect3DDevice8::DeletePixelShader(THIS_ DWORD Handle)
{
	return ProxyInterface->DeletePixelShader(Handle);
}

HRESULT m_IDirect3DDevice8::GetPixelShaderFunction(THIS_ DWORD Handle, void* pData, DWORD* pSizeOfData)
{
	return ProxyInterface->GetPixelShaderFunction(Handle, pData, pSizeOfData);
}

//HRESULT m_IDirect3DDevice8::Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, HWND hDestWindowOverride, CONST RGNDATA *pDirtyRegion)
//{
//	return ProxyInterface->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
//}

HRESULT m_IDirect3DDevice8::DrawIndexedPrimitive(THIS_ D3DPRIMITIVETYPE Type, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
	return ProxyInterface->DrawIndexedPrimitive(Type, MinVertexIndex, NumVertices, startIndex, primCount);
}

HRESULT m_IDirect3DDevice8::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinIndex, UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData, D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	return ProxyInterface->DrawIndexedPrimitiveUP(PrimitiveType, MinIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT m_IDirect3DDevice8::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	if (SurvivorPc_TakeShadowDraw())
	{
		// PS1 semi-transparency mode 0 for the enemy shadow (survivor_pc.cpp, Ps1ShadowBlend).
		DWORD blend, src, dst, alphaTest, alphaFunc, alphaRef, factor, cop, carg1, carg2, aop, aarg1, aarg2;
		ProxyInterface->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
		ProxyInterface->GetRenderState(D3DRS_SRCBLEND, &src);
		ProxyInterface->GetRenderState(D3DRS_DESTBLEND, &dst);
		ProxyInterface->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest);
		ProxyInterface->GetRenderState(D3DRS_ALPHAFUNC, &alphaFunc);
		ProxyInterface->GetRenderState(D3DRS_ALPHAREF, &alphaRef);
		ProxyInterface->GetRenderState(D3DRS_TEXTUREFACTOR, &factor);
		ProxyInterface->GetTextureStageState(0, D3DTSS_COLOROP, &cop);
		ProxyInterface->GetTextureStageState(0, D3DTSS_COLORARG1, &carg1);
		ProxyInterface->GetTextureStageState(0, D3DTSS_COLORARG2, &carg2);
		ProxyInterface->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop);
		ProxyInterface->GetTextureStageState(0, D3DTSS_ALPHAARG1, &aarg1);
		ProxyInterface->GetTextureStageState(0, D3DTSS_ALPHAARG2, &aarg2);
		ProxyInterface->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		ProxyInterface->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		ProxyInterface->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		ProxyInterface->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
		ProxyInterface->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
		ProxyInterface->SetRenderState(D3DRS_ALPHAREF, 1);
		ProxyInterface->SetRenderState(D3DRS_TEXTUREFACTOR, 0x80404040);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
		const HRESULT hr = ProxyInterface->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		ProxyInterface->SetRenderState(D3DRS_ALPHABLENDENABLE, blend);
		ProxyInterface->SetRenderState(D3DRS_SRCBLEND, src);
		ProxyInterface->SetRenderState(D3DRS_DESTBLEND, dst);
		ProxyInterface->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest);
		ProxyInterface->SetRenderState(D3DRS_ALPHAFUNC, alphaFunc);
		ProxyInterface->SetRenderState(D3DRS_ALPHAREF, alphaRef);
		ProxyInterface->SetRenderState(D3DRS_TEXTUREFACTOR, factor);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLOROP, cop);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG1, carg1);
		ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG2, carg2);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAOP, aop);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAARG1, aarg1);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAARG2, aarg2);
		return hr;
	}
	return ProxyInterface->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
}


HRESULT m_IDirect3DDevice8::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	return ProxyInterface->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
}

HRESULT m_IDirect3DDevice8::BeginScene()
{
	return ProxyInterface->BeginScene();
}

HRESULT m_IDirect3DDevice8::GetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer8** ppStreamData, UINT* pStride)
{
	HRESULT hr = ProxyInterface->GetStreamSource(StreamNumber, ppStreamData, pStride);

	if (SUCCEEDED(hr) && ppStreamData)
	{
		*ppStreamData = ProxyAddressLookupTable->FindAddress<m_IDirect3DVertexBuffer8>(*ppStreamData);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::SetStreamSource(THIS_ UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride)
{
	if (pStreamData)
	{
		pStreamData = static_cast<m_IDirect3DVertexBuffer8 *>(pStreamData)->GetProxyInterface();
	}

	return ProxyInterface->SetStreamSource(StreamNumber, pStreamData, Stride);
}

HRESULT m_IDirect3DDevice8::GetBackBuffer(THIS_ UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8** ppBackBuffer)
{
	HRESULT hr = ProxyInterface->GetBackBuffer(iBackBuffer, Type, ppBackBuffer);

	if (SUCCEEDED(hr) && ppBackBuffer)
	{
		*ppBackBuffer = ProxyAddressLookupTable->FindAddress<m_IDirect3DSurface8>(*ppBackBuffer);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface)
{
	HRESULT hr = ProxyInterface->GetDepthStencilSurface(ppZStencilSurface);

	if (SUCCEEDED(hr) && ppZStencilSurface)
	{
		*ppZStencilSurface = ProxyAddressLookupTable->FindAddress<m_IDirect3DSurface8>(*ppZStencilSurface);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture)
{
	HRESULT hr = ProxyInterface->GetTexture(Stage, ppTexture);

	if (SUCCEEDED(hr) && ppTexture && *ppTexture)
	{
		switch ((*ppTexture)->GetType())
		{
		case D3DRTYPE_TEXTURE:
			*ppTexture = ProxyAddressLookupTable->FindAddress<m_IDirect3DTexture8>(*ppTexture);
			break;
		case D3DRTYPE_VOLUMETEXTURE:
			*ppTexture = ProxyAddressLookupTable->FindAddress<m_IDirect3DVolumeTexture8>(*ppTexture);
			break;
		case D3DRTYPE_CUBETEXTURE:
			*ppTexture = ProxyAddressLookupTable->FindAddress<m_IDirect3DCubeTexture8>(*ppTexture);
			break;
		default:
			return D3DERR_INVALIDCALL;
		}
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
{
	return ProxyInterface->GetTextureStageState(Stage, Type, pValue);
}


HRESULT m_IDirect3DDevice8::SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture)
{
	if (pTexture)
	{
		switch (pTexture->GetType())
		{
		case D3DRTYPE_TEXTURE:
			pTexture = static_cast<m_IDirect3DTexture8 *>(pTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_VOLUMETEXTURE:
			pTexture = static_cast<m_IDirect3DVolumeTexture8 *>(pTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_CUBETEXTURE:
			pTexture = static_cast<m_IDirect3DCubeTexture8 *>(pTexture)->GetProxyInterface();
			break;
		default:
			return D3DERR_INVALIDCALL;
		}
	}

	return ProxyInterface->SetTexture(Stage, pTexture);
}

HRESULT m_IDirect3DDevice8::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
	return ProxyInterface->SetTextureStageState(Stage, Type, Value);
}

HRESULT m_IDirect3DDevice8::UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture, IDirect3DBaseTexture8 *pDestinationTexture)
{
	if (pSourceTexture)
	{
		switch (pSourceTexture->GetType())
		{
		case D3DRTYPE_TEXTURE:
			pSourceTexture = static_cast<m_IDirect3DTexture8 *>(pSourceTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_VOLUMETEXTURE:
			pSourceTexture = static_cast<m_IDirect3DVolumeTexture8 *>(pSourceTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_CUBETEXTURE:
			pSourceTexture = static_cast<m_IDirect3DCubeTexture8 *>(pSourceTexture)->GetProxyInterface();
			break;
		default:
			return D3DERR_INVALIDCALL;
		}
	}
	if (pDestinationTexture)
	{
		switch (pDestinationTexture->GetType())
		{
		case D3DRTYPE_TEXTURE:
			pDestinationTexture = static_cast<m_IDirect3DTexture8 *>(pDestinationTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_VOLUMETEXTURE:
			pDestinationTexture = static_cast<m_IDirect3DVolumeTexture8 *>(pDestinationTexture)->GetProxyInterface();
			break;
		case D3DRTYPE_CUBETEXTURE:
			pDestinationTexture = static_cast<m_IDirect3DCubeTexture8 *>(pDestinationTexture)->GetProxyInterface();
			break;
		default:
			return D3DERR_INVALIDCALL;
		}
	}

	return ProxyInterface->UpdateTexture(pSourceTexture, pDestinationTexture);
}

HRESULT m_IDirect3DDevice8::ValidateDevice(DWORD *pNumPasses)
{
	return ProxyInterface->ValidateDevice(pNumPasses);
}

HRESULT m_IDirect3DDevice8::GetClipPlane(DWORD Index, float *pPlane)
{
	return ProxyInterface->GetClipPlane(Index, pPlane);
}

HRESULT m_IDirect3DDevice8::SetClipPlane(DWORD Index, CONST float *pPlane)
{
	return ProxyInterface->SetClipPlane(Index, pPlane);
}

HRESULT m_IDirect3DDevice8::Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
	return ProxyInterface->Clear(Count, pRects, Flags, Color, Z, Stencil);
}

HRESULT m_IDirect3DDevice8::GetViewport(D3DVIEWPORT8 *pViewport)
{
	return ProxyInterface->GetViewport(pViewport);
}

//HRESULT m_IDirect3DDevice8::SetViewport(CONST D3DVIEWPORT8 *pViewport)
//{
//	return ProxyInterface->SetViewport(pViewport);
//}

HRESULT m_IDirect3DDevice8::CreateVertexShader(THIS_ CONST DWORD* pDeclaration, CONST DWORD* pFunction, DWORD* pHandle, DWORD Usage)
{
	return ProxyInterface->CreateVertexShader(pDeclaration, pFunction, pHandle, Usage);
}

HRESULT m_IDirect3DDevice8::GetVertexShader(THIS_ DWORD* pHandle)
{
	return ProxyInterface->GetVertexShader(pHandle);
}

HRESULT m_IDirect3DDevice8::SetVertexShader(THIS_ DWORD Handle)
{
	return ProxyInterface->SetVertexShader(Handle);
}

HRESULT m_IDirect3DDevice8::DeleteVertexShader(THIS_ DWORD Handle)
{
	return ProxyInterface->DeleteVertexShader(Handle);
}

HRESULT m_IDirect3DDevice8::GetVertexShaderDeclaration(THIS_ DWORD Handle, void* pData, DWORD* pSizeOfData)
{
	return ProxyInterface->GetVertexShaderDeclaration(Handle, pData, pSizeOfData);
}

HRESULT m_IDirect3DDevice8::GetVertexShaderFunction(THIS_ DWORD Handle, void* pData, DWORD* pSizeOfData)
{
	return ProxyInterface->GetVertexShaderFunction(Handle, pData, pSizeOfData);
}

HRESULT m_IDirect3DDevice8::SetPixelShaderConstant(THIS_ DWORD Register, CONST void* pConstantData, DWORD ConstantCount)
{
	return ProxyInterface->SetPixelShaderConstant(Register, pConstantData, ConstantCount);
}

HRESULT m_IDirect3DDevice8::GetPixelShaderConstant(THIS_ DWORD Register, void* pConstantData, DWORD ConstantCount)
{
	return ProxyInterface->GetPixelShaderConstant(Register, pConstantData, ConstantCount);
}

HRESULT m_IDirect3DDevice8::SetVertexShaderConstant(THIS_ DWORD Register, CONST void* pConstantData, DWORD ConstantCount)
{
	return ProxyInterface->SetVertexShaderConstant(Register, pConstantData, ConstantCount);
}

HRESULT m_IDirect3DDevice8::GetVertexShaderConstant(THIS_ DWORD Register, void* pConstantData, DWORD ConstantCount)
{
	return ProxyInterface->GetVertexShaderConstant(Register, pConstantData, ConstantCount);
}

HRESULT m_IDirect3DDevice8::ResourceManagerDiscardBytes(THIS_ DWORD Bytes)
{
	return ProxyInterface->ResourceManagerDiscardBytes(Bytes);
}

/*HRESULT m_IDirect3DDevice8::CreateImageSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** ppSurface)
{
	HRESULT hr = ProxyInterface->CreateImageSurface(Width, Height, Format, ppSurface);

	if (SUCCEEDED(hr) && ppSurface)
	{
		*ppSurface = new m_IDirect3DSurface8(*ppSurface, this);
	}

	return hr;
}*/

HRESULT m_IDirect3DDevice8::CreateImageSurface(THIS_ UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** ppSurface)
{
	HRESULT hr = ProxyInterface->CreateImageSurface(Width, Height, Format, ppSurface);

	if (SUCCEEDED(hr) && ppSurface)
	{
		*ppSurface = new m_IDirect3DSurface8(*ppSurface, this);
	}

	return hr;
}

HRESULT m_IDirect3DDevice8::CopyRects(THIS_ IDirect3DSurface8* pSourceSurface, CONST RECT* pSourceRectsArray, UINT cRects, IDirect3DSurface8* pDestinationSurface, CONST POINT* pDestPointsArray)
{
	if (pSourceSurface)
	{
		pSourceSurface = static_cast<m_IDirect3DSurface8 *>(pSourceSurface)->GetProxyInterface();
	}

	if (pDestinationSurface)
	{
		pDestinationSurface = static_cast<m_IDirect3DSurface8 *>(pDestinationSurface)->GetProxyInterface();
	}

	return ProxyInterface->CopyRects(pSourceSurface, pSourceRectsArray, cRects, pDestinationSurface, pDestPointsArray);
}

HRESULT m_IDirect3DDevice8::GetFrontBuffer(THIS_ IDirect3DSurface8* pDestSurface)
{
	if (pDestSurface)
	{
		pDestSurface = static_cast<m_IDirect3DSurface8 *>(pDestSurface)->GetProxyInterface();
	}

	return ProxyInterface->GetFrontBuffer(pDestSurface);
}

HRESULT m_IDirect3DDevice8::GetInfo(THIS_ DWORD DevInfoID, void* pDevInfoStruct, DWORD DevInfoStructSize)
{
	return ProxyInterface->GetInfo(DevInfoID, pDevInfoStruct, DevInfoStructSize);
}
