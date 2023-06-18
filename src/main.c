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

#define CS_FILE strc("..\\src\\shader.hlsl")
#define CS_ENTRYPOINT "CSMain"
#define CS_FEATURE_LEVEL "cs_5_0"
#define CS_COMPILE_FLAGS (D3DCOMPILE_DEBUG | D3DCOMPILE_IEEE_STRICTNESS)
#define CS_NUM_THREADS_W 16
#define CS_NUM_THREADS_H 8
/* NOTE(lcf): Calculate number of threads to cover the dimension, rounding up
   instead of down by adding the divisor -1. */
#define CS_DISPATCH_COUNT(x,n) ((x+n-1)/n)

struct CS_Constants {
    u32 iFrame;
    f32 iTime;
    f32 RenderWidth;
    f32 RenderHeight;
};
global HMM_Vec3 Pos;
global HMM_Vec3 Look = {0.0, 0.0, 1.0};
global HMM_Vec3 Up = {0.0, 1.0, 0.0};

/* TODO: move to lcf; */
global Arena *Scratch;

global HWND Window;
global ID3D11Device1 *Device;
global ID3D11DeviceContext1 *DeviceContext;
global IDXGISwapChain1 *SwapChain;
global b32 DoNotWaitForVsync;

global DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
global ID3D11RenderTargetView *FrameBufferView;
global ID3D11DepthStencilView *DepthStencilView;
global ID3D11UnorderedAccessView *FrameBufferUAV;
global ID3D11UnorderedAccessView *ComputeBufferUAV;
global D3D11_VIEWPORT AppViewport;

global b32 SwapChainExists;
global s32 RenderWidth;
global s32 RenderHeight;
global s32 FrameCount;
internal void TeardownSwapChain(void);
internal void SetupSwapChain(void);

global ID3D11ComputeShader* Shader;
internal void RecompileShader();

global b32 Quit;

LRESULT WINAPI CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
        /* TODO: let app know that user is trying to quit somehow? */
    } break;
    case WM_SIZE: {
    } break;
    default: {
        Result = DefWindowProcA(hwnd, msg, wParam, lParam);
    } break;
    }
    return Result;
}

global u32 presentationFlags = 0;
int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow)
{
    (void) hinstance, previnstance, cmdline, cmdshow;

    os_Init();
    Scratch = Arena_create();

    /* Configuration */
    if (DoNotWaitForVsync) {
        presentationFlags |= DXGI_PRESENT_DO_NOT_WAIT;
    }

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
    SetupSwapChain();
    RecompileShader();

    /* Compile Shader and Set up needed buffers*/
    ID3D11Buffer *ConstantsBuffer;
    {
        /* NOTE(lcf): sizeof() must be a multiple of 16 due to buffer config. */
        ASSERTSTATIC(sizeof(struct CS_Constants) % 16 == 0, constantsize16);
        D3D11_BUFFER_DESC constantsDesc = {
            sizeof(struct CS_Constants),
            D3D11_USAGE_DYNAMIC,
            D3D11_BIND_CONSTANT_BUFFER,
            D3D11_CPU_ACCESS_WRITE,
            0, /* No misc flags */
            0 /* Not a structured buffer so stride not needed. */
        };
        Device->lpVtbl->CreateBuffer(Device, &constantsDesc, 0, &ConstantsBuffer);
    }

    /* Message Loop */
    s64 startTime = os_GetTimeMicroseconds();
    s64 flipTime = startTime;
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

        /* Rebuild SwapChain and Device if needed. */
        SetupSwapChain();
        RecompileShader();

        /* Update Shader Constants */
        {
            struct CS_Constants frameConstants;
            frameConstants.iFrame = FrameCount++;
            frameConstants.iTime = (flipTime - startTime)  * 0.000001f;
            frameConstants.RenderWidth = (f32) RenderWidth;
            frameConstants.RenderHeight = (f32) RenderHeight;
            
            D3D11_MAPPED_SUBRESOURCE resource;	
            DeviceContext->lpVtbl->Map(DeviceContext, (ID3D11Resource*) ConstantsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
            memcpy(resource.pData, &frameConstants, sizeof(frameConstants));
            DeviceContext->lpVtbl->Unmap(DeviceContext, (ID3D11Resource*) ConstantsBuffer, 0);
        }
        
        /* Rendering */
        ID3D11UnorderedAccessView* UAVs[] = {FrameBufferUAV, ComputeBufferUAV};
        DeviceContext->lpVtbl->CSSetShader(DeviceContext, Shader, 0, 0);
        DeviceContext->lpVtbl->CSSetUnorderedAccessViews(DeviceContext, 0, 2, UAVs, 0);
        DeviceContext->lpVtbl->CSSetConstantBuffers(DeviceContext, 0, 1, &ConstantsBuffer);
        DeviceContext->lpVtbl->Dispatch(DeviceContext, CS_DISPATCH_COUNT(RenderWidth, CS_NUM_THREADS_W), CS_DISPATCH_COUNT(RenderHeight, CS_NUM_THREADS_H), 1);
        
        SwapChain->lpVtbl->Present(SwapChain, 0, presentationFlags);
    }

    
    /* Free Swap Chain */
    TeardownSwapChain();
    SAFE_RELEASE(Shader);
    SAFE_RELEASE(SwapChain);
    SAFE_RELEASE(DeviceContext);

    ID3D11Debug *Debug;
    Device->lpVtbl->QueryInterface(Device, &IID_ID3D11Debug, &Debug);
    Debug->lpVtbl->ReportLiveDeviceObjects(Debug, 0x2);
    SAFE_RELEASE(Device);

}

