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
#include "survivor_fix/survivor_pc.h"
#include "survivor_fix/log.h"
#include "survivor_fix/config.h"
#include "resource.h"
#include "d3dx8tex.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

extern HMODULE g_hWrapperModule;

namespace {
IDirect3DTexture8* gEnemySurfaceTextures[3] = {};
LONG gShadowTraceCount = 0;
bool gShadowStencilStarted = false;
bool gShadowStencilPending = false;

struct ShadowVertex { float x, y, z; };
std::vector<ShadowVertex> gShadowCasterTriangles;

ShadowVertex TransformPoint(const ShadowVertex& p, const D3DMATRIX& m)
{
	const float* a = reinterpret_cast<const float*>(&m);
	return {
		p.x * a[0] + p.y * a[4] + p.z * a[8]  + a[12],
		p.x * a[1] + p.y * a[5] + p.z * a[9]  + a[13],
		p.x * a[2] + p.y * a[6] + p.z * a[10] + a[14]
	};
}

void AppendShadowPrism(std::vector<ShadowVertex>& out, ShadowVertex a, ShadowVertex b, ShadowVertex c)
{
	// Survivor's animated model coordinates become more negative above the
	// floor. Extend toward +Y through the stage; the depth/stencil test clips
	// the volume to whatever 3D stage surface actually receives it.
	ShadowVertex ae = a, be = b, ce = c;
	ae.y += 100000.0f; be.y += 100000.0f; ce.y += 100000.0f;
	const ShadowVertex v[] = {
		a,b,c, ce,be,ae,
		a,b,be, a,be,ae,
		b,c,ce, b,ce,be,
		c,a,ae, c,ae,ce
	};
	out.insert(out.end(), std::begin(v), std::end(v));
}

bool AddDynamicShadowVolume(IDirect3DDevice8* device, UINT startVertex, UINT primitiveCount)
{
	static LONG diagnostic = 0;
	const bool log = InterlockedIncrement(&diagnostic) <= 4;
	// Alpha-blended cards (glass shards, smoke, flames, foliage, etc.) do not
	// describe their visible shape in geometry. Treating their rectangular quad
	// as an opaque caster produces a square shadow, so leave those draws out of
	// the volume. Opaque and alpha-tested character materials remain eligible.
	DWORD alphaBlend = FALSE;
	device->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphaBlend);
	if (alphaBlend) {
		if (log) sf::Log("DynamicShadow volume rejected: alpha-blended caster");
		return false;
	}
	IDirect3DVertexBuffer8* vb = nullptr;
	UINT stride = 0;
	const HRESULT streamHr = device->GetStreamSource(0, &vb, &stride);
	if (FAILED(streamHr) || !vb || stride < 12) {
		if (log) sf::Log("DynamicShadow volume rejected: stream hr=%08lX vb=%p stride=%u", streamHr, vb, stride);
		if (vb) vb->Release();
		return false;
	}
	BYTE* data = nullptr;
	const HRESULT lockHr = vb->Lock(0, 0, &data, D3DLOCK_READONLY);
	if (FAILED(lockHr) || !data) {
		if (log) sf::Log("DynamicShadow volume rejected: VB lock hr=%08lX", lockHr);
		vb->Release(); return false;
	}
	D3DMATRIX world{};
	device->GetTransform(D3DTS_WORLD, &world);
	std::vector<ShadowVertex> volume;
	volume.reserve(static_cast<size_t>(primitiveCount) * 3);
	for (UINT i = 0; i < primitiveCount; ++i) {
		const BYTE* tri = data + static_cast<size_t>(startVertex + i * 3) * stride;
		ShadowVertex a{}, b{}, c{};
		std::memcpy(&a, tri, 12); std::memcpy(&b, tri + stride, 12); std::memcpy(&c, tri + stride * 2, 12);
		volume.push_back(TransformPoint(a, world));
		volume.push_back(TransformPoint(b, world));
		volume.push_back(TransformPoint(c, world));
	}
	if (log && !volume.empty()) {
		float minX=volume[0].x,minY=volume[0].y,minZ=volume[0].z,maxX=minX,maxY=minY,maxZ=minZ;
		// Only the first six vertices are the original/extruded first triangle;
		// report them explicitly to identify axis direction and double transforms.
		for (size_t i=0; i<volume.size(); ++i) {
			minX=(std::min)(minX,volume[i].x); minY=(std::min)(minY,volume[i].y); minZ=(std::min)(minZ,volume[i].z);
			maxX=(std::max)(maxX,volume[i].x); maxY=(std::max)(maxY,volume[i].y); maxZ=(std::max)(maxZ,volume[i].z);
		}
		sf::Log("DynamicShadow bounds: worldT=%.1f,%.1f,%.1f bounds=%.1f..%.1f %.1f..%.1f %.1f..%.1f first=%.1f,%.1f,%.1f",
			reinterpret_cast<const float*>(&world)[12], reinterpret_cast<const float*>(&world)[13], reinterpret_cast<const float*>(&world)[14],
			minX,maxX,minY,maxY,minZ,maxZ,volume[0].x,volume[0].y,volume[0].z);
	}
	vb->Unlock(); vb->Release();
	if (volume.empty()) return false;
	gShadowCasterTriangles.insert(gShadowCasterTriangles.end(), volume.begin(), volume.end());
	gShadowStencilPending = true;
	return true;

