#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <stdio.h>
#include <wchar.h>

DEFINE_GUID(XMF_XVP_DISABLE_FRC, 0x2c0afa19, 0x7a97, 0x4d5a, 0x9e, 0xe8, 0x16, 0xd4, 0xfc, 0x51, 0x8d, 0x8c);
DEFINE_GUID(XMF_SOURCE_READER_D3D11_BIND_FLAGS, 0x33f3197b, 0xf73a, 0x4e14, 0x8d, 0x85, 0x0e, 0x4c, 0x43, 0x68, 0x78, 0x8d);
DEFINE_GUID(XMF_SOURCE_READER_D3D_MANAGER, 0xec822da2, 0xe1e9, 0x4b29, 0xa0, 0xd8, 0x56, 0x3c, 0x71, 0x9f, 0x52, 0x69);
DEFINE_GUID(XMF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, 0xf81da2c, 0xb537, 0x4672, 0xa8, 0xb2, 0xa6, 0x81, 0xb1, 0x73, 0x07, 0xa3);
DEFINE_GUID(XMF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 0xa634a91c, 0x822b, 0x41b9, 0xa4, 0x94, 0x4d, 0xe4, 0x64, 0x36, 0x12, 0xb0);
DEFINE_GUID(XMF_SOURCE_READER_DISABLE_DXVA, 0xaa456cfd, 0x3943, 0x4a1e, 0xa7, 0x7d, 0x18, 0x38, 0xc0, 0xea, 0x2e, 0x35);

static const GUID XGUID_NULL = {0};
static LONGLONG qpc(void) { LARGE_INTEGER v; QueryPerformanceCounter(&v); return v.QuadPart; }

