// Compile check: psy_audio.h's own declarations of IAudioClock and of the
// IAudioClient slots it calls (GetStreamLatency, GetService), against the
// Windows SDK's.
//
// A spy class derives from each SDK interface and records which method ran.
// The test calls every slot the header uses through the header's C table and
// checks that the intended method ran with the arguments it was given, and
// that the IID is the SDK's. A wrong slot is a failure here, not a crash on a
// rig. Off Windows, or without miniaudio's WASAPI support, it returns 0.
#if defined(_WIN32)
/* The SDK first: miniaudio defines CLSCTX_ALL only when no header has. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#endif
#define PSY_AUDIO_IMPLEMENTATION
#include "psy_audio.h"

#include <stdio.h>

#if defined(_WIN32) && !defined(PSYAU_NO_MINIAUDIO) && defined(MA_SUPPORT_WASAPI)

static int g_failures = 0;
static int g_hit = -1;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "psy_audio_com: FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

struct SpyClock : IAudioClock {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { g_hit = 0; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { g_hit = 1; return 1; }
    ULONG STDMETHODCALLTYPE Release() override { g_hit = 2; return 1; }
    HRESULT STDMETHODCALLTYPE GetFrequency(UINT64* f) override { g_hit = 3; *f = 384000; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPosition(UINT64* p, UINT64* q) override { g_hit = 4; *p = 11; *q = 22; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD* c) override { g_hit = 5; *c = 0; return S_OK; }
};

static SpyClock g_clock;

struct SpyClient : IAudioClient {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { g_hit = 0; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { g_hit = 1; return 1; }
    ULONG STDMETHODCALLTYPE Release() override { g_hit = 2; return 1; }
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID) override { g_hit = 3; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32*) override { g_hit = 4; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME* l) override { g_hit = 5; *l = 123; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32*) override { g_hit = 6; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE, const WAVEFORMATEX*, WAVEFORMATEX**) override { g_hit = 7; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX**) override { g_hit = 8; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME*, REFERENCE_TIME*) override { g_hit = 9; return S_OK; }
    HRESULT STDMETHODCALLTYPE Start() override { g_hit = 10; return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { g_hit = 11; return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset() override { g_hit = 12; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE) override { g_hit = 13; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void** out) override {
        g_hit = 14;
        *out = IsEqualIID(riid, __uuidof(IAudioClock)) ? static_cast<IAudioClock*>(&g_clock) : nullptr;
        return *out ? S_OK : E_NOINTERFACE;
    }
};

int main(void) {
    SpyClient client;
    psyau__IAudioClient* ac = reinterpret_cast<psyau__IAudioClient*>(static_cast<IAudioClient*>(&client));
    void* clk = nullptr;
    LONGLONG lat = 0;
    UINT64 f = 0, p = 0, q = 0;
    CHECK(IsEqualIID(psyau__IID_IAudioClock, __uuidof(IAudioClock)));
    CHECK(ac->lpVtbl->GetStreamLatency(ac, &lat) == S_OK && g_hit == 5 && lat == 123);
    CHECK(ac->lpVtbl->GetService(ac, &psyau__IID_IAudioClock, &clk) == S_OK && g_hit == 14 && clk);
    if (clk) {
        psyau__IAudioClock* c = static_cast<psyau__IAudioClock*>(clk);
        CHECK(c->lpVtbl->GetFrequency(c, &f) == S_OK && g_hit == 3 && f == 384000);
        CHECK(c->lpVtbl->GetPosition(c, &p, &q) == S_OK && g_hit == 4 && p == 11 && q == 22);
        c->lpVtbl->Release(c);
        CHECK(g_hit == 2);
    }
    if (g_failures) return 1;
    printf("psy_audio_com: every slot matches the SDK\n");
    return 0;
}
#else
int main(void) { return 0; }
#endif