	// Legacy per-triangle prism renderer retained below temporarily for source
	// comparison; coherent silhouette submission now occurs at flush time.
	DWORD state = 0;
	const HRESULT stateHr = device->CreateStateBlock(D3DSBT_ALL, &state);
	if (FAILED(stateHr)) { if (log) sf::Log("DynamicShadow volume rejected: state block hr=%08lX", stateHr); return false; }
	if (!gShadowStencilStarted) {
		IDirect3DSurface8* depth = nullptr;
		D3DSURFACE_DESC8 desc{};
		if (SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth) { depth->GetDesc(&desc); depth->Release(); }
		const HRESULT clearHr = device->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);
		if (log) sf::Log("DynamicShadow stencil: format=%u clear=%08lX triangles=%u volumeVertices=%u",
			static_cast<unsigned>(desc.Format), clearHr, primitiveCount, static_cast<unsigned>(volume.size()));
		if (FAILED(clearHr)) {
			device->DeleteStateBlock(state);
			return false;
		}
		gShadowStencilStarted = true;
	}
	D3DMATRIX identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
	device->SetTransform(D3DTS_WORLD, &identity);
	device->SetVertexShader(D3DFVF_XYZ);
	device->SetPixelShader(0);
	device->SetTexture(0, nullptr);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_ZENABLE, TRUE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
	device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	device->SetRenderState(D3DRS_STENCILREF, 0);
	device->SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
	device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
	device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
	device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(volume.size() / 3), volume.data(), sizeof(ShadowVertex));
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
	device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_DECRSAT);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(volume.size() / 3), volume.data(), sizeof(ShadowVertex));
	device->ApplyStateBlock(state);
	device->DeleteStateBlock(state);
	gShadowStencilPending = true;
	if (log) sf::Log("DynamicShadow volume submitted; overlay pending");
	return true;
}

struct ShadowPointKey {
	int x, y, z;
	bool operator<(const ShadowPointKey& r) const {
		if (x != r.x) return x < r.x; if (y != r.y) return y < r.y; return z < r.z;
	}
};
ShadowPointKey PointKey(const ShadowVertex& v) {
	// Animated seams can differ by a tiny fraction after bone transforms.
	return {static_cast<int>(std::lround(v.x * 0.25f)), static_cast<int>(std::lround(v.y * 0.25f)),
	        static_cast<int>(std::lround(v.z * 0.25f))};
}
struct ShadowEdgeKey {
	ShadowPointKey a, b;
	bool operator<(const ShadowEdgeKey& r) const { return a < r.a || (!(r.a < a) && b < r.b); }
};
struct ShadowEdgeInfo { int front = 0, back = 0; ShadowVertex a{}, b{}; };

