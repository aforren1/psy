// Compile check: psy_video.h's own copies of Media Foundation's GUIDs, its
// IStream table and its MFVideoArea, against the Windows SDK's.
//
// The header loads Media Foundation at run time and keeps local GUIDs, so it
// links no import library and no mfuuid.lib. A wrong GUID would fail only at
// run time, on a rig, as a missing attribute; here it is a failure in this
// test. Interface IIDs come from __uuidof, which needs no import
// library either. The IStream table is called through the SDK's C++ interface,
// so a slot out of order runs the wrong method and fails a check. Off
// Windows, or with PSYVID_NO_MF, it does nothing and returns 0.
#if defined(_WIN32)
#include <initguid.h>   // the SDK's DEFINE_GUIDs become definitions in this file
#endif
#ifndef PSYVID_NO_PL_MPEG
#define PSYVID_NO_PL_MPEG   // not about MPEG-1; CMake gives this check no pl_mpeg.h
#endif
#define PSY_VIDEO_IMPLEMENTATION
#include "psy_video.h"

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if PSYVID__MF
#include <wmcodecdsp.h>

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "psy_video_com: FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define SAME(ours, sdk) do { const GUID sdk_ = (sdk); CHECK(memcmp(&(ours), &sdk_, sizeof(GUID)) == 0); } while (0)

