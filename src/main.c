#define TITLE "PathTracerGPU"
#define TARGET_RENDER_W 1280
#define TARGET_RENDER_H 720

#include "lcf/lcf.h"
#include "lcf/lcf.c"

#include "stb/stb_image_write.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid")

#include "HandmadeMath/HandmadeMath.h"
typedef HMM_Vec3 vec3;

#include <stdlib.h>
#pragma comment(lib,"user32.lib") 


static void SetupRender(void);

global HWND Window;
global ID3D11Device1* Device;
global ID3D11DeviceContext1* DeviceContext;
global IDXGISwapChain1 *SwapChain;
global b32 DoNotWaitForVsync;

global DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
global ID3D11RenderTargetView *FrameBufferView;
global ID3D11DepthStencilView* DepthBufferView;
global D3D11_VIEWPORT AppViewport;

global b32 Quit;

LRESULT WINAPI CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* TODO: handle window closed messages and such, close audio thread and do other needed
       cleanup things */
    LRESULT Result = 0;

    switch (msg) {
        /* TODO: handle resizing / moving window
           REF: https://stackoverflow.com/questions/28406919/redraw-window-using-directx-during-move-resize
        */
    case WM_DESTROY: {
        if (!Quit) {
            ASSERTM(false, "Window should not be destroyed by OS");
        }
        /* TODO: remake window */
    } break;
    case WM_CLOSE: {
        Quit = true;
        /* TODO: let game know that user is trying to quit somehow? */
    } break;
    case WM_SIZE: {
    } break;
    default: {
        Result = DefWindowProcA(hwnd, msg, wParam, lParam);
    } break;
    }
    return Result;
}

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow) {
    (void) hinstance, previnstance, cmdline, cmdshow;

    os_Init();
    Arena *arena = Arena_create();

    { /* Receive a message from ourself to make window active. */
        PostQuitMessage(0);
        MSG msg;
        GetMessage(&msg, 0, 0, 0);
    }

    { /* Init Window */
        WNDCLASSEXA wndClassEx = {sizeof(wndClassEx), 0};
        wndClassEx.lpfnWndProc   = WndProc;
        wndClassEx.lpszClassName = TITLE;
        wndClassEx.hInstance = GetModuleHandleA(0);
        wndClassEx.hCursor = LoadCursor(0, IDC_ARROW);
        wndClassEx.hIcon = LoadIcon(0, IDI_WINLOGO);
        RegisterClassExA(&wndClassEx);

        Window = CreateWindowExA(
            0, TITLE, TITLE,
            WS_VISIBLE /* Start visible */
            | WS_CAPTION /* Title bar, required for top menu */
            | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX /* Top menu with min/max/close buttons */
            | WS_SIZEBOX /* Resizeable */
            , 
            CW_USEDEFAULT, CW_USEDEFAULT, TARGET_RENDER_W, TARGET_RENDER_H, /* x,y,w,h */
            0, 0, 0, 0
            );
    }

    /* Setup Render Target */
    SetupRender();

    /* Message Loop */
    s64 flipTime = 0;
    s64 lastFlipTime = 0;
    while (!Quit) {
        lastFlipTime = flipTime;
        flipTime = os_GetTimeMicroseconds();
        
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            switch (msg.message) {
            case WM_CLOSE:
            case WM_DESTROY:
            case WM_QUIT: {
                Quit = true;
            } break;
            case WM_KEYDOWN: {
                switch (msg.wParam) {
                case VK_ESCAPE: {
                    Quit = true;
                } break;
                }
            } break;
            case WM_SETCURSOR:
            {
                SetCursor(0);
            } break;
            }
            TranslateMessage(&msg); 
            DispatchMessageA(&msg);
        }

        SetupRender();
        
        u32 flags = 0;
        if (DoNotWaitForVsync) {
            flags |= DXGI_PRESENT_DO_NOT_WAIT;
        }
        SwapChain->lpVtbl->Present(SwapChain, true, flags);
    }
    
    /* Free Swap Chain */
    SAFE_RELEASE(SwapChain);
    SAFE_RELEASE(DeviceContext);
    SAFE_RELEASE(Device);
}

