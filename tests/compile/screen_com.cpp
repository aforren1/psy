// Compile check: ysp/screen.h's own declarations of the presentation and
// DirectComposition interfaces, against the Windows SDK's.
//
// For each interface the header calls, a spy class derives from the SDK's
// C++ interface and records which method ran. The test calls every slot the
// header uses through the header's C table and checks that the intended
// method ran with the arguments it was given, and that methods returning an
// aggregate (SystemInterruptTime, LUID) come back through the hidden return
// pointer as the header declares them. It also checks the IIDs and the
// layout of the structs the header declares. A wrong slot is a failure
// here, not a crash on a rig.
//
// MinGW-w64 has dcomp.h but no Presentation.h, so there it checks the
// DirectComposition part only. Off Windows it does nothing and returns 0.
#define YSP_SCREEN_IMPLEMENTATION
#include "ysp/screen.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if defined(_WIN32) && defined(YSCR__DXGI)
#include <dcomp.h>
#include <d3d10.h>   /* ID3D10Multithread */
#if __has_include(<Presentation.h>) && defined(_MSC_VER)
#include <Presentation.h>
#define HAVE_PRESENTATION 1
#endif

static int g_failures = 0;
static int g_hit = -1;
static uint64_t g_arg = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "screen_com: FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

#define SPY_IUNK \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { g_hit = 0; return E_NOINTERFACE; } \
    ULONG STDMETHODCALLTYPE AddRef() override { g_hit = 1; return 1; } \
    ULONG STDMETHODCALLTYPE Release() override { g_hit = 2; return 1; }

template <class C, class S> static C* as_c(S* s) { return reinterpret_cast<C*>(s); }

// --- DirectComposition -----------------------------------------------------

