/* audio_wasapi_periods.c - the periods and buffers each WASAPI render
 * endpoint offers. Plays nothing.
 *
 *     audio_wasapi_periods [--init] [--exclusive]
 *
 *   --init       also initialize (never start) shared streams on each
 *                endpoint: IAudioClient::Initialize, event-driven, at buffer
 *                durations of 0 to 3 engine periods, and
 *                IAudioClient3::InitializeSharedAudioStream at every period
 *                the endpoint offers, up to 8; print the buffer each one gets
 *   --exclusive  also initialize (never start) an exclusive event-driven
 *                stream at the device format, at the minimum and the default
 *                device period, with the realignment that
 *                AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED asks for. While it is
 *                initialized, other programs can lose the device.
 *
 * For each active render endpoint: its name and adapter, the mix format
 * (shared mode), the device format (exclusive mode),
 * IAudioClient::GetDevicePeriod, and IAudioClient3::GetSharedModeEnginePeriod
 * for the mix format: the default, fundamental, minimum and maximum
 * engine period. ysp/audio.h's shared-mode period comes from these: a
 * minimum under the default means the endpoint offers low-latency shared
 * mode (docs/audio.md, "Periods and buffers").
 *
 * Exit: 0, 1 when no endpoint could be read, 2 usage, 3 not Windows.
 */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
int main(void) {
    printf("audio_wasapi_periods: WASAPI is Windows only\n");
    return 3;
}
#else

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>

/* The GUIDs and keys, here, so the program links no uuid library. */
static const GUID wp_CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID wp_IID_IMMDeviceEnumerator = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID wp_IID_IAudioClient = { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const GUID wp_IID_IAudioClient3 = { 0x7ED4EE07, 0x8E67, 0x4CD4, { 0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42 } };
static const GUID wp_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID wp_PCM = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const PROPERTYKEY wp_PKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
static const PROPERTYKEY wp_PKEY_DeviceInterface_FriendlyName = { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 2 };
static const PROPERTYKEY wp_PKEY_AudioEngine_DeviceFormat = { { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0 };

#define WP_E_NOT_ALIGNED ((HRESULT)0x88890019L)  /* AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED */

static int g_init = 0, g_excl = 0;

static double ms_of(double frames, DWORD rate) { return frames * 1000.0 / (double)rate; }

static void print_format(const char* label, const WAVEFORMATEX* wf) {
    const char* kind = "?";
    unsigned valid = wf->wBitsPerSample;
    unsigned long mask = 0;
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) kind = "float";
    else if (wf->wFormatTag == WAVE_FORMAT_PCM) kind = "int";
    else if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)wf;
        kind = IsEqualGUID(&x->SubFormat, &wp_FLOAT) ? "float" : IsEqualGUID(&x->SubFormat, &wp_PCM) ? "int" : "other";
        valid = x->Samples.wValidBitsPerSample;
        mask = x->dwChannelMask;
    }
    printf("  %-14s %s %u bits (%u valid), %lu Hz, %u channels, mask 0x%lx%s\n", label, kind,
           (unsigned)wf->wBitsPerSample, valid, (unsigned long)wf->nSamplesPerSec, (unsigned)wf->nChannels, mask,
           wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? ", extensible" : "");
}

static void print_prop_string(IPropertyStore* ps, const PROPERTYKEY* key, const char* label) {
    PROPVARIANT v;
    char buf[256] = "?";
    PropVariantInit(&v);
    if (SUCCEEDED(IPropertyStore_GetValue(ps, key, &v)) && v.vt == VT_LPWSTR)
        WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, buf, sizeof buf, NULL, NULL);
    printf("  %-14s %s\n", label, buf);
    PropVariantClear(&v);
}

static IAudioClient* activate(IMMDevice* md) {
    IAudioClient* ac = NULL;
    if (FAILED(IMMDevice_Activate(md, &wp_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&ac))) return NULL;
    return ac;
}

/* One shared stream, initialized and released: the buffer it gets. */
static void try_shared(IMMDevice* md, const WAVEFORMATEX* mix, REFERENCE_TIME dur, const char* label) {
    IAudioClient* ac = activate(md);
    UINT32 buf = 0;
    REFERENCE_TIME lat = -1;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HRESULT hr;
    if (!ac) return;
    hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, dur, 0, mix, NULL);
    if (SUCCEEDED(hr)) {
        IAudioClient_SetEventHandle(ac, ev);
        IAudioClient_GetBufferSize(ac, &buf);
        IAudioClient_GetStreamLatency(ac, &lat);
        printf("    %-34s buffer %5u frames (%6.3f ms), GetStreamLatency %6.3f ms\n", label, (unsigned)buf,
               ms_of(buf, mix->nSamplesPerSec), (double)lat / 1e4);
    } else {
        printf("    %-34s failed, HRESULT 0x%08lx\n", label, (unsigned long)hr);
    }
    IAudioClient_Release(ac);
    CloseHandle(ev);
}