global b32 SwapChainExists;
global s32 RenderWidth;
global s32 RenderHeight;
static void SetupRender(void) {
    RECT client;
    ASSERT(GetClientRect(Window, &client));
    
    if (!(RenderWidth == client.right && RenderHeight == client.bottom)) {
        if (SwapChainExists) {
            SAFE_RELEASE(FrameBufferView);
            SAFE_RELEASE(DepthBufferView);
            AppViewport = (D3D11_VIEWPORT) {0};
        } else { /* Create the swap chain */
            /* TODO(lcf): eventually this should be parameterized. For now I'm just picking
               settings that work best on my machine. */

            /* Init D3D11 */
            u32 flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

            /* TODO WARN(lcf): this should be wrapped in a feature macro */
            flags |= D3D11_CREATE_DEVICE_DEBUG;

            D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
            ID3D11Device* baseDevice;
            ID3D11DeviceContext* baseDeviceContext;
            D3D11CreateDevice(
                0, /* Use Default video adapter. */
                D3D_DRIVER_TYPE_HARDWARE, /* Use Hardware Accelerated Driver for the Device */
                0, /* Handle for the dll of the software renderer we are using (lol!) */
                flags,

                featureLevels, /* Array of feature levels + fallbacks, in order we would prefer them*/
                ARRAYSIZE(featureLevels),
        
                D3D11_SDK_VERSION, /* "The SDK version; use D3D11_SDK_VERSION." - MSDN */
        
                &baseDevice, /* Will hold the device object created. */
                0, /* If not null, this will hold the feature level that was chosen. */
                &baseDeviceContext /* This holds the device context object created. */
                );

            /* ID3D11Device1 is an object inheriting from ID3D11Device, with some extra methods.
               These COM procedures make sure that the device and context we just received can be
               used as an ID3D11Device1.
    
               However, what we actually want to get to is the IXDGIFactory2, which will allow us to
               create a modern swap chain (double to use for rendering. So we go through several more layers
               of COM B.S. to get there.
            */
            baseDevice->lpVtbl->QueryInterface(baseDevice, &IID_ID3D11Device1, &Device);
            baseDeviceContext->lpVtbl->QueryInterface(baseDeviceContext, &IID_ID3D11DeviceContext1, &DeviceContext);

            IDXGIDevice1* dxgiDevice;
            IDXGIAdapter* dxgiAdapter;
            IDXGIFactory2* dxgiFactory;
            HR(Device->lpVtbl->QueryInterface(Device, &IID_IDXGIDevice1, &dxgiDevice));
            HR(dxgiDevice->lpVtbl->GetAdapter(dxgiDevice, &dxgiAdapter));
            HR(dxgiAdapter->lpVtbl->GetParent(dxgiAdapter, &IID_IDXGIFactory2, &dxgiFactory));

            /* Decrease frame latency and alt-enter fullscreen since it borks everything. */
            dxgiDevice->lpVtbl->SetMaximumFrameLatency(dxgiDevice, 1);
            dxgiFactory->lpVtbl->MakeWindowAssociation(dxgiFactory, Window,
                                               DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
            
            swapChainDesc.Width              = client.right; /* use window width */
            swapChainDesc.Height             = client.bottom; /* use window height */
            swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.Stereo             = FALSE; /* enable 3d glasses */
            swapChainDesc.SampleDesc.Count   = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.BufferCount        = 2;
            swapChainDesc.Scaling            = DXGI_SCALING_STRETCH;
            swapChainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
            swapChainDesc.Flags              = 0
                | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
                | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            dxgiFactory->lpVtbl->CreateSwapChainForHwnd(dxgiFactory, (IUnknown*) Device, Window, &swapChainDesc, 0, 0, &SwapChain);
            dxgiDevice->lpVtbl->Release(dxgiDevice);
            dxgiAdapter->lpVtbl->Release(dxgiAdapter);
            dxgiFactory->lpVtbl->Release(dxgiFactory);

            SwapChainExists = true;
        }

        SwapChain->lpVtbl->ResizeBuffers(SwapChain, 0, client.right, client.bottom, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
        
        { /* Create Render Target */
            D3D11_TEXTURE2D_DESC DepthBufferDesc;
            ID3D11Texture2D* FrameBuffer;
            ID3D11Texture2D* DepthBuffer;

            SwapChain->lpVtbl->GetBuffer(SwapChain, 0, &IID_ID3D11Texture2D, &FrameBuffer);
            Device->lpVtbl->CreateRenderTargetView(Device, (ID3D11Resource*) FrameBuffer, 0, &FrameBufferView);

            FrameBuffer->lpVtbl->GetDesc(FrameBuffer, &DepthBufferDesc);
            DepthBufferDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
            DepthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            Device->lpVtbl->CreateTexture2D(Device, &DepthBufferDesc, 0, &DepthBuffer);
            Device->lpVtbl->CreateDepthStencilView(Device, (ID3D11Resource*) DepthBuffer, 0, &DepthBufferView);

            AppViewport = (D3D11_VIEWPORT) { 0.0f, 0.0f, (f32) DepthBufferDesc.Width, (f32) DepthBufferDesc.Height, 0.0f, 1.0f };

            FrameBuffer->lpVtbl->Release(FrameBuffer);
            DepthBuffer->lpVtbl->Release(DepthBuffer);
        }
    }
}
