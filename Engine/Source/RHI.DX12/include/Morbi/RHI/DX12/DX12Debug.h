#pragma once

#include <string>
#include <string_view>

#include "Morbi/Core/Export.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <d3d12.h>
    #include <dxgi1_6.h>
    #include <dxgidebug.h>
    #include <wrl/client.h>
#endif

namespace Morbi::RHI::DX12
{
    class MORBI_RHI_DX12_API DX12Debug
    {
    public:
#if defined(_WIN32)
        // Must be called BEFORE D3D12CreateDevice and CreateDXGIFactory2 when DebugConfig::IsDebugMode() is true.
        // Returns DXGI_CREATE_FACTORY_DEBUG if DXGI debug factory creation should be enabled.
        UINT EnablePreDeviceDebugAndDred(bool enableValidation, bool enableGpuGbv) noexcept;

        // Must be called AFTER D3D12CreateDevice succeeds when debug mode is active.
        void InitializePostDevice(ID3D12Device* device) noexcept;

        // Unregisters callbacks and optionally reports live DXGI/D3D12 objects.
        void Shutdown(ID3D12Device* device) noexcept;

        // Flushes queued InfoQueue messages if ID3D12InfoQueue1 callback wasn't registered.
        void FlushMessages() noexcept;

        // Checks an HRESULT; logs failure and dumps full DRED AutoBreadcrumbs + PageFault if device removed/hung.
        bool CheckHResult(HRESULT hr, ID3D12Device* device, const char* operationContext) noexcept;

        // Explicitly dumps GetDeviceRemovedReason + DRED AutoBreadcrumbs & PageFault allocations.
        static void DumpDeviceRemovedExtendedData(ID3D12Device* device, HRESULT triggerHr, const char* operationContext) noexcept;

        // Assigns a UTF-8 debug name to any ID3D12Object when debug mode is active.
        static void SetObjectName(ID3D12Object* obj, std::string_view name) noexcept;

        [[nodiscard]] static std::string FormatHResult(HRESULT hr);
#endif

    private:
#if defined(_WIN32)
        bool m_active = false;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_infoQueue;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_infoQueue1;
        DWORD m_callbackCookie = 0;
        bool m_callbackRegistered = false;
        Microsoft::WRL::ComPtr<IDXGIDebug1> m_dxgiDebug;
        Microsoft::WRL::ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;
#endif
    };
}