static void try_shared3(IMMDevice* md, const WAVEFORMATEX* mix, UINT32 period) {
    IAudioClient* ac = activate(md);
    IAudioClient3* a3 = NULL;
    UINT32 buf = 0, cur = 0;
    REFERENCE_TIME lat = -1;
    WAVEFORMATEX* cf = NULL;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HRESULT hr;
    char label[64];
    if (!ac) return;
    snprintf(label, sizeof label, "IAudioClient3, period %u", (unsigned)period);
    if (SUCCEEDED(IAudioClient_QueryInterface(ac, &wp_IID_IAudioClient3, (void**)&a3))) {
        hr = IAudioClient3_InitializeSharedAudioStream(a3, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, mix, NULL);
        if (SUCCEEDED(hr)) {
            IAudioClient_SetEventHandle(ac, ev);
            IAudioClient_GetBufferSize(ac, &buf);
            IAudioClient_GetStreamLatency(ac, &lat);
            IAudioClient3_GetCurrentSharedModeEnginePeriod(a3, &cf, &cur);
            if (cf) CoTaskMemFree(cf);
            printf("    %-34s buffer %5u frames (%6.3f ms), GetStreamLatency %6.3f ms, engine now %u\n", label,
                   (unsigned)buf, ms_of(buf, mix->nSamplesPerSec), (double)lat / 1e4, (unsigned)cur);
        } else {
            printf("    %-34s failed, HRESULT 0x%08lx\n", label, (unsigned long)hr);
        }
        IAudioClient3_Release(a3);
    }
    IAudioClient_Release(ac);
    CloseHandle(ev);
}

/* Exclusive, event-driven: periodicity and buffer duration must be equal
 * (Microsoft, IAudioClient::Initialize). On BUFFER_SIZE_NOT_ALIGNED the
 * documented fix is a new client at the aligned size. */
static void try_exclusive(IMMDevice* md, const WAVEFORMATEX* df, REFERENCE_TIME per, const char* label) {
    IAudioClient* ac = activate(md);
    UINT32 buf = 0;
    REFERENCE_TIME lat = -1;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HRESULT hr;
    int realigned = 0;
    if (!ac) return;
    hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, per, per, df, NULL);
    if (hr == WP_E_NOT_ALIGNED) {
        IAudioClient_GetBufferSize(ac, &buf);
        IAudioClient_Release(ac);
        per = (REFERENCE_TIME)(1e7 * (double)buf / (double)df->nSamplesPerSec + 0.5);
        ac = activate(md);
        if (!ac) return;
        realigned = 1;
        hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, per, per, df, NULL);
    }
    if (SUCCEEDED(hr)) {
        IAudioClient_SetEventHandle(ac, ev);
        IAudioClient_GetBufferSize(ac, &buf);
        IAudioClient_GetStreamLatency(ac, &lat);
        printf("    %-34s buffer %5u frames (%6.3f ms), GetStreamLatency %6.3f ms%s\n", label, (unsigned)buf,
               ms_of(buf, df->nSamplesPerSec), (double)lat / 1e4, realigned ? ", after realignment" : "");
    } else {
        printf("    %-34s failed, HRESULT 0x%08lx%s\n", label, (unsigned long)hr, realigned ? " after realignment" : "");
    }
    IAudioClient_Release(ac);
    CloseHandle(ev);
}

