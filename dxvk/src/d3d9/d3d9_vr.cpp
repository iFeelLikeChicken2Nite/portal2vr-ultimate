#include "../dxvk/dxvk_include.h"

#include "d3d9_vr.h"

#include "d3d9_include.h"
#include "d3d9_surface.h"

#include "d3d9_device.h"

#include "L4D2VR/game.h"
#include "L4D2VR/openvr_session.h"
#include "L4D2VR/vr.h"

namespace dxvk {

    Portal2VRBridge::Registry<D3D9DeviceEx, IDirect3DVR9>& GetD3D9VRBridgeRegistry()
    {
        // Deliberately lives until process exit; device Release removes every
        // borrowed entry before the last public reference can disappear.
        static auto* registry = new Portal2VRBridge::Registry<D3D9DeviceEx, IDirect3DVR9>();
        return *registry;
    }

    class D3D9VR final : public ComObjectClamp<IDirect3DVR9>
    {
    public:

        D3D9VR(IDirect3DDevice9 *pDevice)
            : m_device(static_cast<D3D9DeviceEx *>(pDevice))
        {
            vr::EVRInitError error = vr::VRInitError_None;
            m_openVRSession.Acquire(error);
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID riid,
            void **ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            *ppvObject = nullptr;

            if (riid == __uuidof(IUnknown) ||
                riid == __uuidof(IDirect3DVR9)) {
                *ppvObject = ref(this);
                return S_OK;
            }

            Logger::warn("D3D9VR::QueryInterface: Unknown interface query");
            Logger::warn(str::format(riid));
            return E_NOINTERFACE;
        }