int main() {
    SAME(psyvid__IID_IUnknown, __uuidof(IUnknown));
    SAME(psyvid__IID_ISequentialStream, __uuidof(ISequentialStream));
    SAME(psyvid__IID_IStream, __uuidof(IStream));
    SAME(psyvid__IID_IMFAttributes, __uuidof(IMFAttributes));
    SAME(psyvid__IID_IMF2DBuffer, __uuidof(IMF2DBuffer));
    SAME(psyvid__IID_IMF2DBuffer2, __uuidof(IMF2DBuffer2));
    SAME(psyvid__IID_IMFDXGIBuffer, __uuidof(IMFDXGIBuffer));
    SAME(psyvid__IID_IMFSourceReaderEx, __uuidof(IMFSourceReaderEx));
    SAME(psyvid__IID_IMFTransform, __uuidof(IMFTransform));
    SAME(psyvid__IID_ID3D10Multithread, __uuidof(ID3D10Multithread));
    SAME(psyvid__IID_IDXGIFactory1, __uuidof(IDXGIFactory1));
    SAME(psyvid__MF_MT_MAJOR_TYPE, MF_MT_MAJOR_TYPE);
    SAME(psyvid__MF_MT_SUBTYPE, MF_MT_SUBTYPE);
    SAME(psyvid__MFMediaType_Video, MFMediaType_Video);
    SAME(psyvid__MFVideoFormat_NV12, MFVideoFormat_NV12);
    SAME(psyvid__MFVideoFormat_H264, MFVideoFormat_H264);
    SAME(psyvid__MFVideoFormat_HEVC, MFVideoFormat_HEVC);
    SAME(psyvid__MF_MT_FRAME_SIZE, MF_MT_FRAME_SIZE);
    SAME(psyvid__MF_MT_FRAME_RATE, MF_MT_FRAME_RATE);
    SAME(psyvid__MF_MT_PIXEL_ASPECT_RATIO, MF_MT_PIXEL_ASPECT_RATIO);
    SAME(psyvid__MF_MT_INTERLACE_MODE, MF_MT_INTERLACE_MODE);
    SAME(psyvid__MF_MT_MINIMUM_DISPLAY_APERTURE, MF_MT_MINIMUM_DISPLAY_APERTURE);
    SAME(psyvid__MF_MT_DEFAULT_STRIDE, MF_MT_DEFAULT_STRIDE);
    SAME(psyvid__MF_MT_YUV_MATRIX, MF_MT_YUV_MATRIX);
    SAME(psyvid__MF_MT_VIDEO_NOMINAL_RANGE, MF_MT_VIDEO_NOMINAL_RANGE);
    SAME(psyvid__MF_MT_TRANSFER_FUNCTION, MF_MT_TRANSFER_FUNCTION);
    SAME(psyvid__MF_MT_VIDEO_PRIMARIES, MF_MT_VIDEO_PRIMARIES);
    SAME(psyvid__MF_MT_VIDEO_CHROMA_SITING, MF_MT_VIDEO_CHROMA_SITING);
#if defined(_MSC_VER)
    SAME(psyvid__MF_MT_VIDEO_ROTATION, MF_MT_VIDEO_ROTATION);   // MinGW-w64's mfapi.h lacks it
#endif
    SAME(psyvid__MF_MT_MPEG2_PROFILE, MF_MT_MPEG2_PROFILE);
    SAME(psyvid__MF_MT_MPEG_SEQUENCE_HEADER, MF_MT_MPEG_SEQUENCE_HEADER);
    SAME(psyvid__MF_PD_DURATION, MF_PD_DURATION);
    SAME(psyvid__MF_READWRITE_DISABLE_CONVERTERS, MF_READWRITE_DISABLE_CONVERTERS);
    SAME(psyvid__MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS);
    SAME(psyvid__MF_SOURCE_READER_D3D_MANAGER, MF_SOURCE_READER_D3D_MANAGER);
    SAME(psyvid__MF_SOURCE_READER_D3D11_BIND_FLAGS, MF_SOURCE_READER_D3D11_BIND_FLAGS);
    SAME(psyvid__IID_ID3D11Texture2D, __uuidof(ID3D11Texture2D));
    SAME(psyvid__MF_BYTESTREAM_CONTENT_TYPE, MF_BYTESTREAM_CONTENT_TYPE);
    SAME(psyvid__MFSampleExtension_CleanPoint, MFSampleExtension_CleanPoint);
    SAME(psyvid__MFSampleExtension_Interlaced, MFSampleExtension_Interlaced);
    SAME(psyvid__MFSampleExtension_DecodeTimestamp, MFSampleExtension_DecodeTimestamp);
    SAME(psyvid__MFT_FRIENDLY_NAME_Attribute, MFT_FRIENDLY_NAME_Attribute);
    SAME(psyvid__MFT_ENUM_HARDWARE_URL_Attribute, MFT_ENUM_HARDWARE_URL_Attribute);
    SAME(psyvid__MFT_TRANSFORM_CLSID_Attribute, MFT_TRANSFORM_CLSID_Attribute);
    SAME(psyvid__CLSID_MSH264DecoderMFT, __uuidof(CMSH264DecoderMFT));
    {
        static const GUID zero = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };   // GUID_NULL
        SAME(psyvid__GUID_NULL, zero);
    }

    // MFVideoArea as the header reads it from MF_MT_MINIMUM_DISPLAY_APERTURE
    static_assert(sizeof(psyvid__mfarea) == sizeof(MFVideoArea), "MFVideoArea size");
    static_assert(offsetof(psyvid__mfarea, x) == offsetof(MFVideoArea, OffsetX) + offsetof(MFOffset, value), "OffsetX");
    static_assert(offsetof(psyvid__mfarea, y) == offsetof(MFVideoArea, OffsetY) + offsetof(MFOffset, value), "OffsetY");
    static_assert(offsetof(psyvid__mfarea, cx) == offsetof(MFVideoArea, Area) + offsetof(SIZE, cx), "Area.cx");
    static_assert(offsetof(psyvid__mfarea, cy) == offsetof(MFVideoArea, Area) + offsetof(SIZE, cy), "Area.cy");

    // The IStream table, called as Media Foundation calls it
    {
        static const unsigned char bytes[100] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        psyvid__stm* s = (psyvid__stm*)psyvid__malloc(sizeof *s);
        IStream* is;
        IUnknown* u = NULL;
        STATSTG st;
        LARGE_INTEGER mv;
        ULARGE_INTEGER np;
        unsigned char got[4] = { 0, 0, 0, 0 };
        ULONG n = 0;
        memset(s, 0, sizeof *s);
        s->vt = &psyvid__stm_table;
        s->refs = 1;
        InitializeSRWLock(&s->lock);
        CHECK(psyvid__src_open(&s->src, NULL, bytes, sizeof bytes, NULL, NULL) == PSYVID_OK);
        is = (IStream*)(void*)s;
        CHECK(is->Stat(&st, STATFLAG_NONAME) == S_OK && st.cbSize.QuadPart == sizeof bytes && st.type == STGTY_STREAM);
        mv.QuadPart = 5;
        CHECK(is->Seek(mv, STREAM_SEEK_SET, &np) == S_OK && np.QuadPart == 5);
        CHECK(is->Read(got, 3, &n) == S_OK && n == 3 && got[0] == 6 && got[2] == 8);
        mv.QuadPart = -2;
        CHECK(is->Seek(mv, STREAM_SEEK_END, &np) == S_OK && np.QuadPart == sizeof bytes - 2);
        CHECK(is->Read(got, 4, &n) == S_FALSE && n == 2);
        CHECK(is->Write(got, 1, &n) == STG_E_ACCESSDENIED);
        CHECK(is->QueryInterface(__uuidof(ISequentialStream), (void**)&u) == S_OK && u == (IUnknown*)is);
        CHECK(is->AddRef() == 3);
        CHECK(is->Release() == 2);
        if (u) u->Release();
        CHECK(is->Release() == 0);   // frees it
    }
    if (g_failures) { fprintf(stderr, "psy_video_com: %d failure(s)\n", g_failures); return 1; }
    printf("psy_video_com: GUIDs, MFVideoArea and the IStream table match the SDK\n");
    return 0;
}
#else
int main() { return 0; }
#endif