bool SubmitCoherentShadowVolume(IDirect3DDevice8* device)
{
	if (gShadowCasterTriangles.empty()) return false;
	std::map<ShadowEdgeKey, ShadowEdgeInfo> edges;
	std::vector<ShadowVertex> volume;
	volume.reserve(gShadowCasterTriangles.size() * 4);
	auto extrude = [](ShadowVertex v) { v.y += 100000.0f; return v; };
	auto edge = [&](ShadowVertex a, ShadowVertex b, bool front) {
		ShadowPointKey ka = PointKey(a), kb = PointKey(b);
		bool reverse = kb < ka;
		ShadowEdgeKey key{reverse ? kb : ka, reverse ? ka : kb};
		auto& e = edges[key];
		if (front) { ++e.front; e.a = a; e.b = b; } else ++e.back;
	};
	unsigned frontFaces = 0;
	for (size_t i = 0; i + 2 < gShadowCasterTriangles.size(); i += 3) {
		const ShadowVertex a=gShadowCasterTriangles[i], b=gShadowCasterTriangles[i+1], c=gShadowCasterTriangles[i+2];
		const float abx=b.x-a.x, abz=b.z-a.z, acx=c.x-a.x, acz=c.z-a.z;
		const float normalY = abz*acx - abx*acz;
		const bool front = normalY < 0.0f;
		edge(a,b,front); edge(b,c,front); edge(c,a,front);
		if (front) {
			++frontFaces;
			const ShadowVertex ae=extrude(a), be=extrude(b), ce=extrude(c);
			const ShadowVertex caps[] = {a,b,c, ce,be,ae};
			volume.insert(volume.end(), std::begin(caps), std::end(caps));
		}
	}
	unsigned silhouetteEdges = 0;
	for (const auto& item : edges) {
		const auto& e = item.second;
		if (!e.front || (!e.back && e.front > 1)) continue;
		++silhouetteEdges;
		const ShadowVertex ae=extrude(e.a), be=extrude(e.b);
		const ShadowVertex side[] = {e.a,e.b,be, e.a,be,ae};
		volume.insert(volume.end(), std::begin(side), std::end(side));
	}
	gShadowCasterTriangles.clear();
	if (volume.empty()) return false;
	DWORD state=0; if (FAILED(device->CreateStateBlock(D3DSBT_ALL,&state))) return false;
	const HRESULT clearHr=device->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1.0f,0);
	if (FAILED(clearHr)) { device->DeleteStateBlock(state); return false; }
	D3DMATRIX identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
	device->SetTransform(D3DTS_WORLD,&identity); device->SetVertexShader(D3DFVF_XYZ); device->SetPixelShader(0);
	device->SetTexture(0,nullptr); device->SetRenderState(D3DRS_LIGHTING,FALSE);
	device->SetRenderState(D3DRS_ZENABLE,TRUE); device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); device->SetRenderState(D3DRS_COLORWRITEENABLE,0);
	device->SetRenderState(D3DRS_STENCILENABLE,TRUE); device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
	device->SetRenderState(D3DRS_STENCILMASK,0xFFFFFFFF); device->SetRenderState(D3DRS_STENCILWRITEMASK,0xFFFFFFFF);
	device->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP); device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP);
	// Capped z-fail is stable even when the camera/near plane intersects a long
	// enemy shadow volume; z-pass cancelled in those common first-person views.
	device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CCW); device->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_INCR);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(volume.size()/3),volume.data(),sizeof(ShadowVertex));
	device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW); device->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_DECR);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(volume.size()/3),volume.data(),sizeof(ShadowVertex));
	device->ApplyStateBlock(state); device->DeleteStateBlock(state);
	static LONG logged=0; if (InterlockedIncrement(&logged)<=4)
		sf::Log("DynamicShadow coherent volume: frontFaces=%u silhouetteEdges=%u vertices=%u",frontFaces,silhouetteEdges,static_cast<unsigned>(volume.size()));
	return true;
}

void FlushDynamicShadowStencil(IDirect3DDevice8* device)
{
	static LONG flushDiagnostic = 0;
	if (!gShadowStencilPending || SurvivorPc_IsDynamicShadow()) return;
	gShadowStencilPending = false;
	gShadowStencilStarted = false;
	if (!SubmitCoherentShadowVolume(device)) return;
	DWORD state = 0;
	if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &state))) return;
	D3DVIEWPORT8 vp{}; device->GetViewport(&vp);
	struct V { float x,y,z,rhw; DWORD color; };
	const float l = static_cast<float>(vp.X), t = static_cast<float>(vp.Y);
	const float r = l + vp.Width, b = t + vp.Height;
	const V q[] = {{l,t,0,1,0x58000000},{r,t,0,1,0x58000000},{l,b,0,1,0x58000000},
	               {l,b,0,1,0x58000000},{r,t,0,1,0x58000000},{r,b,0,1,0x58000000}};
	device->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	device->SetPixelShader(0); device->SetTexture(0, nullptr);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_NOTEQUAL);
	device->SetRenderState(D3DRS_STENCILREF, 0);
	device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(V));
	if (InterlockedIncrement(&flushDiagnostic) <= 4) sf::Log("DynamicShadow stencil overlay submitted");
	device->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.0f, 0);
	device->ApplyStateBlock(state); device->DeleteStateBlock(state);
}

