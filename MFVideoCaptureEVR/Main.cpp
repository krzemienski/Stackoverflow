//----------------------------------------------------------------------------------------------
// Main.cpp
//----------------------------------------------------------------------------------------------
#pragma once
#define WIN32_LEAN_AND_MEAN
#define STRICT

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "Strmiids.lib")

//----------------------------------------------------------------------------------------------
// Microsoft Windows SDK for Windows 7
#include <WinSDKVer.h>
#include <new>
#include <Windows.h>
#include <assert.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>
#include <mferror.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <evr.h>

template <class T> inline void SAFE_RELEASE(T*& p){

	if(p){
		p->Release();
		p = NULL;
	}
}

#define WINDOWAPPLICATION_CLASS L"WindowApplication"

HRESULT ProcessEvr();
HRESULT InitSourceReader(IMFSourceReader**);
HRESULT CreateVideoCaptureSource(IMFMediaSource**);
HRESULT SetupSourceReaderMediaType(IMFSourceReader*, UINT32*, UINT32*);
HRESULT InitWindow(HWND&, const UINT32, const UINT32);
LRESULT CALLBACK WindowApplicationMsgProc(HWND, UINT, WPARAM, LPARAM);
HRESULT InitMediaSink(IMFSourceReader*, IMFMediaSink**, IMFStreamSink**, const HWND, const UINT32, const UINT32);
HRESULT InitSampleAllocator(IMFMediaSink*, IMFStreamSink*, IMFVideoSampleAllocator**, IMFSample**);
HRESULT InitClock(IMFMediaSink*, IMFPresentationClock**);
HRESULT DisplayVideo(IMFSourceReader*, IMFStreamSink*, IMFSample*);
HRESULT RenderFrame(IMFSourceReader*, IMFStreamSink*, IMFSample*);
HRESULT CopyAttribute(IMFAttributes*, IMFAttributes*, REFGUID);

// TimeStamp for capture card
LONGLONG g_llTimeStamp = 0;

void main() {

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	if(SUCCEEDED(hr)) {

		hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);

		if(SUCCEEDED(hr)) {

			hr = ProcessEvr();

			hr = MFShutdown();
		}

		CoUninitialize();
	}
}