internal void RecompileShader()
{
    internal u64 CSFileLastWriteTime = 0;
    if (os_FileWasWritten(CS_FILE, &CSFileLastWriteTime)) {
        ID3D11ComputeShader *newShader;
        ID3DBlob *csblob = 0;
        ID3DBlob *errblob = 0;
        str shader = os_ReadFile(Scratch, CS_FILE);
        HRESULT hr = D3DCompile(shader.str, shader.len, 0, 0, 0, CS_ENTRYPOINT, CS_FEATURE_LEVEL, 1 << 15, 0, &csblob, &errblob);
        if (!SUCCEEDED(hr)) {
            if (errblob) {
                OutputDebugStringA((ch8*) errblob->lpVtbl->GetBufferPointer(errblob));
            }
        } else {
            HR(Device->lpVtbl->CreateComputeShader(Device, csblob->lpVtbl->GetBufferPointer(csblob), csblob->lpVtbl->GetBufferSize(csblob), 0, &newShader));

            SAFE_RELEASE(Shader);
            Shader = newShader;
            FrameCount = 0;
        }        
        SAFE_RELEASE(csblob);
        SAFE_RELEASE(errblob);
    }
}

internal void SetupSwapChain(void)
{
    RECT client;
    ASSERT(GetClientRect(Window, &client));
    
    if (!(RenderWidth == client.right && RenderHeight == client.bottom)) {
        if (SwapChainExists) {
            TeardownSwapChain();
        } else { /* Create the swap chain */
            /* TODO(lcf): eventually this should be parameterized. For now I'm just picking
               settings that work best on my machine. */

            /* Init D3D11 */
            u32 flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

            /* TODO WARN(lcf): this should be wrapped in a feature macro */
            flags |= D3D11_CREATE_DEVICE_DEBUG;

            D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
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
               create a modern swap chain to use for rendering. So we go through several more layers
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
            dxgiFactory->lpVtbl->MakeWindowAssociation(dxgiFactory, Window, DXGI_MWA_NO_ALT_ENTER);
            
            swapChainDesc.Width              = client.right; /* use window width */
            swapChainDesc.Height             = client.bottom; /* use window height */
            swapChainDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.Stereo             = FALSE; /* enable 3d glasses */
            swapChainDesc.SampleDesc.Count   = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT
                /* NOTE(lcf): Add unordered access for uav. */
                | DXGI_USAGE_UNORDERED_ACCESS; 
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

        SwapChain->lpVtbl->ResizeBuffers(SwapChain, 0,
                                         client.right, client.bottom,
                                         DXGI_FORMAT_UNKNOWN,
                                         DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
                                         | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

        RenderWidth = client.right;
        RenderHeight = client.bottom;
        FrameCount = 0;
        
        { /* Create Render Target */
            D3D11_TEXTURE2D_DESC depthBufferDesc;
            ID3D11Texture2D *frameBuffer;
            ID3D11Texture2D *depthBuffer;

            SwapChain->lpVtbl->GetBuffer(SwapChain, 0, &IID_ID3D11Texture2D, &frameBuffer);
            Device->lpVtbl->CreateRenderTargetView(Device, (ID3D11Resource*) frameBuffer, 0, &FrameBufferView);
                        
            frameBuffer->lpVtbl->GetDesc(frameBuffer, &depthBufferDesc);
            depthBufferDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            Device->lpVtbl->CreateTexture2D(Device, &depthBufferDesc, 0, &depthBuffer);
            Device->lpVtbl->CreateDepthStencilView(Device, (ID3D11Resource*) depthBuffer, 0, &DepthStencilView);
            

            AppViewport = (D3D11_VIEWPORT) { 0.0f, 0.0f, (f32) depthBufferDesc.Width, (f32) depthBufferDesc.Height, 0.0f, 1.0f };

            /* Compute Shaders */
            /* Create UAV for frame buffer so compute shader can access it. */
            Device->lpVtbl->CreateUnorderedAccessView(Device, (ID3D11Resource*) frameBuffer, 0, &FrameBufferUAV);

            /* And an additional buffer of the same size to store compute data */
            D3D11_TEXTURE2D_DESC computeBufferDesc = depthBufferDesc;
            computeBufferDesc.Format    = DXGI_FORMAT_R32G32B32A32_FLOAT;
            computeBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            ID3D11Texture2D *computeBuffer;
            HR(Device->lpVtbl->CreateTexture2D(Device, &computeBufferDesc, 0, &computeBuffer));
            HR(Device->lpVtbl->CreateUnorderedAccessView(Device, (ID3D11Resource*) computeBuffer, 0, &ComputeBufferUAV));

            SAFE_RELEASE(computeBuffer);
            SAFE_RELEASE(frameBuffer);
            SAFE_RELEASE(depthBuffer);
        }


    }
}

internal void TeardownSwapChain(void)
{
    SAFE_RELEASE(FrameBufferView);
    SAFE_RELEASE(DepthStencilView);
    SAFE_RELEASE(FrameBufferUAV);
    SAFE_RELEASE(ComputeBufferUAV);
    AppViewport = (D3D11_VIEWPORT) {0};    
}