void TraceDynamicShadowDraw(IDirect3DDevice8* device, const char* kind, UINT vertices, UINT primitives)
{
	if (!device || InterlockedIncrement(&gShadowTraceCount) > 24) return;
	DWORD shader = 0;
	UINT stride = 0, base = 0;
	IDirect3DVertexBuffer8* vb = nullptr;
	IDirect3DIndexBuffer8* ib = nullptr;
	device->GetVertexShader(&shader);
	device->GetStreamSource(0, &vb, &stride);
	device->GetIndices(&ib, &base);
	sf::Log("DynamicShadow trace: %s vertices=%u primitives=%u FVF/shader=%08lX stride=%u VB=%p IB=%p base=%u",
		kind, vertices, primitives, shader, stride, vb, ib, base);
	if (vb) vb->Release();
	if (ib) ib->Release();
}

void ApplySmoothModelLighting(IDirect3DDevice8* device)
{
	if (!device || !SurvivorPc_SmoothModelLighting() || !SurvivorPc_IsModelDraw())
		return;
	// Apply at the animated draw, rather than only when the engine initially
	// enables lighting. Bone transforms can contain scale, so normalization is
	// required after each world transform; Gouraud interpolation preserves the
	// model's vertex normals instead of presenting a flat face colour.
	device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
	device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
}

struct DynamicShadowState {
	DWORD lighting, alphaBlend, alphaTest, srcBlend, dstBlend, zWrite, zBias, cull, factor;
	DWORD colorOp, colorArg1, alphaOp, alphaArg1;
	IDirect3DBaseTexture8* texture;
	D3DMATRIX world;
};

bool BeginDynamicShadowDraw(IDirect3DDevice8* device, DynamicShadowState& s)
{
	if (!device || !SurvivorPc_IsDynamicShadow()) return false;
	device->GetRenderState(D3DRS_LIGHTING, &s.lighting);
	device->GetRenderState(D3DRS_ALPHABLENDENABLE, &s.alphaBlend);
	device->GetRenderState(D3DRS_ALPHATESTENABLE, &s.alphaTest);
	device->GetRenderState(D3DRS_SRCBLEND, &s.srcBlend);
	device->GetRenderState(D3DRS_DESTBLEND, &s.dstBlend);
	device->GetRenderState(D3DRS_ZWRITEENABLE, &s.zWrite);
	device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(D3DRS_ZBIAS), &s.zBias);
	device->GetRenderState(D3DRS_CULLMODE, &s.cull);
	device->GetRenderState(D3DRS_TEXTUREFACTOR, &s.factor);
	device->GetTextureStageState(0, D3DTSS_COLOROP, &s.colorOp);
	device->GetTextureStageState(0, D3DTSS_COLORARG1, &s.colorArg1);
	device->GetTextureStageState(0, D3DTSS_ALPHAOP, &s.alphaOp);
	device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &s.alphaArg1);
	s.texture = nullptr;
	device->GetTexture(0, &s.texture);
	device->GetTransform(D3DTS_WORLD, &s.world);
	D3DMATRIX projected = {};
	const float* a = reinterpret_cast<const float*>(&s.world);
	float* o = reinterpret_cast<float*>(&projected);
	const float floorY = SurvivorPc_DynamicShadowFloorY();
	for (int row = 0; row < 4; ++row) {
		o[row * 4 + 0] = a[row * 4 + 0];
		o[row * 4 + 1] = a[row * 4 + 3] * floorY;
		o[row * 4 + 2] = a[row * 4 + 2];
		o[row * 4 + 3] = a[row * 4 + 3];
	}
	device->SetTransform(D3DTS_WORLD, &projected);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(D3DRS_ZBIAS), 1);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, 0x58000000);
	device->SetTexture(0, nullptr);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
	return true;
}

void EndDynamicShadowDraw(IDirect3DDevice8* device, DynamicShadowState& s)
{
	device->SetTransform(D3DTS_WORLD, &s.world);
	device->SetTexture(0, s.texture);
	if (s.texture) s.texture->Release();
	device->SetRenderState(D3DRS_LIGHTING, s.lighting);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, s.alphaBlend);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, s.alphaTest);
	device->SetRenderState(D3DRS_SRCBLEND, s.srcBlend);
	device->SetRenderState(D3DRS_DESTBLEND, s.dstBlend);
	device->SetRenderState(D3DRS_ZWRITEENABLE, s.zWrite);
	device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(D3DRS_ZBIAS), s.zBias);
	device->SetRenderState(D3DRS_CULLMODE, s.cull);
	device->SetRenderState(D3DRS_TEXTUREFACTOR, s.factor);
	device->SetTextureStageState(0, D3DTSS_COLOROP, s.colorOp);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, s.colorArg1);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, s.alphaOp);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, s.alphaArg1);
}