HRESULT ProcessEvr(){

	IMFSourceReader* pSourceReader = NULL;
	IMFMediaSink* pMediaSink = NULL;
	IMFStreamSink* pStreamSink = NULL;
	IMFVideoSampleAllocator* pVideoSampleAllocator = NULL;
	IMFSample* pD3DVideoSample = NULL;
	IMFPresentationClock* pClock = NULL;
	UINT32 uiWidth = 0;
	UINT32 uiHeight = 0;
	HWND hWnd = NULL;

	HRESULT hr = InitSourceReader(&pSourceReader);
	if(FAILED(hr)){ goto done; }

	hr = SetupSourceReaderMediaType(pSourceReader, &uiWidth, &uiHeight);
	if(FAILED(hr)){ goto done; }

	hr = InitWindow(hWnd, uiWidth, uiHeight);
	if(FAILED(hr)){ goto done; }

	hr = InitMediaSink(pSourceReader, &pMediaSink, &pStreamSink, hWnd, uiWidth, uiHeight);
	if(FAILED(hr)){ goto done; }

	hr = InitSampleAllocator(pMediaSink, pStreamSink, &pVideoSampleAllocator, &pD3DVideoSample);
	if(FAILED(hr)){ goto done; }

	hr = InitClock(pMediaSink, &pClock);
	if(FAILED(hr)){ goto done; }

	hr = DisplayVideo(pSourceReader, pStreamSink, pD3DVideoSample);

done:

	if(pClock){

		hr = pClock->Stop();

		// Wait for the Sink to stop, we should handle event Media Sink
		Sleep(1000);

		SAFE_RELEASE(pClock);
	}

	SAFE_RELEASE(pD3DVideoSample);
	SAFE_RELEASE(pStreamSink);

	if(pVideoSampleAllocator){

		hr = pVideoSampleAllocator->UninitializeSampleAllocator();
		SAFE_RELEASE(pVideoSampleAllocator);
	}

	if(pMediaSink){

		hr = pMediaSink->Shutdown();
		SAFE_RELEASE(pMediaSink);
	}

	if(pSourceReader){

		hr = pSourceReader->Flush((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		SAFE_RELEASE(pSourceReader);
	}

	if(IsWindow(hWnd)) {

		DestroyWindow(hWnd);
		UnregisterClass(WINDOWAPPLICATION_CLASS, GetModuleHandle(NULL));
		hWnd = NULL;
	}

	return hr;
}

HRESULT InitSourceReader(IMFSourceReader** ppSourceReader){

	IMFMediaSource* pMediaSource = NULL;
	IMFAttributes* pAttributes = NULL;

	HRESULT hr = CreateVideoCaptureSource(&pMediaSource);
	if(FAILED(hr)){ goto done; }

	hr = MFCreateAttributes(&pAttributes, 1);
	if(FAILED(hr)){ goto done; }

	hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1);
	if(FAILED(hr)){ goto done; }

	hr = MFCreateSourceReaderFromMediaSource(pMediaSource, pAttributes, ppSourceReader);

done:

	SAFE_RELEASE(pAttributes);
	SAFE_RELEASE(pMediaSource);

	return hr;
}

HRESULT CreateVideoCaptureSource(IMFMediaSource** ppSource){

	HRESULT hr = S_OK;
	IMFAttributes* pAttributes = NULL;
	UINT32 uiDevices = 0;
	IMFActivate** ppDevices = NULL;
	IMFMediaSource* pSource = NULL;
	// Change indexes according to your capture card
	int iVideoDeviceIndex = 0;
	int iVideoStreamIndex = 0;
	int iMediaTypeIndex = 2;
	IMFPresentationDescriptor* pPresentationDescriptor = NULL;
	BOOL bSelected = FALSE;
	IMFStreamDescriptor* pSourceSD = NULL;
	IMFMediaTypeHandler* pHandler = NULL;
	IMFMediaType* pType = NULL;

	hr = MFCreateAttributes(&pAttributes, 1);
	if(FAILED(hr)){ goto done; }

	hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if(FAILED(hr)){ goto done; }

	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &uiDevices);
	if(FAILED(hr)){ goto done; }

	// Be sure there is a video capture device
	hr = uiDevices > 0 ? S_OK : E_FAIL;
	if(FAILED(hr)){ goto done; }

	// Use the first video capture device (iVideoDeviceIndex == 0)
	hr = ppDevices[iVideoDeviceIndex]->ActivateObject(__uuidof(IMFMediaSource), reinterpret_cast<void**>(&pSource));
	if(FAILED(hr)){ goto done; }

	hr = pSource->CreatePresentationDescriptor(&pPresentationDescriptor);
	if(FAILED(hr)){ goto done; }

	// Get the first stream descriptor (iVideoStreamIndex == 0)
	hr = pPresentationDescriptor->GetStreamDescriptorByIndex(iVideoStreamIndex, &bSelected, &pSourceSD);
	if(FAILED(hr)){ goto done; }

	// Be sure stream descriptor is selected
	hr = bSelected ? S_OK : E_FAIL;
	if(FAILED(hr)){ goto done; }

	hr = pSourceSD->GetMediaTypeHandler(&pHandler);
	if(FAILED(hr)){ goto done; }

	// Get the first media type (iMediaTypeIndex == 0)
	hr = pHandler->GetMediaTypeByIndex(iMediaTypeIndex, &pType);
	if(FAILED(hr)){ goto done; }

	hr = pHandler->SetCurrentMediaType(pType);
	if(FAILED(hr)){ goto done; }

	*ppSource = pSource;
	(*ppSource)->AddRef();

done:

	SAFE_RELEASE(pType);
	SAFE_RELEASE(pHandler);
	SAFE_RELEASE(pSourceSD);
	SAFE_RELEASE(pPresentationDescriptor);
	SAFE_RELEASE(pSource);

	for(UINT32 i = 0; i < uiDevices; i++)
		SAFE_RELEASE(ppDevices[i]);

	CoTaskMemFree(ppDevices);

	SAFE_RELEASE(pAttributes);

	return hr;
}