static void probe(IMMDevice* md, int index, int is_default) {
    IPropertyStore* ps = NULL;
    IAudioClient* ac = activate(md);
    IAudioClient3* a3 = NULL;
    WAVEFORMATEX* mix = NULL;
    WAVEFORMATEX* df = NULL;
    PROPVARIANT dv;
    REFERENCE_TIME pdef = 0, pmin = 0;
    UINT32 def = 0, fund = 0, mn = 0, mx = 0;
    HRESULT hr;
    printf("endpoint %d%s\n", index, is_default ? " (the default for eConsole)" : "");
    PropVariantInit(&dv);
    if (SUCCEEDED(IMMDevice_OpenPropertyStore(md, STGM_READ, &ps))) {
        print_prop_string(ps, &wp_PKEY_Device_FriendlyName, "name");
        print_prop_string(ps, &wp_PKEY_DeviceInterface_FriendlyName, "adapter");
        if (SUCCEEDED(IPropertyStore_GetValue(ps, &wp_PKEY_AudioEngine_DeviceFormat, &dv)) && dv.vt == VT_BLOB)
            df = (WAVEFORMATEX*)dv.blob.pBlobData;
    }
    if (!ac) { printf("  IMMDevice::Activate(IAudioClient) failed\n"); goto out; }
    if (FAILED(IAudioClient_GetMixFormat(ac, &mix))) { printf("  GetMixFormat failed\n"); goto out; }
    print_format("mix format", mix);
    if (df) {
        hr = IAudioClient_IsFormatSupported(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, df, NULL);
        print_format("device format", df);
        printf("  %-14s %s (IsFormatSupported 0x%08lx)\n", "exclusive", hr == S_OK ? "the device format is supported" : "not supported",
               (unsigned long)hr);
    }
    if (SUCCEEDED(IAudioClient_GetDevicePeriod(ac, &pdef, &pmin)))
        printf("  %-14s default %.3f ms, minimum %.3f ms (IAudioClient::GetDevicePeriod; the minimum is exclusive mode's)\n",
               "device period", (double)pdef / 1e4, (double)pmin / 1e4);
    hr = IAudioClient_QueryInterface(ac, &wp_IID_IAudioClient3, (void**)&a3);
    if (FAILED(hr)) {
        printf("  %-14s no IAudioClient3 (HRESULT 0x%08lx): no low-latency shared mode\n", "engine period", (unsigned long)hr);
    } else {
        WAVEFORMATEX* cf = NULL;
        UINT32 cur = 0;
        hr = IAudioClient3_GetSharedModeEnginePeriod(a3, mix, &def, &fund, &mn, &mx);
        if (FAILED(hr)) {
            printf("  %-14s GetSharedModeEnginePeriod failed, HRESULT 0x%08lx\n", "engine period", (unsigned long)hr);
        } else {
            DWORD r = mix->nSamplesPerSec;
            printf("  %-14s default %u (%.3f ms), fundamental %u (%.3f ms), minimum %u (%.3f ms), maximum %u (%.3f ms) frames\n",
                   "engine period", (unsigned)def, ms_of(def, r), (unsigned)fund, ms_of(fund, r), (unsigned)mn, ms_of(mn, r),
                   (unsigned)mx, ms_of(mx, r));
            printf("  %-14s %s\n", "low latency", mn < def ? "offered: periods under the default" : "not offered: the minimum is the default");
        }
        if (SUCCEEDED(IAudioClient3_GetCurrentSharedModeEnginePeriod(a3, &cf, &cur))) {
            printf("  %-14s %u frames (GetCurrentSharedModeEnginePeriod: what the engine runs now)\n", "engine now", (unsigned)cur);
            if (cf) CoTaskMemFree(cf);
        }
        IAudioClient3_Release(a3);
    }
    if (g_init) {
        int k;
        REFERENCE_TIME p = pdef ? pdef : 100000;
        printf("  shared streams, initialized and released, never started:\n");
        for (k = 0; k <= 3; k++) {
            char label[64];
            snprintf(label, sizeof label, "Initialize, buffer %d x %.3f ms", k, (double)p / 1e4);
            try_shared(md, mix, p * k, label);
        }
        if (fund) {
            UINT32 q;
            int n = 0;
            for (q = mn; q <= mx && n < 8; q += fund, n++) try_shared3(md, mix, q);
        }
    }
    if (g_excl && df) {
        printf("  exclusive streams at the device format, initialized and released, never started:\n");
        if (pmin) try_exclusive(md, df, pmin, "Initialize, period = minimum");
        if (pdef) try_exclusive(md, df, pdef, "Initialize, period = default");
    }
out:
    if (mix) CoTaskMemFree(mix);
    PropVariantClear(&dv);
    if (ps) IPropertyStore_Release(ps);
    if (ac) IAudioClient_Release(ac);
}

int main(int argc, char** argv) {
    IMMDeviceEnumerator* en = NULL;
    IMMDeviceCollection* col = NULL;
    IMMDevice* defdev = NULL;
    LPWSTR defid = NULL;
    UINT n = 0, i;
    int a;
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--init")) g_init = 1;
        else if (!strcmp(argv[a], "--exclusive")) g_excl = 1;
        else { fprintf(stderr, "usage: audio_wasapi_periods [--init] [--exclusive]\n"); return 2; }
    }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&wp_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &wp_IID_IMMDeviceEnumerator, (void**)&en))
        || FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &col))
        || FAILED(IMMDeviceCollection_GetCount(col, &n)) || n == 0) {
        fprintf(stderr, "audio_wasapi_periods: no active render endpoint\n");
        return 1;
    }
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &defdev))) {
        IMMDevice_GetId(defdev, &defid);
        IMMDevice_Release(defdev);
    }
    for (i = 0; i < n; i++) {
        IMMDevice* md = NULL;
        LPWSTR id = NULL;
        int is_def = 0;
        if (FAILED(IMMDeviceCollection_Item(col, i, &md))) continue;
        if (defid && SUCCEEDED(IMMDevice_GetId(md, &id))) { is_def = wcscmp(id, defid) == 0; CoTaskMemFree(id); }
        probe(md, (int)i, is_def);
        IMMDevice_Release(md);
    }
    if (defid) CoTaskMemFree(defid);
    IMMDeviceCollection_Release(col);
    IMMDeviceEnumerator_Release(en);
    CoUninitialize();
    return 0;
}
#endif