struct SpyDevice : IDCompositionDevice {
    SPY_IUNK
    HRESULT STDMETHODCALLTYPE Commit() override { g_hit = 3; return S_OK; }
    HRESULT STDMETHODCALLTYPE WaitForCommitCompletion() override { g_hit = 4; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DCOMPOSITION_FRAME_STATISTICS*) override { g_hit = 5; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateTargetForHwnd(HWND h, BOOL topmost, IDCompositionTarget**) override { g_hit = 6; g_arg = (uint64_t)(uintptr_t)h + (uint64_t)topmost; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateVisual(IDCompositionVisual**) override { g_hit = 7; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSurface(UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionSurface**) override { g_hit = 8; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateVirtualSurface(UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, IDCompositionVirtualSurface**) override { g_hit = 9; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSurfaceFromHandle(HANDLE h, IUnknown**) override { g_hit = 10; g_arg = (uint64_t)(uintptr_t)h; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSurfaceFromHwnd(HWND, IUnknown**) override { g_hit = 11; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateTranslateTransform(IDCompositionTranslateTransform**) override { g_hit = 12; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateScaleTransform(IDCompositionScaleTransform**) override { g_hit = 13; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateRotateTransform(IDCompositionRotateTransform**) override { g_hit = 14; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateSkewTransform(IDCompositionSkewTransform**) override { g_hit = 15; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateMatrixTransform(IDCompositionMatrixTransform**) override { g_hit = 16; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateTransformGroup(IDCompositionTransform**, UINT, IDCompositionTransform**) override { g_hit = 17; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateTranslateTransform3D(IDCompositionTranslateTransform3D**) override { g_hit = 18; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateScaleTransform3D(IDCompositionScaleTransform3D**) override { g_hit = 19; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateRotateTransform3D(IDCompositionRotateTransform3D**) override { g_hit = 20; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateMatrixTransform3D(IDCompositionMatrixTransform3D**) override { g_hit = 21; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateTransform3DGroup(IDCompositionTransform3D**, UINT, IDCompositionTransform3D**) override { g_hit = 22; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffectGroup(IDCompositionEffectGroup**) override { g_hit = 23; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateRectangleClip(IDCompositionRectangleClip**) override { g_hit = 24; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateAnimation(IDCompositionAnimation**) override { g_hit = 25; return S_OK; }
    HRESULT STDMETHODCALLTYPE CheckDeviceState(BOOL*) override { g_hit = 26; return S_OK; }
};

struct SpyTarget : IDCompositionTarget {
    SPY_IUNK
    HRESULT STDMETHODCALLTYPE SetRoot(IDCompositionVisual* v) override { g_hit = 3; g_arg = (uint64_t)(uintptr_t)v; return S_OK; }
};

struct SpyVisual : IDCompositionVisual {
    SPY_IUNK
    HRESULT STDMETHODCALLTYPE SetOffsetX(float) override { g_hit = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOffsetX(IDCompositionAnimation*) override { g_hit = 101; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOffsetY(float) override { g_hit = 102; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOffsetY(IDCompositionAnimation*) override { g_hit = 103; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTransform(const D2D_MATRIX_3X2_F&) override { g_hit = 104; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTransform(IDCompositionTransform*) override { g_hit = 105; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTransformParent(IDCompositionVisual*) override { g_hit = 106; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEffect(IDCompositionEffect*) override { g_hit = 107; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE) override { g_hit = 108; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBorderMode(DCOMPOSITION_BORDER_MODE) override { g_hit = 109; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClip(const D2D_RECT_F&) override { g_hit = 110; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClip(IDCompositionClip*) override { g_hit = 111; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetContent(IUnknown* c) override { g_hit = 15; g_arg = (uint64_t)(uintptr_t)c; return S_OK; }
    HRESULT STDMETHODCALLTYPE AddVisual(IDCompositionVisual*, BOOL, IDCompositionVisual*) override { g_hit = 16; return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveVisual(IDCompositionVisual*) override { g_hit = 17; return S_OK; }
    HRESULT STDMETHODCALLTYPE RemoveAllVisuals() override { g_hit = 18; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompositeMode(DCOMPOSITION_COMPOSITE_MODE) override { g_hit = 19; return S_OK; }
};

static void check_dcomp(void) {
    SpyDevice dev;
    SpyTarget tgt;
    SpyVisual vis;
    yscr__DCDevice* d = as_c<yscr__DCDevice>(static_cast<IDCompositionDevice*>(&dev));
    yscr__DCTarget* t = as_c<yscr__DCTarget>(static_cast<IDCompositionTarget*>(&tgt));
    yscr__DCVisual* v = as_c<yscr__DCVisual>(static_cast<IDCompositionVisual*>(&vis));
    void* out = NULL;
    g_hit = -1; d->lpVtbl->Release(d); CHECK(g_hit == 2);
    g_hit = -1; d->lpVtbl->Commit(d); CHECK(g_hit == 3);
    g_hit = -1; g_arg = 0; d->lpVtbl->CreateTargetForHwnd(d, (HWND)(uintptr_t)0x1000, TRUE, &out); CHECK(g_hit == 6 && g_arg == 0x1001);
    g_hit = -1; d->lpVtbl->CreateVisual(d, &out); CHECK(g_hit == 7);
    g_hit = -1; g_arg = 0; d->lpVtbl->CreateSurfaceFromHandle(d, (HANDLE)(uintptr_t)0x2468, &out); CHECK(g_hit == 10 && g_arg == 0x2468);
    g_hit = -1; g_arg = 0; t->lpVtbl->SetRoot(t, (void*)(uintptr_t)0x1357); CHECK(g_hit == 3 && g_arg == 0x1357);
    g_hit = -1; g_arg = 0; v->lpVtbl->SetContent(v, (void*)(uintptr_t)0x9753); CHECK(g_hit == 15 && g_arg == 0x9753);
#if defined(_MSC_VER)
    CHECK(memcmp(&yscr__IID_IDCompositionDevice, &__uuidof(IDCompositionDevice), sizeof(IID)) == 0);
    /* desc.d3d11_video: the one IID serves ID3D10Multithread and ID3D11Multithread */
    CHECK(memcmp(&yscr__IID_ID3D11Multithread, &__uuidof(ID3D11Multithread), sizeof(IID)) == 0);
    CHECK(memcmp(&yscr__IID_ID3D11Multithread, &__uuidof(ID3D10Multithread), sizeof(IID)) == 0);
#endif
}

#if defined(HAVE_PRESENTATION)

// --- presentation ------------------------------------------------------------

struct SpyFactory : IPresentationFactory {
    SPY_IUNK
    boolean STDMETHODCALLTYPE IsPresentationSupported() override { g_hit = 3; return 1; }
    boolean STDMETHODCALLTYPE IsPresentationSupportedWithIndependentFlip() override { g_hit = 4; return 1; }
    HRESULT STDMETHODCALLTYPE CreatePresentationManager(IPresentationManager**) override { g_hit = 5; return S_OK; }
};

struct SpyManager : IPresentationManager {
    SPY_IUNK
    HRESULT STDMETHODCALLTYPE AddBufferFromResource(IUnknown* r, IPresentationBuffer**) override { g_hit = 3; g_arg = (uint64_t)(uintptr_t)r; return S_OK; }
    HRESULT STDMETHODCALLTYPE CreatePresentationSurface(HANDLE h, IPresentationSurface**) override { g_hit = 4; g_arg = (uint64_t)(uintptr_t)h; return S_OK; }
    UINT64 STDMETHODCALLTYPE GetNextPresentId() override { g_hit = 5; return 0x5555; }
    HRESULT STDMETHODCALLTYPE SetTargetTime(SystemInterruptTime t) override { g_hit = 6; g_arg = t.value; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPreferredPresentDuration(SystemInterruptTime, SystemInterruptTime) override { g_hit = 7; return S_OK; }
    HRESULT STDMETHODCALLTYPE ForceVSyncInterrupt(boolean) override { g_hit = 8; return S_OK; }
    HRESULT STDMETHODCALLTYPE Present() override { g_hit = 9; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPresentRetiringFence(REFIID, void**) override { g_hit = 10; return S_OK; }
    HRESULT STDMETHODCALLTYPE CancelPresentsFrom(UINT64 id) override { g_hit = 11; g_arg = id; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLostEvent(HANDLE*) override { g_hit = 12; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPresentStatisticsAvailableEvent(HANDLE*) override { g_hit = 13; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnablePresentStatisticsKind(PresentStatisticsKind k, boolean on) override { g_hit = 14; g_arg = (uint64_t)k * 10 + on; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetNextPresentStatistics(IPresentStatistics**) override { g_hit = 15; return S_OK; }
};

struct SpyBuffer : IPresentationBuffer {
    SPY_IUNK
    HRESULT STDMETHODCALLTYPE GetAvailableEvent(HANDLE*) override { g_hit = 3; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsAvailable(boolean* a) override { g_hit = 4; *a = 1; return S_OK; }
};

struct SpySurface : IPresentationSurface {
    SPY_IUNK
    void STDMETHODCALLTYPE SetTag(UINT_PTR) override { g_hit = 3; }
    HRESULT STDMETHODCALLTYPE SetBuffer(IPresentationBuffer* b) override { g_hit = 4; g_arg = (uint64_t)(uintptr_t)b; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetColorSpace(DXGI_COLOR_SPACE_TYPE c) override { g_hit = 5; g_arg = (uint64_t)c; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetAlphaMode(DXGI_ALPHA_MODE a) override { g_hit = 6; g_arg = (uint64_t)a; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetSourceRect(const RECT* r) override { g_hit = 7; g_arg = (uint64_t)r->right; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTransform(PresentationTransform*) override { g_hit = 8; return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictToOutput(IUnknown*) override { g_hit = 9; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDisableReadback(boolean) override { g_hit = 10; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetLetterboxingMargins(float, float, float, float) override { g_hit = 11; return S_OK; }
};

#define SPY_STATS(KIND) \
    UINT64 STDMETHODCALLTYPE GetPresentId() override { g_hit = 3; return 0x4242; } \
    PresentStatisticsKind STDMETHODCALLTYPE GetKind() override { g_hit = 4; return KIND; }

struct SpyStatus : IPresentStatusPresentStatistics {
    SPY_IUNK
    SPY_STATS(PresentStatisticsKind_PresentStatus)
    CompositionFrameId STDMETHODCALLTYPE GetCompositionFrameId() override { g_hit = 5; return 0x77; }
    PresentStatus STDMETHODCALLTYPE GetPresentStatus() override { g_hit = 6; return PresentStatus_Skipped; }
};

static CompositionFrameDisplayInstance g_inst[2];
struct SpyComp : ICompositionFramePresentStatistics {
    SPY_IUNK
    SPY_STATS(PresentStatisticsKind_CompositionFrame)
    UINT_PTR STDMETHODCALLTYPE GetContentTag() override { g_hit = 5; return 0x99; }
    CompositionFrameId STDMETHODCALLTYPE GetCompositionFrameId() override { g_hit = 6; return 0x88; }
    void STDMETHODCALLTYPE GetDisplayInstanceArray(UINT* n, const CompositionFrameDisplayInstance** a) override { g_hit = 7; *n = 2; *a = g_inst; }
};

struct SpyIFlip : IIndependentFlipFramePresentStatistics {
    SPY_IUNK
    SPY_STATS(PresentStatisticsKind_IndependentFlipFrame)
    LUID STDMETHODCALLTYPE GetOutputAdapterLUID() override { g_hit = 5; LUID l; l.LowPart = 0x11223344; l.HighPart = 0x55667788; return l; }
    UINT STDMETHODCALLTYPE GetOutputVidPnSourceId() override { g_hit = 6; return 3; }
    UINT_PTR STDMETHODCALLTYPE GetContentTag() override { g_hit = 7; return 0x66; }
    SystemInterruptTime STDMETHODCALLTYPE GetDisplayedTime() override { g_hit = 8; SystemInterruptTime t; t.value = 0x0123456789ABCDEFull; return t; }
    SystemInterruptTime STDMETHODCALLTYPE GetPresentDuration() override { g_hit = 9; SystemInterruptTime t; t.value = 0xFEDCBA9876543210ull; return t; }
};

static void check_presentation(void) {
    SpyFactory f; SpyManager m; SpyBuffer b; SpySurface s; SpyStatus st; SpyComp cs; SpyIFlip fs;
    yscr__PFactory* pf = as_c<yscr__PFactory>(static_cast<IPresentationFactory*>(&f));
    yscr__PManager* pm = as_c<yscr__PManager>(static_cast<IPresentationManager*>(&m));
    yscr__PBuffer* pb = as_c<yscr__PBuffer>(static_cast<IPresentationBuffer*>(&b));
    yscr__PSurface* ps = as_c<yscr__PSurface>(static_cast<IPresentationSurface*>(&s));
    yscr__PStatusStats* pst = as_c<yscr__PStatusStats>(static_cast<IPresentStatusPresentStatistics*>(&st));
    yscr__CompStats* pcs = as_c<yscr__CompStats>(static_cast<ICompositionFramePresentStatistics*>(&cs));
    yscr__IFlipStats* pfs = as_c<yscr__IFlipStats>(static_cast<IIndependentFlipFramePresentStatistics*>(&fs));
    void* out = NULL;
    HANDLE h = NULL;
    unsigned char avail = 0;
    RECT r = { 0, 0, 640, 480 };

    g_hit = -1; CHECK(pf->lpVtbl->IsPresentationSupported(pf) == 1 && g_hit == 3);
    g_hit = -1; CHECK(pf->lpVtbl->IsPresentationSupportedWithIndependentFlip(pf) == 1 && g_hit == 4);
    g_hit = -1; pf->lpVtbl->CreatePresentationManager(pf, &out); CHECK(g_hit == 5);

    g_hit = -1; g_arg = 0; pm->lpVtbl->AddBufferFromResource(pm, (void*)(uintptr_t)0xABC, &out); CHECK(g_hit == 3 && g_arg == 0xABC);
    g_hit = -1; g_arg = 0; pm->lpVtbl->CreatePresentationSurface(pm, (HANDLE)(uintptr_t)0xDEF, &out); CHECK(g_hit == 4 && g_arg == 0xDEF);
    g_hit = -1; CHECK(pm->lpVtbl->GetNextPresentId(pm) == 0x5555 && g_hit == 5);
    { yscr__SIT t; t.value = 0x1122334455667788ull; g_hit = -1; g_arg = 0; pm->lpVtbl->SetTargetTime(pm, t); CHECK(g_hit == 6 && g_arg == 0x1122334455667788ull); }
    g_hit = -1; pm->lpVtbl->Present(pm); CHECK(g_hit == 9);
    g_hit = -1; g_arg = 0; pm->lpVtbl->CancelPresentsFrom(pm, 77); CHECK(g_hit == 11 && g_arg == 77);
    g_hit = -1; pm->lpVtbl->GetLostEvent(pm, &h); CHECK(g_hit == 12);
    g_hit = -1; pm->lpVtbl->GetPresentStatisticsAvailableEvent(pm, &h); CHECK(g_hit == 13);
    g_hit = -1; g_arg = 0; pm->lpVtbl->EnablePresentStatisticsKind(pm, 3, 1); CHECK(g_hit == 14 && g_arg == 31);
    g_hit = -1; pm->lpVtbl->GetNextPresentStatistics(pm, &out); CHECK(g_hit == 15);

    g_hit = -1; pb->lpVtbl->GetAvailableEvent(pb, &h); CHECK(g_hit == 3);
    g_hit = -1; pb->lpVtbl->IsAvailable(pb, &avail); CHECK(g_hit == 4 && avail == 1);

    g_hit = -1; g_arg = 0; ps->lpVtbl->SetBuffer(ps, (void*)(uintptr_t)0x31); CHECK(g_hit == 4 && g_arg == 0x31);
    g_hit = -1; g_arg = 0; ps->lpVtbl->SetColorSpace(ps, 12); CHECK(g_hit == 5 && g_arg == 12);
    g_hit = -1; g_arg = 0; ps->lpVtbl->SetAlphaMode(ps, 3); CHECK(g_hit == 6 && g_arg == 3);
    g_hit = -1; g_arg = 0; ps->lpVtbl->SetSourceRect(ps, &r); CHECK(g_hit == 7 && g_arg == 640);

    g_hit = -1; CHECK(pst->lpVtbl->GetPresentId(pst) == 0x4242 && g_hit == 3);
    g_hit = -1; CHECK(pst->lpVtbl->GetKind(pst) == YSCR__KIND_STATUS && g_hit == 4);
    g_hit = -1; CHECK(pst->lpVtbl->GetCompositionFrameId(pst) == 0x77 && g_hit == 5);
    g_hit = -1; CHECK(pst->lpVtbl->GetPresentStatus(pst) == YSCR__STATUS_SKIPPED && g_hit == 6);

    {
        UINT n = 0;
        const yscr__CFDI* a = NULL;
        g_inst[1].instanceKind = CompositionFrameInstanceKind_ScanoutOnScreen;
        g_inst[1].displayVidPnSourceId = 9;
        g_hit = -1; CHECK(pcs->lpVtbl->GetKind(pcs) == YSCR__KIND_COMPOSITION && g_hit == 4);
        g_hit = -1; CHECK(pcs->lpVtbl->GetCompositionFrameId(pcs) == 0x88 && g_hit == 6);
        g_hit = -1; pcs->lpVtbl->GetDisplayInstanceArray(pcs, &n, &a); CHECK(g_hit == 7 && n == 2);
        CHECK(a && a[1].instanceKind == 1 && a[1].displayVidPnSourceId == 9);
    }
    {
        yscr__SIT t;
        LUID l;
        t.value = 0;
        g_hit = -1; CHECK(pfs->lpVtbl->GetKind(pfs) == YSCR__KIND_IFLIP && g_hit == 4);
        g_hit = -1; pfs->lpVtbl->GetOutputAdapterLUID(pfs, &l); CHECK(g_hit == 5 && l.LowPart == 0x11223344 && l.HighPart == 0x55667788);
        g_hit = -1; CHECK(pfs->lpVtbl->GetOutputVidPnSourceId(pfs) == 3 && g_hit == 6);
        g_hit = -1; pfs->lpVtbl->GetDisplayedTime(pfs, &t); CHECK(g_hit == 8 && t.value == 0x0123456789ABCDEFull);
        g_hit = -1; pfs->lpVtbl->GetPresentDuration(pfs, &t); CHECK(g_hit == 9 && t.value == 0xFEDCBA9876543210ull);
    }

    CHECK(memcmp(&yscr__IID_IPresentationFactory, &__uuidof(IPresentationFactory), sizeof(IID)) == 0);
    CHECK(memcmp(&yscr__IID_IPresentStatusPresentStatistics, &__uuidof(IPresentStatusPresentStatistics), sizeof(IID)) == 0);
    CHECK(memcmp(&yscr__IID_ICompositionFramePresentStatistics, &__uuidof(ICompositionFramePresentStatistics), sizeof(IID)) == 0);
    CHECK(memcmp(&yscr__IID_IIndependentFlipFramePresentStatistics, &__uuidof(IIndependentFlipFramePresentStatistics), sizeof(IID)) == 0);
}

static_assert(sizeof(yscr__SIT) == sizeof(SystemInterruptTime), "SystemInterruptTime");
static_assert(sizeof(yscr__CFDI) == sizeof(CompositionFrameDisplayInstance), "CompositionFrameDisplayInstance size");
static_assert(offsetof(yscr__CFDI, displayVidPnSourceId) == offsetof(CompositionFrameDisplayInstance, displayVidPnSourceId), "displayVidPnSourceId");
static_assert(offsetof(yscr__CFDI, renderAdapterLUID) == offsetof(CompositionFrameDisplayInstance, renderAdapterLUID), "renderAdapterLUID");
static_assert(offsetof(yscr__CFDI, instanceKind) == offsetof(CompositionFrameDisplayInstance, instanceKind), "instanceKind");
static_assert(offsetof(yscr__CFDI, finalTransform) == offsetof(CompositionFrameDisplayInstance, finalTransform), "finalTransform");
static_assert(offsetof(yscr__CFDI, requiredCrossAdapterCopy) == offsetof(CompositionFrameDisplayInstance, requiredCrossAdapterCopy), "requiredCrossAdapterCopy");
static_assert(offsetof(yscr__CFDI, colorSpace) == offsetof(CompositionFrameDisplayInstance, colorSpace), "colorSpace");
static_assert(sizeof(yscr__CFStats) == sizeof(COMPOSITION_FRAME_STATS), "COMPOSITION_FRAME_STATS");
static_assert(sizeof(yscr__CTargetId) == sizeof(COMPOSITION_TARGET_ID) &&
              offsetof(yscr__CTargetId, vidPnSourceId) == offsetof(COMPOSITION_TARGET_ID, vidPnSourceId), "COMPOSITION_TARGET_ID");
static_assert(sizeof(yscr__CTargetStats) == sizeof(COMPOSITION_TARGET_STATS) &&
              offsetof(yscr__CTargetStats, presentedStats) == offsetof(COMPOSITION_TARGET_STATS, presentedStats) &&
              offsetof(yscr__CStats, time) == offsetof(COMPOSITION_STATS, time), "COMPOSITION_TARGET_STATS");
static_assert(YSCR__KIND_STATUS == PresentStatisticsKind_PresentStatus && YSCR__KIND_COMPOSITION == PresentStatisticsKind_CompositionFrame &&
              YSCR__KIND_IFLIP == PresentStatisticsKind_IndependentFlipFrame, "PresentStatisticsKind");
static_assert(YSCR__STATUS_SKIPPED == PresentStatus_Skipped && YSCR__STATUS_CANCELED == PresentStatus_Canceled, "PresentStatus");
#endif /* HAVE_PRESENTATION */

/* The display config structs the header declares itself (OS GAMMA, CODES). */
static_assert(sizeof(yscr__dci_header) == sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER), "DEVICE_INFO_HEADER");
static_assert(sizeof(yscr__dc_path) == sizeof(DISPLAYCONFIG_PATH_INFO), "PATH_INFO");
static_assert(offsetof(yscr__dc_path, tgt_adapter) == offsetof(DISPLAYCONFIG_PATH_INFO, targetInfo.adapterId), "targetInfo.adapterId");
static_assert(offsetof(yscr__dc_path, tgt_id) == offsetof(DISPLAYCONFIG_PATH_INFO, targetInfo.id), "targetInfo.id");
static_assert(offsetof(yscr__dc_path, flags) == offsetof(DISPLAYCONFIG_PATH_INFO, flags), "flags");
static_assert(sizeof(yscr__dc_mode) == sizeof(DISPLAYCONFIG_MODE_INFO), "MODE_INFO");
static_assert(sizeof(yscr__dc_source_name) == sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME), "SOURCE_DEVICE_NAME");
static_assert(sizeof(yscr__dc_preferred) == sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE), "TARGET_PREFERRED_MODE");
static_assert(offsetof(yscr__dc_preferred, width) == offsetof(DISPLAYCONFIG_TARGET_PREFERRED_MODE, width), "width");
static_assert(sizeof(yscr__aci) == sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO), "GET_ADVANCED_COLOR_INFO");

int main(void) {
    check_dcomp();
#if defined(HAVE_PRESENTATION)
    check_presentation();
#else
    printf("screen_com: no Presentation.h here; the DirectComposition part only\n");
#endif
    if (g_failures) { fprintf(stderr, "screen_com: %d failure(s)\n", g_failures); return 1; }
    printf("screen_com: all slots, returns, IIDs and layouts match the SDK\n");
    return 0;
}

#else
int main(void) { return 0; }
#endif