HRESULT SetupSourceReaderMediaType(IMFSourceReader* pSourceReader, UINT32* puiWidth, UINT32* puiHeight){

	IMFMediaType* pMediaType = NULL;
	IMFMediaType* pVideoMediaType = NULL;

	HRESULT hr = pSourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pMediaType);
	if(FAILED(hr)){ goto done; }

	hr = MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, puiWidth, puiHeight);
	if(FAILED(hr)){ goto done; }

	hr = MFCreateMediaType(&pVideoMediaType);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if(FAILED(hr)){ goto done; }

	hr = MFSetAttributeRatio(pVideoMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if(FAILED(hr)){ goto done; }

	hr = CopyAttribute(pMediaType, pVideoMediaType, MF_MT_FRAME_SIZE);
	if(FAILED(hr)){ goto done; }

	hr = CopyAttribute(pMediaType, pVideoMediaType, MF_MT_FRAME_RATE);
	if(FAILED(hr)){ goto done; }

	hr = pSourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoMediaType);

done:

	SAFE_RELEASE(pVideoMediaType);
	SAFE_RELEASE(pMediaType);

	return hr;
}

HRESULT InitWindow(HWND& hWnd, const UINT32 uiWidth, const UINT32 uiHeight){

	WNDCLASSEX WndClassEx;

	WndClassEx.cbSize = sizeof(WNDCLASSEX);
	WndClassEx.style = CS_HREDRAW | CS_VREDRAW;
	WndClassEx.lpfnWndProc = WindowApplicationMsgProc;
	WndClassEx.cbClsExtra = 0L;
	WndClassEx.cbWndExtra = 0L;
	WndClassEx.hInstance = GetModuleHandle(NULL);
	WndClassEx.hIcon = NULL;
	WndClassEx.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClassEx.hbrBackground = NULL;
	WndClassEx.lpszMenuName = NULL;
	WndClassEx.lpszClassName = WINDOWAPPLICATION_CLASS;
	WndClassEx.hIconSm = NULL;

	if(!RegisterClassEx(&WndClassEx)) {
		return E_FAIL;
	}

	int iWndL = uiWidth + 8 + GetSystemMetrics(SM_CXSIZEFRAME) * 2;
	int iWndH = uiHeight + 8 + GetSystemMetrics(SM_CYSIZEFRAME) * 2 + GetSystemMetrics(SM_CYCAPTION);

	int iXWnd = (GetSystemMetrics(SM_CXSCREEN) - iWndL) / 2;
	int iYWnd = (GetSystemMetrics(SM_CYSCREEN) - iWndH) / 2;

	if((hWnd = CreateWindowEx(WS_EX_ACCEPTFILES, WINDOWAPPLICATION_CLASS, WINDOWAPPLICATION_CLASS, WS_OVERLAPPEDWINDOW, iXWnd, iYWnd,
		iWndL, iWndH, GetDesktopWindow(), NULL, GetModuleHandle(NULL), NULL)) == NULL) {
		return E_FAIL;
	}

	RECT rc;
	GetClientRect(hWnd, &rc);

	// If failed change iWndL or/and iWndH to be TRUE
	assert(rc.right == (LONG)uiWidth && rc.bottom == (LONG)uiHeight);

	ShowWindow(hWnd, SW_SHOW);

	return S_OK;
}

