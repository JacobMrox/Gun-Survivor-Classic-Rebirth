#include <cstdio>
#include <cstdlib>
using UINT=unsigned;using DWORD=unsigned long;using HRESULT=long;
constexpr HRESULT S_OK=0,E_FAIL=static_cast<HRESULT>(0x80004005L);
#define SUCCEEDED(v) ((v)>=0)
#define FAILED(v) ((v)<0)
constexpr int D3DSBT_ALL=1,D3DRS_ALPHABLENDENABLE=2,FALSE=0,D3DTSS_COLOROP=3,D3DTOP_SELECTARG1=4,D3DTSS_ALPHAOP=5,D3DTOP_DISABLE=6;
struct D3DVIEWPORT8{};struct D3DSURFACE_DESC8{UINT Width=320,Height=240;};
struct IDirect3DSurface8{void Release(){}};
HRESULT gLastPresentResult=S_OK;bool gScreenFxDrawSucceeded=true;
struct IDirect3DDevice8{
 HRESULT begin=S_OK,draw=S_OK,end=S_OK,present=S_OK;bool effect=true;int presents=0,restores=0;
 HRESULT CreateStateBlock(int,DWORD* b){*b=1;return S_OK;}void GetViewport(D3DVIEWPORT8*){}
 HRESULT BeginScene(){return begin;}HRESULT EndScene(){return end;}
 void SetRenderState(int,int){}void SetTexture(int,void*){}void SetTextureStageState(int,int,int){}
 HRESULT ApplyStateBlock(DWORD){++restores;return S_OK;}void DeleteStateBlock(DWORD){}void SetViewport(D3DVIEWPORT8*){}
 HRESULT Present(void*,void*,void*,void*){++presents;gLastPresentResult=present;gScreenFxDrawSucceeded=effect;return S_OK;}
};
IDirect3DDevice8* gDev=nullptr;IDirect3DDevice8* gWrapper=nullptr;void* gFrame=reinterpret_cast<void*>(1);bool gFrameValid=true,gHolding=false;
bool BackBuffer(IDirect3DDevice8*,IDirect3DSurface8** b,D3DSURFACE_DESC8*){static IDirect3DSurface8 s;*b=&s;return true;}
void BeginDraw(IDirect3DDevice8*,UINT,UINT){}
HRESULT Quad(IDirect3DDevice8* d,float,float,float,float,DWORD){return d->draw;}
#include "held_frame_test.inc"
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 IDirect3DDevice8 d;gDev=gWrapper=&d;
 d.begin=E_FAIL;require(SurvivorPc_PresentHeldFrame()==0&&d.presents==0,"failed BeginScene must not report or present a frame");
 d={};d.draw=E_FAIL;require(SurvivorPc_PresentHeldFrame()==0&&d.presents==0,"failed held draw must propagate");
 d={};d.end=E_FAIL;require(SurvivorPc_PresentHeldFrame()==0&&d.presents==0,"failed EndScene must propagate");
 d={};d.present=E_FAIL;require(SurvivorPc_PresentHeldFrame()==0,"native Present failure must survive the wrapper S_OK mask");
 d={};d.effect=false;require(SurvivorPc_PresentHeldFrame()==0,"failed effect pass must propagate");
 d={};require(SurvivorPc_PresentHeldFrame()==1&&d.presents==1&&!gHolding&&d.restores==1,"successful frame restores state and reports success");
 std::puts("PASS: held-frame failures and success");
}