struct HsvColour { float h, s, v; };

HsvColour RgbToHsv(BYTE r8, BYTE g8, BYTE b8) {
    const float r = r8 / 255.0f, g = g8 / 255.0f, b = b8 / 255.0f;
    const float hi = (std::max)({r, g, b});
    const float lo = (std::min)({r, g, b});
    const float d = hi - lo;
    float h = 0.0f;
    if (d > 0.0001f) {
        if (hi == r) h = std::fmod((g - b) / d, 6.0f);
        else if (hi == g) h = ((b - r) / d) + 2.0f;
        else h = ((r - g) / d) + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    return {h, hi > 0.0f ? d / hi : 0.0f, hi};
}

DWORD HsvToArgb(float h, float s, float v, BYTE alpha) {
    const float scaled = h * 6.0f;
    const int sector = static_cast<int>(std::floor(scaled)) % 6;
    const float f = scaled - std::floor(scaled);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    float r = v, g = t, b = p;
    switch (sector) {
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    }
    const auto byte = [](float x) { return static_cast<BYTE>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (DWORD(alpha) << 24) | (DWORD(byte(r)) << 16) | (DWORD(byte(g)) << 8) | byte(b);
}

bool HueShiftBloodTexture(IDirect3DTexture8* texture, DWORD rgb) {
    if (!texture || rgb == 0x00FFFFFFu) return true;
    const HsvColour target = RgbToHsv(BYTE(rgb >> 16), BYTE(rgb >> 8), BYTE(rgb));
    D3DSURFACE_DESC8 desc = {};
    D3DLOCKED_RECT lock = {};
    if (FAILED(texture->GetLevelDesc(0, &desc))) return false;
    const bool argb32 = desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8;
    const bool rgb16 = desc.Format == D3DFMT_A4R4G4B4 || desc.Format == D3DFMT_A1R5G5B5 ||
                       desc.Format == D3DFMT_X1R5G5B5 || desc.Format == D3DFMT_R5G6B5;
    if ((!argb32 && !rgb16) || FAILED(texture->LockRect(0, &lock, nullptr, 0))) {
        sf::Log("BloodColor: cannot hue-shift %ux%u format=%u", desc.Width, desc.Height,
                static_cast<unsigned>(desc.Format));
        return false;
    }
    bool changed = false;
    auto shift = [&](BYTE r, BYTE g, BYTE b, BYTE a) {
        const HsvColour source = RgbToHsv(r, g, b);
        const float rawDifference = std::fabs(source.h - target.h);
        const float hueDifference = (std::min)(rawDifference, 1.0f - rawDifference);
        // Makes the operation idempotent. Some native effect textures are
        // uploaded again into an existing D3D object; checking only the pointer
        // left those later uploads red, while blindly recolouring every draw
        // repeatedly darkened the artwork.
        if (source.s > 0.05f && hueDifference < 0.02f &&
            std::fabs(source.s - target.s) < 0.08f)
            return (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | b;
        changed = true;
        return HsvToArgb(target.h, target.s, source.v * target.v, a);
    };
    for (UINT y = 0; y < desc.Height; ++y) {
        BYTE* bytes = static_cast<BYTE*>(lock.pBits) + y * lock.Pitch;
        if (argb32) {
            auto* row = reinterpret_cast<DWORD*>(bytes);
            for (UINT x = 0; x < desc.Width; ++x) {
                const DWORD p = row[x];
                const BYTE a = desc.Format == D3DFMT_X8R8G8B8 ? 0xFF : BYTE(p >> 24);
                row[x] = shift(BYTE(p >> 16), BYTE(p >> 8), BYTE(p), a);
            }
        } else {
            auto* row = reinterpret_cast<WORD*>(bytes);
            for (UINT x = 0; x < desc.Width; ++x) {
                const WORD p = row[x];
                BYTE r, g, b, a;
                if (desc.Format == D3DFMT_A4R4G4B4) {
                    a = BYTE(((p >> 12) & 0xF) * 17); r = BYTE(((p >> 8) & 0xF) * 17);
                    g = BYTE(((p >> 4) & 0xF) * 17); b = BYTE((p & 0xF) * 17);
                } else if (desc.Format == D3DFMT_R5G6B5) {
                    a = 0xFF; r = BYTE(((p >> 11) & 0x1F) * 255 / 31);
                    g = BYTE(((p >> 5) & 0x3F) * 255 / 63); b = BYTE((p & 0x1F) * 255 / 31);
                } else {
                    a = desc.Format == D3DFMT_A1R5G5B5 ? ((p & 0x8000) ? 0xFF : 0) : 0xFF;
                    r = BYTE(((p >> 10) & 0x1F) * 255 / 31); g = BYTE(((p >> 5) & 0x1F) * 255 / 31);
                    b = BYTE((p & 0x1F) * 255 / 31);
                }
                const DWORD q = shift(r, g, b, a);
                if (desc.Format == D3DFMT_A4R4G4B4) {
                    row[x] = WORD(((q >> 28) << 12) | (((q >> 20) & 0xF) << 8) |
                                  (((q >> 12) & 0xF) << 4) | ((q >> 4) & 0xF));
                } else if (desc.Format == D3DFMT_R5G6B5) {
                    row[x] = WORD((((q >> 19) & 0x1F) << 11) | (((q >> 10) & 0x3F) << 5) | ((q >> 3) & 0x1F));
                } else {
                    row[x] = WORD((desc.Format == D3DFMT_A1R5G5B5 && (q >> 24) >= 0x80 ? 0x8000 : 0) |
                                  (((q >> 19) & 0x1F) << 10) | (((q >> 11) & 0x1F) << 5) | ((q >> 3) & 0x1F));
                }
            }
        }
    }
    texture->UnlockRect(0);
    if (changed)
        sf::Log("BloodColor: hue-shifted %ux%u format=%u to %u,%u,%u", desc.Width, desc.Height,
                static_cast<unsigned>(desc.Format), BYTE(rgb >> 16), BYTE(rgb >> 8), BYTE(rgb));
    return true;
}

IDirect3DTexture8* EnemySurfaceTexture(IDirect3DDevice8* device, int surface) {
    if (surface < 1 || surface > 3) return nullptr;
    IDirect3DTexture8*& texture = gEnemySurfaceTextures[surface - 1];
    if (texture) return texture;
    const char* names[] = {"ps1_shadow.png", "ps1_bloodx1.png", "ps1_bloodx2.png"};
    char loosePath[MAX_PATH] = {};
    if (GetModuleFileNameA(g_hWrapperModule, loosePath, MAX_PATH)) {
        char* slash = strrchr(loosePath, '\\');
        if (slash) {
            *slash = '\0';
            const size_t folderLength = strlen(loosePath);
            sprintf_s(loosePath + folderLength, MAX_PATH - folderLength,
                "\\FIXRES\\%s", names[surface - 1]);
            // Loose files remain user/mod overrides. Only fall back to the
            // copy embedded in d3d8.dll when the override is absent or bad.
            if (SUCCEEDED(D3DXCreateTextureFromFileExA(device, loosePath, D3DX_DEFAULT, D3DX_DEFAULT,
                1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0,
                nullptr, nullptr, &texture)) && texture) {
                if (surface > 1) HueShiftBloodTexture(texture, sf::g_config.bloodColor);
                return texture;
            }
        }
    }
    const int ids[] = {IDR_PS1_SHADOW_PNG, IDR_PS1_BLOODX1_PNG, IDR_PS1_BLOODX2_PNG};
    HRSRC info = FindResourceA(g_hWrapperModule, MAKEINTRESOURCEA(ids[surface - 1]), RT_RCDATA);
    HGLOBAL data = info ? LoadResource(g_hWrapperModule, info) : nullptr;
    const void* bytes = data ? LockResource(data) : nullptr;
    const DWORD size = info ? SizeofResource(g_hWrapperModule, info) : 0;
    if (bytes && size) {
        D3DXCreateTextureFromFileInMemoryEx(device, bytes, size, D3DX_DEFAULT, D3DX_DEFAULT,
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0,
            nullptr, nullptr, &texture);
        if (surface > 1) HueShiftBloodTexture(texture, sf::g_config.bloodColor);
    }
    return texture;
}
}

// Native BLOOD1/BLOOD2 textures are created and uploaded by the game rather
// than by EnemySurfaceTexture.  Their wrapper calls this after the upload.
void SurvivorHueShiftBloodTexture(IDirect3DTexture8* texture) {
    HueShiftBloodTexture(texture, sf::g_config.bloodColor);
}

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
	if (SurvivorPc_IsSurvivorExe() && *reinterpret_cast<int*>(0x005F3DAC) == 0x2D)
	{
		static DWORD lastState = 0, lastValue = 0;
		if (lastState != State || lastValue != Value)
		{
			sf::Log("SetRenderState 2D: %d=%lu", State, Value);
			lastState = State;
			lastValue = Value;
		}
		if (State == D3DRS_LIGHTING)
			Value = 1;
		if (State == D3DRS_AMBIENT)
			Value = 0x00FFFFFF;
	}
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
	if (pLight && SurvivorPc_IsSurvivorExe() && pLight->Type == D3DLIGHT_SPOT &&
		pLight->Theta <= 0.0001f && pLight->Phi <= 0.0001f)
	{
		// gun_bio.exe 00467230 creates its sole scene light as an overhead spot
		// (0,90000,0), direction (0,-1,0), but leaves both cone angles zero.
		// Such a cone has no useful coverage, so the constant 0x646464 ambient
		// term dominates and models appear flat. The stage HMD/ROOMENV data does
		// not supply a replacement light. Preserve the authored colour/direction
		// as a directional light so every room has a valid diffuse contribution.
		D3DLIGHT8 fixed = *pLight;
		fixed.Type = D3DLIGHT_DIRECTIONAL;
		return ProxyInterface->SetLight(Index, &fixed);
	}
	return ProxyInterface->SetLight(Index, pLight);
}

HRESULT m_IDirect3DDevice8::SetMaterial(CONST D3DMATERIAL8 *pMaterial)
{
	if (pMaterial && SurvivorPc_IsSurvivorExe() && *reinterpret_cast<int*>(0x005F3DAC) == 0x2D)
	{
		D3DMATERIAL8 boosted = *pMaterial;
		boosted.Ambient.r = boosted.Ambient.r < 0.13f ? 0.13f : boosted.Ambient.r;
		boosted.Ambient.g = boosted.Ambient.g < 0.13f ? 0.13f : boosted.Ambient.g;
		boosted.Ambient.b = boosted.Ambient.b < 0.13f ? 0.13f : boosted.Ambient.b;
		sf::Log("SetMaterial 2D: ambient %.3f %.3f %.3f -> %.3f %.3f %.3f",
			pMaterial->Ambient.r, pMaterial->Ambient.g, pMaterial->Ambient.b,
			boosted.Ambient.r, boosted.Ambient.g, boosted.Ambient.b);
		return ProxyInterface->SetMaterial(&boosted);
	}
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
	ApplySmoothModelLighting(ProxyInterface);
	if (SurvivorPc_IsDynamicShadow()) {
		DynamicShadowState shadowState = {};
		if (!BeginDynamicShadowDraw(ProxyInterface, shadowState)) return D3D_OK;
		const HRESULT hr = ProxyInterface->DrawIndexedPrimitive(Type, MinVertexIndex, NumVertices, startIndex, primCount);
		EndDynamicShadowDraw(ProxyInterface, shadowState);
		return hr;
	}
	DynamicShadowState s = {}; const bool shadow = BeginDynamicShadowDraw(ProxyInterface, s);
	const HRESULT hr = ProxyInterface->DrawIndexedPrimitive(Type, MinVertexIndex, NumVertices, startIndex, primCount);
	if (shadow) EndDynamicShadowDraw(ProxyInterface, s);
	return hr;
}

HRESULT m_IDirect3DDevice8::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinIndex, UINT NumVertices, UINT PrimitiveCount, CONST void *pIndexData, D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
	ApplySmoothModelLighting(ProxyInterface);
	if (SurvivorPc_IsDynamicShadow()) {
		DynamicShadowState shadowState = {};
		if (!BeginDynamicShadowDraw(ProxyInterface, shadowState)) return D3D_OK;
		const HRESULT hr = ProxyInterface->DrawIndexedPrimitiveUP(PrimitiveType, MinIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
		EndDynamicShadowDraw(ProxyInterface, shadowState);
		return hr;
	}
	DynamicShadowState s = {}; const bool shadow = BeginDynamicShadowDraw(ProxyInterface, s);
	const HRESULT hr = ProxyInterface->DrawIndexedPrimitiveUP(PrimitiveType, MinIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
	if (shadow) EndDynamicShadowDraw(ProxyInterface, s);
	return hr;
}

HRESULT m_IDirect3DDevice8::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	ApplySmoothModelLighting(ProxyInterface);
	if (SurvivorPc_IsDynamicShadow()) {
		DynamicShadowState shadowState = {};
		if (!BeginDynamicShadowDraw(ProxyInterface, shadowState)) return D3D_OK;
		const HRESULT hr = ProxyInterface->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		EndDynamicShadowDraw(ProxyInterface, shadowState);
		return hr;
	}
	FlushDynamicShadowStencil(ProxyInterface);
	DynamicShadowState shadowState = {}; const bool dynamicShadow = BeginDynamicShadowDraw(ProxyInterface, shadowState);
	if (dynamicShadow) {
		const HRESULT hr = ProxyInterface->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		EndDynamicShadowDraw(ProxyInterface, shadowState);
		return hr;
	}
	const int enemySurface = SurvivorPc_TakeEnemySurfaceDraw();
	if (enemySurface == 1 && SurvivorPc_Hide2DShadows())
		return D3D_OK;
	if (enemySurface)
	{
		// Bind the exact repaired shadow/pool PNG only for this verified native
		// draw. This retains linear alpha without global file/upload tagging.
		DWORD blend, src, dst, alphaTest, alphaFunc, alphaRef, factor, cop, carg1, carg2, aop, aarg1, aarg2;
		IDirect3DBaseTexture8* oldTexture = nullptr;
		ProxyInterface->GetTexture(0, &oldTexture);
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
		// Impact/headshot BLOOD1/BLOOD2 draws are identified synchronously by
		// DrawEffectDetour (surface id 4). Recolour the texture that is actually
		// bound, once, so this works regardless of how the game loaded or padded
		// the BMP and retains the native dense alpha rendering.
		if (enemySurface == 4 && oldTexture && oldTexture->GetType() == D3DRTYPE_TEXTURE)
			HueShiftBloodTexture(static_cast<IDirect3DTexture8*>(oldTexture), sf::g_config.bloodColor);
		ProxyInterface->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		ProxyInterface->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		ProxyInterface->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		// The repaired textures use a continuous alpha ramp; alpha testing was
		// the source of the hard rings/squares in earlier attempts.
		ProxyInterface->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		if (auto* replacement = EnemySurfaceTexture(ProxyInterface, enemySurface))
			ProxyInterface->SetTexture(0, replacement);
		if (enemySurface == 1) {
			ProxyInterface->SetRenderState(D3DRS_TEXTUREFACTOR, 0x80404040);
			ProxyInterface->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		} else {
			// Blood textures are hue-shifted once when loaded.  Draw their real RGB
			// here; applying another fixed-function tint would flatten the result.
			ProxyInterface->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			ProxyInterface->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		}
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		ProxyInterface->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		const HRESULT hr = ProxyInterface->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		ProxyInterface->SetTexture(0, oldTexture);
		if (oldTexture) oldTexture->Release();
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
	ApplySmoothModelLighting(ProxyInterface);
	if (SurvivorPc_IsSurvivorExe() && *reinterpret_cast<int*>(0x005F3DAC) == 0x2D) {
		ProxyInterface->SetRenderState(D3DRS_LIGHTING, TRUE);
		ProxyInterface->SetRenderState(D3DRS_AMBIENT, 0x00323232);
	}
	if (SurvivorPc_IsDynamicShadow()) {
		DynamicShadowState shadowState = {};
		if (!BeginDynamicShadowDraw(ProxyInterface, shadowState)) return D3D_OK;
		const HRESULT hr = ProxyInterface->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
		EndDynamicShadowDraw(ProxyInterface, shadowState);
		return hr;
	}
	DynamicShadowState s = {}; const bool shadow = BeginDynamicShadowDraw(ProxyInterface, s);
	const HRESULT hr = ProxyInterface->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
	if (shadow) EndDynamicShadowDraw(ProxyInterface, s);
	return hr;
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
	if (SurvivorPc_IsSurvivorExe() && *reinterpret_cast<int*>(0x005F3DAC) == 0x2D)
	{
		// Scene 0x2D is pitch black on PC because textures are modulated by black
		// vertex diffuse. Force TFACTOR + TEXTURE so a constant bright level is
		// added on top of every texture, matching PS1 brightness.
		if (Stage == 0 && Type == D3DTSS_COLORARG1)
			Value = D3DTA_TFACTOR;
		if (Stage == 0 && Type == D3DTSS_COLORARG2)
			Value = D3DTA_TEXTURE;
		if (Stage == 0 && Type == D3DTSS_COLOROP)
			Value = D3DTOP_ADD;
	}
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
	if (SurvivorPc_IsSurvivorExe() && *reinterpret_cast<int*>(0x005F3DAC) == 0x2D)
	{
		// Scene 0x2D clears to pure black on PC; force a dim background so the
		// room is at least faintly visible without the light switch.
		Color = D3DCOLOR_ARGB(0xFF, 0x10, 0x10, 0x10);
	}
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