LRESULT CALLBACK WindowApplicationMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	if(msg == WM_PAINT) {

		ValidateRect(hWnd, NULL);
		return 0L;
	}
	else if(msg == WM_ERASEBKGND) {

		return 1L;
	}
	else if(msg == WM_CLOSE) {

		PostQuitMessage(0);
		return 0L;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

HRESULT InitMediaSink(IMFSourceReader* pSourceReader, IMFMediaSink** ppMediaSink, IMFStreamSink** ppStreamSink, const HWND hWnd, const UINT32 uiWidth, const UINT32 uiHeight){

	IMFActivate* pActivate = NULL;
	IMFVideoRenderer* pVideoRenderer = NULL;
	IMFGetService* pService = NULL;
	IMFVideoDisplayControl* pVideoDisplayControl = NULL;
	IMFMediaTypeHandler* pMediaTypeHandler = NULL;
	IMFMediaType* pMediaType = NULL;
	IMFMediaType* pVideoMediaType = NULL;
	RECT rc = {0, 0, (LONG)uiWidth, (LONG)uiHeight};

	HRESULT hr = MFCreateVideoRendererActivate(hWnd, &pActivate);
	if(FAILED(hr)){ goto done; }

	hr = pActivate->ActivateObject(IID_IMFMediaSink, reinterpret_cast<void**>(ppMediaSink));
	if(FAILED(hr)){ goto done; }

	hr = (*ppMediaSink)->QueryInterface(__uuidof(IMFVideoRenderer), reinterpret_cast<void**>(&pVideoRenderer));
	if(FAILED(hr)){ goto done; }

	hr = pVideoRenderer->InitializeRenderer(NULL, NULL);
	if(FAILED(hr)){ goto done; }

	hr = (*ppMediaSink)->QueryInterface(__uuidof(IMFGetService), reinterpret_cast<void**>(&pService));
	if(FAILED(hr)){ goto done; }

	hr = pService->GetService(MR_VIDEO_RENDER_SERVICE, __uuidof(IMFVideoDisplayControl), reinterpret_cast<void**>(&pVideoDisplayControl));
	if(FAILED(hr)){ goto done; }

	hr = pVideoDisplayControl->SetVideoWindow(hWnd);
	if(FAILED(hr)){ goto done; }

	hr = pVideoDisplayControl->SetVideoPosition(NULL, &rc);
	if(FAILED(hr)){ goto done; }

	hr = (*ppMediaSink)->GetStreamSinkByIndex(0, ppStreamSink);
	if(FAILED(hr)){ goto done; }

	hr = (*ppStreamSink)->GetMediaTypeHandler(&pMediaTypeHandler);
	if(FAILED(hr)){ goto done; }

	hr = pSourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pMediaType);
	if(FAILED(hr)){ goto done; }

	hr = MFCreateMediaType(&pVideoMediaType);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if(FAILED(hr)){ goto done; }

	hr = pVideoMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if(FAILED(hr)){ goto done; }

	hr = MFSetAttributeRatio(pVideoMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if(FAILED(hr)){ goto done; }

	hr = CopyAttribute(pMediaType, pVideoMediaType, MF_MT_FRAME_SIZE);
	if(FAILED(hr)){ goto done; }

	hr = CopyAttribute(pMediaType, pVideoMediaType, MF_MT_FRAME_RATE);
	if(FAILED(hr)){ goto done; }

	hr = pMediaTypeHandler->SetCurrentMediaType(pVideoMediaType);

done:

	SAFE_RELEASE(pVideoMediaType);
	SAFE_RELEASE(pMediaType);
	SAFE_RELEASE(pMediaTypeHandler);
	SAFE_RELEASE(pVideoDisplayControl);
	SAFE_RELEASE(pService);
	SAFE_RELEASE(pVideoRenderer);
	SAFE_RELEASE(pActivate);

	return hr;
}

HRESULT InitSampleAllocator(IMFMediaSink* pMediaSink, IMFStreamSink* pStreamSink, IMFVideoSampleAllocator** ppVideoSampleAllocator, IMFSample** ppD3DVideoSample){

	IDirect3DDeviceManager9* pD3D9Manager = NULL;
	IMFMediaTypeHandler* pMediaTypeHandler = NULL;
	IMFMediaType* pMediaType = NULL;

	HRESULT hr = MFGetService(pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(ppVideoSampleAllocator));
	if(FAILED(hr)){ goto done; }

	hr = MFGetService(pMediaSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pD3D9Manager));
	if(FAILED(hr)){ goto done; }

	hr = (*ppVideoSampleAllocator)->SetDirectXManager(pD3D9Manager);
	if(FAILED(hr)){ goto done; }

	hr = pStreamSink->GetMediaTypeHandler(&pMediaTypeHandler);
	if(FAILED(hr)){ goto done; }

	hr = pMediaTypeHandler->GetCurrentMediaType(&pMediaType);
	if(FAILED(hr)){ goto done; }

	hr = (*ppVideoSampleAllocator)->InitializeSampleAllocator(1, pMediaType);
	if(FAILED(hr)){ goto done; }

	hr = (*ppVideoSampleAllocator)->AllocateSample(ppD3DVideoSample);

done:

	SAFE_RELEASE(pMediaType);
	SAFE_RELEASE(pMediaTypeHandler);
	SAFE_RELEASE(pD3D9Manager);

	return hr;
}

HRESULT InitClock(IMFMediaSink* pMediaSink, IMFPresentationClock** ppClock){

	IMFPresentationTimeSource* pTimeSource = NULL;

	HRESULT hr = MFCreatePresentationClock(ppClock);
	if(FAILED(hr)){ goto done; }

	hr = MFCreateSystemTimeSource(&pTimeSource);
	if(FAILED(hr)){ goto done; }

	hr = (*ppClock)->SetTimeSource(pTimeSource);
	if(FAILED(hr)){ goto done; }

	hr = pMediaSink->SetPresentationClock(*ppClock);
	if(FAILED(hr)){ goto done; }

	hr = (*ppClock)->Start(0);

	// Wait for the Sink to start, we should handle event Media Sink
	Sleep(1000);

done:

	SAFE_RELEASE(pTimeSource);

	return hr;
}