int wmain(int argc, wchar_t **argv)
{
    LONGLONG freq, t_start, t_prev, last_window;
    int use_d3d = 1, want_nv12 = 0, frames = 300, i, got = 0, over40 = 0, over50 = 0, is_dxgi = -1;
    double max_dt = 0, sum_dt = 0, win_sum = 0, win_max = 0;
    int win_n = 0, win_over50 = 0;
    ID3D11Device *dev = NULL;
    IMFDXGIDeviceManager *mgr = NULL;
    IMFSourceReader *reader = NULL;
    IMFAttributes *attr = NULL;
    IMFMediaType *mt = NULL;
    UINT token = 0;
    HRESULT hr;
    LARGE_INTEGER f;

    if (argc < 2) { wprintf(L"kullanım: mfbench <dosya> [nod3d] [nv12] [kare sayısı]\n"); return 1; }
    for (i = 2; i < argc; ++i)
    {
        if (!wcscmp(argv[i], L"nod3d")) use_d3d = 0;
        else if (!wcscmp(argv[i], L"nv12")) want_nv12 = 1;
        else frames = _wtoi(argv[i]);
    }
    QueryPerformanceFrequency(&f); freq = f.QuadPart;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    printf("MFStartup hr=%#lx\n", hr);

    if (use_d3d)
    {
        D3D_FEATURE_LEVEL fl;
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, NULL);
        printf("D3D11CreateDevice hr=%#lx\n", hr);
        if (FAILED(hr)) return 2;
        hr = MFCreateDXGIDeviceManager(&token, &mgr);
        hr = IMFDXGIDeviceManager_ResetDevice(mgr, (IUnknown *)dev, token);
        printf("DXGI manager hr=%#lx\n", hr);
    }

    MFCreateAttributes(&attr, 8);
    IMFAttributes_SetUINT32(attr, &XMF_SOURCE_READER_DISABLE_DXVA, 0);
    IMFAttributes_SetUINT32(attr, &XMF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);
    IMFAttributes_SetUINT32(attr, &XMF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, 1);
    IMFAttributes_SetUINT32(attr, &XMF_SOURCE_READER_D3D11_BIND_FLAGS, 8);
    IMFAttributes_SetUINT32(attr, &XMF_XVP_DISABLE_FRC, 1);
    if (mgr) IMFAttributes_SetUnknown(attr, &XMF_SOURCE_READER_D3D_MANAGER, (IUnknown *)mgr);

    hr = MFCreateSourceReaderFromURL(argv[1], attr, &reader);
    printf("MFCreateSourceReaderFromURL hr=%#lx\n", hr);
    if (FAILED(hr)) return 3;

    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    MFCreateMediaType(&mt);
    IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, want_nv12 ? &MFVideoFormat_NV12 : &MFVideoFormat_ARGB32);
    IMFMediaType_SetUINT32(mt, &MF_MT_VIDEO_ROTATION, 0);
    hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt);
    printf("SetCurrentMediaType(%s) hr=%#lx\n", want_nv12 ? "NV12" : "ARGB32", hr);
    if (FAILED(hr)) return 4;
    IMFMediaType_Release(mt);

    {
        IUnknown *xf = NULL;
        hr = IMFSourceReader_GetServiceForStream(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &XGUID_NULL, &IID_IMFTransform, (void **)&xf);
        printf("GetServiceForStream(IMFTransform) hr=%#lx  %s\n", hr, hr == (HRESULT)0x80004002 ? "<-- E_NOINTERFACE (Unity 'Get video transform' hatası)" : "");
        if (xf) IUnknown_Release(xf);
    }
    {
        UINT32 stride = 0, ssize = 0, w = 0, h = 0; GUID sub = {0}; UINT64 fs = 0;
        hr = IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mt);
        IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &sub);
        IMFMediaType_GetUINT64(mt, &MF_MT_FRAME_SIZE, &fs); w = fs >> 32; h = (UINT32)fs;
        IMFMediaType_GetUINT32(mt, &MF_MT_DEFAULT_STRIDE, &stride);
        IMFMediaType_GetUINT32(mt, &MF_MT_SAMPLE_SIZE, &ssize);
        printf("güncel tip: subtype data1=%#lx  %ux%u  stride=%u  sample_size=%u\n", sub.Data1, w, h, (unsigned)(INT32)stride, ssize);
        IMFMediaType_Release(mt);
    }

    t_start = t_prev = last_window = qpc();
    for (i = 0; i < frames; ++i)
    {
        IMFSample *s = NULL; DWORD idx, flags; LONGLONG ts; double dt; LONGLONG a = qpc(), b;
        hr = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &s);
        b = qpc();
        if (FAILED(hr)) { printf("ReadSample hr=%#lx\n", hr); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { printf("akış sonu\n"); break; }
        if (!s) continue;
        if (is_dxgi < 0)
        {
            IMFMediaBuffer *buf = NULL; IMFDXGIBuffer *dx = NULL; DWORD len = 0, cnt = 0;
            IMFSample_GetBufferCount(s, &cnt);
            IMFSample_GetBufferByIndex(s, 0, &buf);
            is_dxgi = SUCCEEDED(IMFMediaBuffer_QueryInterface(buf, &IID_IMFDXGIBuffer, (void **)&dx));
            IMFMediaBuffer_GetCurrentLength(buf, &len);
            printf("ilk örnek: buffer sayısı=%lu  IMFDXGIBuffer=%s  uzunluk=%lu\n", cnt, is_dxgi ? "EVET" : "hayır", len);
            if (dx) IMFDXGIBuffer_Release(dx);
            IMFMediaBuffer_Release(buf);
        }
        IMFSample_Release(s);
        dt = (b - a) * 1000.0 / freq;
        ++got; sum_dt += dt; if (dt > max_dt) max_dt = dt; if (dt > 40) ++over40; if (dt > 50) ++over50;
        win_sum += dt; if (dt > win_max) win_max = dt; ++win_n; if (dt > 50) ++win_over50;
        if (win_n == 100)
        {
            printf("  pencere: ort ReadSample %.1f ms  max %.1f ms  50ms üstü %d\n", win_sum / win_n, win_max, win_over50);
            win_sum = win_max = 0; win_n = win_over50 = 0;
        }
    }
    {
        double total = (qpc() - t_start) * 1000.0 / freq;
        printf("SONUÇ: %d kare, toplam %.0f ms, %.1f kare/sn, ort %.1f ms, max %.1f ms, >40ms: %d, >50ms: %d\n",
                got, total, got * 1000.0 / total, got ? sum_dt / got : 0, max_dt, over40, over50);
    }
    IMFSourceReader_Release(reader);
    MFShutdown();
    return 0;
}