        HRESULT STDMETHODCALLTYPE GetVRDesc(
            IDirect3DSurface9 *pSurface,
            D3D9_TEXTURE_VR_DESC *pDesc)
        {
            if (unlikely(pSurface == nullptr || pDesc == nullptr))
                return D3DERR_INVALIDCALL;

            IDirect3DDevice9* surfaceDevice = nullptr;
            const HRESULT deviceResult = pSurface->GetDevice(&surfaceDevice);
            if (FAILED(deviceResult) || !surfaceDevice) {
                if (surfaceDevice)
                    surfaceDevice->Release();
                return D3DERR_INVALIDCALL;
            }
            const bool belongsToDevice = surfaceDevice == m_device;
            surfaceDevice->Release();
            if (!belongsToDevice)
                return D3DERR_INVALIDCALL;

            D3D9Surface *surface = static_cast<D3D9Surface *>(pSurface);

            const auto *tex = surface->GetCommonTexture();
            if (!tex || tex->Device() != m_device)
                return D3DERR_INVALIDCALL;

            const auto &desc = tex->Desc();
            const auto &image = desc->MultiSample != D3DMULTISAMPLE_NONE ? const_cast<D3D9CommonTexture*>(tex)->GetResolveImage() : tex->GetImage();
            if (image == nullptr || image->handle() == VK_NULL_HANDLE)
                return D3DERR_INVALIDCALL;
            const auto &device = tex->Device()->GetDXVKDevice();

            // I don't know why the image randomly is a uint64_t in OpenVR.
            pDesc->Image = uint64_t(image->handle());
            pDesc->Device = device->handle();
            pDesc->PhysicalDevice = device->adapter()->handle();
            pDesc->Instance = device->instance()->handle();
            pDesc->Queue = device->queues().graphics.queueHandle;
            pDesc->QueueFamilyIndex = device->queues().graphics.queueIndex;

            pDesc->Width = desc->Width;
            pDesc->Height = desc->Height;
            pDesc->Format = tex->GetFormatMapping().FormatColor;
            pDesc->SampleCount = uint32_t(image->info().sampleCount);

            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE TransferSurface(
            IDirect3DSurface9 *pSurface,
            BOOL waitResourceIdle)
        {
            if (unlikely(pSurface == nullptr))
                return D3DERR_INVALIDCALL;

            auto *tex = static_cast<D3D9Surface *>(pSurface)->GetCommonTexture();
            const auto &image = tex->GetImage();

            VkImageSubresourceRange subresources = {
              VK_IMAGE_ASPECT_COLOR_BIT,
              0, image->info().mipLevels,
              0, image->info().numLayers
            };

            m_device->TransformImage(
                tex, &subresources,
                image->info().layout,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            // This wait may need to be on all Faces and Mip Levels (2 loops).
            if (waitResourceIdle)
                m_device->WaitForResource(image, tex->GetMappingBufferSequenceNumber(0u), D3DLOCK_READONLY);

            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE LockDevice()
        {
            m_lock = m_device->LockDevice();
            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE UnlockDevice()
        {
            m_lock = D3D9DeviceLock();
            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE WaitDeviceIdle()
        {
            m_device->Flush();
            // Not clear if we need all here, perhaps...
            m_device->SynchronizeCsThread(DxvkCsThread::SynchronizeAll);
            m_device->GetDXVKDevice()->waitForIdle();
            return D3D_OK;
        }

        HRESULT STDMETHODCALLTYPE CaptureBackBufferData()
        {
            const auto lock = m_device->LockDevice();
            InvalidateBackBufferData();
            IDirect3DSurface9* surface = nullptr;
            HRESULT result = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
            if (FAILED(result) || !surface) {
                if (surface)
                    surface->Release();
                return FAILED(result) ? result : D3DERR_INVALIDCALL;
            }

            auto* texture = static_cast<D3D9Surface*>(surface)->GetCommonTexture();
            result = GetVRDesc(surface, &m_backBufferDesc);
            if (SUCCEEDED(result)) {
                m_backBufferImage = texture->Desc()->MultiSample != D3DMULTISAMPLE_NONE
                    ? texture->GetResolveImage() : texture->GetImage();
                m_hasBackBufferData = true;
                if (!m_loggedCapture) {
                    m_loggedCapture = true;
                    Logger::info(str::format("D3D9VR: back buffer vk format ", uint32_t(m_backBufferDesc.Format),
                        " ", m_backBufferDesc.Width, "x", m_backBufferDesc.Height,
                        " samples ", m_backBufferDesc.SampleCount));
                }
            } else if (!m_loggedCaptureFailure) {
                m_loggedCaptureFailure = true;
                Logger::warn(str::format("D3D9VR: back buffer capture failed (multisample ",
                    uint32_t(texture->Desc()->MultiSample), ")"));
            }
            surface->Release();
            return result;
        }

        void STDMETHODCALLTYPE InvalidateBackBufferData()
        {
            const auto lock = m_device->LockDevice();
            m_backBufferCaptureStarted = true;
            m_hasBackBufferData = false;
            m_backBufferImage = nullptr;
            m_backBufferDesc = {};
        }

        HRESULT STDMETHODCALLTYPE GetBackBufferData(SharedTextureHolder* backBufferData)
        {
            const auto lock = m_device->LockDevice();
            if (!backBufferData)
                return D3DERR_INVALIDCALL;
            D3D9_TEXTURE_VR_DESC textureDesc{};
            if (m_hasBackBufferData) {
                textureDesc = m_backBufferDesc;
            } else {
                if (m_backBufferCaptureStarted)
                    return D3DERR_INVALIDCALL;
                // VR initializes before the first Present. There is no
                // just-presented image yet, so expose the current buffer.
                IDirect3DSurface9* surface = nullptr;
                HRESULT result = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
                if (FAILED(result) || !surface) {
                    if (surface)
                        surface->Release();
                    return FAILED(result) ? result : D3DERR_INVALIDCALL;
                }
                result = GetVRDesc(surface, &textureDesc);
                surface->Release();
                if (FAILED(result))
                    return result;
            }

            memcpy(&backBufferData->m_VulkanData, &textureDesc, sizeof(vr::VRVulkanTextureData_t));
            backBufferData->m_VRTexture.handle = &backBufferData->m_VulkanData;
            backBufferData->m_VRTexture.eColorSpace = vr::ColorSpace_Auto;
            backBufferData->m_VRTexture.eType = vr::TextureType_Vulkan;
        
            return D3D_OK;
        }

    private:
        D3D9DeviceEx *m_device;
        Portal2VROpenVR::SessionLease m_openVRSession;
        D3D9DeviceLock m_lock;
        Rc<DxvkImage> m_backBufferImage;
        D3D9_TEXTURE_VR_DESC m_backBufferDesc{};
        bool m_hasBackBufferData = false;
        bool m_backBufferCaptureStarted = false;
        bool m_loggedCaptureFailure = false;
        bool m_loggedCapture = false;
    };

}

HRESULT __stdcall Direct3DCreateVRImpl(IDirect3DDevice9 *pDevice, IDirect3DVR9 **pInterface) {
    if (pInterface == nullptr)
        return D3DERR_INVALIDCALL;

    *pInterface = dxvk::ref(new dxvk::D3D9VR(pDevice));

    return D3D_OK;
}

HRESULT __stdcall AcquireSoleVRBridge(IDirect3DDevice9 **pDevice, IDirect3DVR9 **pInterface) {
    if (!pDevice || !pInterface) {
        if (pDevice)
            *pDevice = nullptr;
        if (pInterface)
            *pInterface = nullptr;
        return D3DERR_INVALIDCALL;
    }
    dxvk::D3D9DeviceEx* device = nullptr;
    if (!dxvk::GetD3D9VRBridgeRegistry().AcquireSole(&device, pInterface)) {
        *pDevice = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    *pDevice = device;
    return D3D_OK;
}