HRESULT DisplayVideo(IMFSourceReader* pSourceReader, IMFStreamSink* pStreamSink, IMFSample* pD3DVideoSample){

	HRESULT hr = S_OK;

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));

	while(msg.message != WM_QUIT){

		if(PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)){

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else{

			hr = RenderFrame(pSourceReader, pStreamSink, pD3DVideoSample);

			if(FAILED(hr)){

				PostQuitMessage(0);
			}
		}
	}

	return hr;
}

HRESULT RenderFrame(IMFSourceReader* pSourceReader, IMFStreamSink* pStreamSink, IMFSample* pD3DVideoSample){

	IMFSample* pVideoSample = NULL;
	IMFMediaBuffer* pSrcBuffer = NULL;
	IMFMediaBuffer* pDstBuffer = NULL;
	IMF2DBuffer* p2DBuffer = NULL;

	DWORD dwStreamIndex;
	DWORD dwtreamFlags;
	LONGLONG llTimeStamp;
	BYTE* pbBuffer = NULL;
	DWORD dwBuffer = 0;
	UINT32 uiAttribute = 0;

	HRESULT hr = pSourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &dwStreamIndex, &dwtreamFlags, &llTimeStamp, &pVideoSample);

	// todo : check what is llTimeStamp from ReadSample (avoid GetSampleTime or GetSampleDuration)

	if(dwtreamFlags != 0)
		goto done;

	// Time/Duration
	hr = pD3DVideoSample->SetSampleTime(g_llTimeStamp);
	if(FAILED(hr)){ goto done; }

	hr = pVideoSample->GetSampleDuration(&llTimeStamp);
	hr = pD3DVideoSample->SetSampleDuration(llTimeStamp);

	g_llTimeStamp += llTimeStamp;

	// Buffer
	hr = pVideoSample->ConvertToContiguousBuffer(&pSrcBuffer);
	if(FAILED(hr)){ goto done; }

	hr = pSrcBuffer->Lock(&pbBuffer, NULL, &dwBuffer);
	if(FAILED(hr)){ goto done; }

	hr = pD3DVideoSample->GetBufferByIndex(0, &pDstBuffer);
	if(FAILED(hr)){ goto done; }

	hr = pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
	if(FAILED(hr)){ goto done; }

	hr = p2DBuffer->ContiguousCopyFrom(pbBuffer, dwBuffer);
	if(FAILED(hr)){ goto done; }

	hr = pSrcBuffer->Unlock();
	if(FAILED(hr)){ goto done; }

	// Attributes
	hr = pVideoSample->GetUINT32(MFSampleExtension_Discontinuity, &uiAttribute);
	if(FAILED(hr)){ goto done; }

	hr = pD3DVideoSample->SetUINT32(MFSampleExtension_Discontinuity, uiAttribute);
	if(FAILED(hr)){ goto done; }

	hr = pVideoSample->GetUINT32(MFSampleExtension_CleanPoint, &uiAttribute);
	if(FAILED(hr)){ goto done; }

	hr = pD3DVideoSample->SetUINT32(MFSampleExtension_CleanPoint, uiAttribute);
	if(FAILED(hr)){ goto done; }

	hr = pStreamSink->ProcessSample(pD3DVideoSample);

done:

	// todo : Unlock if error after Lock and before Unlock

	SAFE_RELEASE(p2DBuffer);
	SAFE_RELEASE(pDstBuffer);
	SAFE_RELEASE(pSrcBuffer);
	SAFE_RELEASE(pVideoSample);

	return hr;
}

HRESULT CopyAttribute(IMFAttributes* pFrom, IMFAttributes* pTo, REFGUID guidKey){

	PROPVARIANT val;

	HRESULT hr = pFrom->GetItem(guidKey, &val);

	if(SUCCEEDED(hr)){

		hr = pTo->SetItem(guidKey, val);
		PropVariantClear(&val);
	}
	else if(hr == MF_E_ATTRIBUTENOTFOUND){

		hr = S_OK;
	}

	return hr;
}