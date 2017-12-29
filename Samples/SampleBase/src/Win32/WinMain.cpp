/*     Copyright 2015-2017 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include <memory>
#include <iomanip>
#include <Windows.h>
#include "SampleBase.h"
#include "RenderDeviceFactoryD3D11.h"
#include "RenderDeviceFactoryD3D12.h"
#include "RenderDeviceFactoryOpenGL.h"
#include "Timer.h"

using namespace Diligent;

#define TW_STATIC
#include "AntTweakBar.h"

std::unique_ptr<SampleBase> g_pSample;

LRESULT CALLBACK MessageProc(HWND, UINT, WPARAM, LPARAM);

// Create Direct3D device and swap chain
void InitDevice(HWND hWnd, IRenderDevice **ppRenderDevice, IDeviceContext **ppImmediateContext,  ISwapChain **ppSwapChain, DeviceType DevType)
{
    SwapChainDesc SCDesc;
    SCDesc.SamplesCount = 1;
    switch (DevType)
    {
        case DeviceType::D3D11:
        {
            EngineD3D11Attribs DeviceAttribs;
            DeviceAttribs.DebugFlags = (Uint32)EngineD3D11DebugFlags::VerifyCommittedShaderResources |
                                       (Uint32)EngineD3D11DebugFlags::VerifyCommittedResourceRelevance;

#ifdef ENGINE_DLL
            GetEngineFactoryD3D11Type GetEngineFactoryD3D11 = nullptr;
            // Load the dll and import GetEngineFactoryD3D11() function
            LoadGraphicsEngineD3D11(GetEngineFactoryD3D11);
#endif
            auto *pFactoryD3D11 = GetEngineFactoryD3D11();
            pFactoryD3D11->CreateDeviceAndContextsD3D11( DeviceAttribs, ppRenderDevice, ppImmediateContext, 0 );
            pFactoryD3D11->CreateSwapChainD3D11( *ppRenderDevice, *ppImmediateContext, SCDesc, hWnd, ppSwapChain );
        }
        break;

        case DeviceType::D3D12:
        {
#ifdef ENGINE_DLL
            GetEngineFactoryD3D12Type GetEngineFactoryD3D12 = nullptr;
            // Load the dll and import GetEngineFactoryD3D12() function
            LoadGraphicsEngineD3D12(GetEngineFactoryD3D12);
#endif

            auto *pFactoryD3D12 = GetEngineFactoryD3D12();
            EngineD3D12Attribs EngD3D12Attribs;
			EngD3D12Attribs.GPUDescriptorHeapDynamicSize[0] = 32768;
			EngD3D12Attribs.GPUDescriptorHeapSize[1] = 128;
			EngD3D12Attribs.GPUDescriptorHeapDynamicSize[1] = 2048-128;
			EngD3D12Attribs.DynamicDescriptorAllocationChunkSize[0] = 32;
			EngD3D12Attribs.DynamicDescriptorAllocationChunkSize[1] = 8; // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            pFactoryD3D12->CreateDeviceAndContextsD3D12( EngD3D12Attribs, ppRenderDevice, ppImmediateContext, 0);
            pFactoryD3D12->CreateSwapChainD3D12( *ppRenderDevice, *ppImmediateContext, SCDesc, hWnd, ppSwapChain );
        }
        break;

        case DeviceType::OpenGL:
        {
#ifdef ENGINE_DLL
            // Declare function pointer
            GetEngineFactoryOpenGLType GetEngineFactoryOpenGL = nullptr;
            // Load the dll and import GetEngineFactoryOpenGL() function
            LoadGraphicsEngineOpenGL(GetEngineFactoryOpenGL);
#endif
            EngineCreationAttribs EngineCreationAttribs;
            GetEngineFactoryOpenGL()->CreateDeviceAndSwapChainGL(
                EngineCreationAttribs, ppRenderDevice, ppImmediateContext, SCDesc, hWnd, ppSwapChain );
        }
        break;

        default:
            LOG_ERROR_AND_THROW("Unknown device type");
        break;
    }
}

ISwapChain *g_pSwapChain;

// Main
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int cmdShow)
{
#if defined(_DEBUG) || defined(DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    std::wstring Title = L"Graphics engine sample";

    DeviceType DevType = DeviceType::Undefined;
    std::wstring CmdLine = GetCommandLine();
    std::wstring Key = L"mode=";
    auto pos = CmdLine.find( Key );
    if( pos != std::string::npos )
    {
        pos += Key.length();
        auto Val = CmdLine.substr( pos );
        if(Val == L"D3D11")
        {
            DevType = DeviceType::D3D11;
            Title.append( L" (D3D11)" );
        }
        else if(Val == L"D3D12")
        {
            DevType = DeviceType::D3D12;
            Title.append( L" (D3D12)" );
        }
        else if(Val == L"GL")
        {
            DevType = DeviceType::OpenGL;
            Title.append( L" (OpenGL)" );
        }
        else
        {
            LOG_ERROR("Unknown device type. Only the following types are supported: D3D11, D3D12, GL");
            return -1;
        }
    }
    else
    {
        LOG_INFO_MESSAGE("Device type is not specified. Using D3D11 device");
        DevType = DeviceType::D3D11;
        Title.append( L" (D3D11)" );
    }
    // Register our window class
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW|CS_VREDRAW, MessageProc,
                        0L, 0L, instance, NULL, NULL, NULL, NULL, L"SampleApp", NULL };
    RegisterClassEx(&wcex);

    // Create a window
    RECT rc = { 0, 0, 1280, 1024 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND wnd = CreateWindow(L"SampleApp", Title.c_str(), 
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 
                            rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, instance, NULL);
    if (!wnd)
    {
        MessageBox(NULL, L"Cannot create window", L"Error", MB_OK|MB_ICONERROR);
        return 0;
    }
    ShowWindow(wnd, cmdShow);
    UpdateWindow(wnd);

    RefCntAutoPtr<IRenderDevice> pRenderDevice;
    RefCntAutoPtr<IDeviceContext> pDeviceContext;
    Diligent::RefCntAutoPtr<ISwapChain> pSwapChain;
    InitDevice( wnd, &pRenderDevice, &pDeviceContext, &pSwapChain, DevType );
    g_pSwapChain = pSwapChain;

    // Initialize AntTweakBar
    // TW_OPENGL and TW_OPENGL_CORE were designed to select rendering with 
    // very old GL specification. Using these modes results in applying some 
    // odd offsets which distorts everything
    // Latest OpenGL works very much like Direct3D11, and 
    // Tweak Bar will never know if D3D or OpenGL is actually used
    if (!TwInit(TW_DIRECT3D11, pRenderDevice.RawPtr(), pDeviceContext.RawPtr(), pSwapChain->GetDesc().ColorBufferFormat))
    {
        MessageBoxA(wnd, TwGetLastError(), "AntTweakBar initialization failed", MB_OK|MB_ICONERROR);
        return 0;
    }
    TwDefine(" TW_HELP visible=false ");

    g_pSample.reset( CreateSample(pRenderDevice, pDeviceContext, pSwapChain) );
    g_pSample->WindowResize( g_pSwapChain->GetDesc().Width, g_pSwapChain->GetDesc().Height );


    Timer Timer;
    auto PrevTime = Timer.GetElapsedTime();
    double filteredFrameTime = 0.0;

    // Main message loop
    MSG msg = {0};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            auto CurrTime = Timer.GetElapsedTime();
            auto ElapsedTime = CurrTime - PrevTime;
            PrevTime = CurrTime;
            g_pSample->Update(CurrTime, ElapsedTime);
            g_pSample->Render();

	        // Draw tweak bars
            // Restore default render target in case the sample has changed it
            pDeviceContext->SetRenderTargets(0, nullptr, nullptr);
            TwDraw();

            pSwapChain->Present();

            double filterScale = 0.2;
            filteredFrameTime = filteredFrameTime * (1.0 - filterScale) + filterScale * ElapsedTime;
            std::wstringstream fpsCounterSS;
            fpsCounterSS << " - " << std::fixed << std::setprecision(1) << filteredFrameTime * 1000;
            fpsCounterSS << " ms (" << 1.0 / filteredFrameTime << " fps)";
            SetWindowText(wnd, (Title + fpsCounterSS.str()).c_str());
        }
    }
    
    TwTerminate();

	//pDeviceContext->Flush();
    g_pSample.reset();
    pRenderDevice.Release();

    return (int)msg.wParam;
}

// Called every time the application receives a message
LRESULT CALLBACK MessageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Send event message to AntTweakBar
    if (TwEventWin(wnd, message, wParam, lParam))
        return 0; // Event has been handled by AntTweakBar
    
    switch (message) 
    {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(wnd, &ps);
                EndPaint(wnd, &ps);
                return 0;
            }
        case WM_SIZE: // Window size has been changed
            if( g_pSwapChain )
            {
                g_pSwapChain->Resize(LOWORD(lParam), HIWORD(lParam));
                g_pSample->WindowResize( g_pSwapChain->GetDesc().Width, g_pSwapChain->GetDesc().Height );
            }
            return 0;
        case WM_CHAR:
            if (wParam == VK_ESCAPE)
                PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
        {
            struct WindowMessageData
            {
                HWND hWnd;
                UINT message;
                WPARAM wParam;
                LPARAM lParam;
            }msg{wnd, message, wParam, lParam};
            if (g_pSample != nullptr && g_pSample->HandleNativeMessage(&msg) )
                return 0;
            else
                return DefWindowProc(wnd, message, wParam, lParam);
        }
    }
}
