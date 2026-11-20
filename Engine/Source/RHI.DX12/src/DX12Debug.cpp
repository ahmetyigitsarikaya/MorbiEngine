#include "Morbi/RHI/DX12/DX12Debug.h"

#include <cstdio>
#include <vector>

#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Types.hpp"

namespace Morbi::RHI::DX12
{
#if defined(_WIN32)
    namespace
    {
        const char* SeverityLabel(D3D12_MESSAGE_SEVERITY sev) noexcept
        {
            switch (sev)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
            case D3D12_MESSAGE_SEVERITY_ERROR:      return "ERROR";
            case D3D12_MESSAGE_SEVERITY_WARNING:    return "WARNING";
            case D3D12_MESSAGE_SEVERITY_INFO:       return "INFO";
            case D3D12_MESSAGE_SEVERITY_MESSAGE:    return "MESSAGE";
            default:                                return "UNKNOWN";
            }
        }

        void LogD3D12Message(
            D3D12_MESSAGE_CATEGORY category,
            D3D12_MESSAGE_SEVERITY severity,
            D3D12_MESSAGE_ID id,
            LPCSTR description) noexcept
        {
            const char* safeDesc = description ? description : "<null>";
            char buf[2048]{};
            std::snprintf(
                buf,
                sizeof(buf),
                "[%s] [Cat=%d ID=%d] %s",
                SeverityLabel(severity),
                static_cast<int>(category),
                static_cast<int>(id),
                safeDesc);

            switch (severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                Core::Log::Fatal("RHI.DX12.Debug", buf);
                break;
            case D3D12_MESSAGE_SEVERITY_ERROR:
                Core::Log::Error("RHI.DX12.Debug", buf);
                break;
            case D3D12_MESSAGE_SEVERITY_WARNING:
                Core::Log::Warn("RHI.DX12.Debug", buf);
                break;
            case D3D12_MESSAGE_SEVERITY_INFO:
            case D3D12_MESSAGE_SEVERITY_MESSAGE:
            default:
                Core::Log::Trace("RHI.DX12.Debug", buf);
                break;
            }
        }

        void CALLBACK D3D12MessageCallbackProc(
            D3D12_MESSAGE_CATEGORY category,
            D3D12_MESSAGE_SEVERITY severity,
            D3D12_MESSAGE_ID id,
            LPCSTR pDescription,
            void* pContext)
        {
            (void)pContext;
            LogD3D12Message(category, severity, id, pDescription);
        }

        const char* BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) noexcept
        {
            switch (op)
            {
            case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:                       return "SetMarker";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:                      return "BeginEvent";
            case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:                        return "EndEvent";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:                   return "DrawInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:            return "DrawIndexedInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:                 return "ExecuteIndirect";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:                        return "Dispatch";
            case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:                return "CopyBufferRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:               return "CopyTextureRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:                    return "CopyResource";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTILES:                       return "CopyTiles";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:              return "ResolveSubresource";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:           return "ClearRenderTargetView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:        return "ClearUnorderedAccessView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:           return "ClearDepthStencilView";
            case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:                 return "ResourceBarrier";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:                   return "ExecuteBundle";
            case D3D12_AUTO_BREADCRUMB_OP_PRESENT:                         return "Present";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:                return "ResolveQueryData";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:                 return "BeginSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:                   return "EndSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME:                     return "DecodeFrame";
            case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES:                   return "ProcessFrames";
            case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT:            return "AtomicCopyBufferUINT";
            case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64:          return "AtomicCopyBufferUINT64";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION:        return "ResolveSubresourceRegion";
            case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE:            return "WriteBufferImmediate";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1:                    return "DecodeFrame1";
            case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION:     return "SetProtectedResourceSession";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2:                    return "DecodeFrame2";
            case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1:                  return "ProcessFrames1";
            case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE: return "BuildRaytracingAS";
            case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO: return "EmitRaytracingASPostBuildInfo";
            case D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE: return "CopyRaytracingAS";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS:                    return "DispatchRays";
            case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEMETACOMMAND:           return "InitializeMetaCommand";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND:              return "ExecuteMetaCommand";
            case D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION:                  return "EstimateMotion";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP:         return "ResolveMotionVectorHeap";
            case D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1:               return "SetPipelineState1";
            case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEEXTENSIONCOMMAND:      return "InitializeExtensionCommand";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND:         return "ExecuteExtensionCommand";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH:                    return "DispatchMesh";
            default:                                                       return "UnknownOp";
            }
        }

        const char* DredAllocationTypeName(D3D12_DRED_ALLOCATION_TYPE type) noexcept
        {
            switch (type)
            {
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:     return "CommandQueue";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR: return "CommandAllocator";
            case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:    return "PipelineState";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:      return "CommandList";
            case D3D12_DRED_ALLOCATION_TYPE_FENCE:             return "Fence";
            case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:   return "DescriptorHeap";
            case D3D12_DRED_ALLOCATION_TYPE_HEAP:              return "Heap";
            case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:          return "Resource";
            case D3D12_DRED_ALLOCATION_TYPE_PASS:              return "Pass";
            case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSION:     return "CryptoSession";
            case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSIONPOLICY: return "CryptoSessionPolicy";
            case D3D12_DRED_ALLOCATION_TYPE_PROTECTEDRESOURCESESSION: return "ProtectedResourceSession";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER_HEAP: return "VideoDecoderHeap";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL:      return "CommandPool";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER:  return "CommandRecorder";
            case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT:      return "StateObject";
            case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:       return "MetaCommand";
            case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP:   return "SchedulingGroup";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_ESTIMATOR: return "VideoMotionEstimator";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_VECTOR_HEAP: return "VideoMotionVectorHeap";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND: return "VideoExtensionCommand";
            default:                                           return "InvalidOrUnknown";
            }
        }

        std::string WideToUtf8Safe(const wchar_t* wideStr)
        {
            if (!wideStr || wideStr[0] == L'\0')
            {
                return {};
            }
            const int needed = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1)
            {
                return {};
            }
            std::string out(static_cast<size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, out.data(), needed, nullptr, nullptr);
            return out;
        }

        template <typename NodeT>
        void LogAutoBreadcrumbNodes(const NodeT* headNode)
        {
            u32 nodeIndex = 0;
            for (const NodeT* node = headNode; node != nullptr; node = node->pNext, ++nodeIndex)
            {
                std::string cmdListName = node->pCommandListDebugNameA
                    ? std::string(node->pCommandListDebugNameA)
                    : WideToUtf8Safe(node->pCommandListDebugNameW);
                if (cmdListName.empty()) cmdListName = "<UnnamedCommandList>";

                std::string queueName = node->pCommandQueueDebugNameA
                    ? std::string(node->pCommandQueueDebugNameA)
                    : WideToUtf8Safe(node->pCommandQueueDebugNameW);
                if (queueName.empty()) queueName = "<UnnamedCommandQueue>";

                const UINT32 lastCompleted = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;

                Core::Log::Error("RHI.DX12.DRED",
                    "AutoBreadcrumb Node[" + std::to_string(nodeIndex) + "]: CommandList='" + cmdListName +
                    "', CommandQueue='" + queueName + "', TotalOps=" + std::to_string(node->BreadcrumbCount) +
                    ", LastCompletedOpIndex=" + std::to_string(lastCompleted));

                if (node->pCommandHistory != nullptr && node->BreadcrumbCount > 0)
                {
                    const UINT32 startIdx = (lastCompleted > 8u) ? (lastCompleted - 8u) : 0u;
                    const UINT32 endIdx = (lastCompleted + 8u < node->BreadcrumbCount)
                        ? (lastCompleted + 8u)
                        : node->BreadcrumbCount;

                    for (UINT32 opIdx = startIdx; opIdx < endIdx; ++opIdx)
                    {
                        const char* marker = (opIdx < lastCompleted) ? "COMPLETED" :
                                             (opIdx == lastCompleted) ? ">>> FAULT_NEXT <<<" : "PENDING";
                        Core::Log::Error("RHI.DX12.DRED",
                            "  Op[" + std::to_string(opIdx) + "] (" + marker + "): " +
                            BreadcrumbOpName(node->pCommandHistory[opIdx]));
                    }
                }
            }
        }
    }

    UINT DX12Debug::EnablePreDeviceDebugAndDred(bool enableValidation, bool enableGpuGbv) noexcept
    {
        const bool debugActive = enableValidation || Core::Diagnostics::IsDebugMode();
        const bool gbvActive = enableGpuGbv || Core::Diagnostics::IsGpuGbvMode();
        m_active = debugActive;

        if (!debugActive)
        {
            return 0u;
        }

        UINT dxgiFactoryFlags = 0u;

        // 1. Enable D3D12 Debug Layer (and optionally GPU-Based Validation) BEFORE D3D12CreateDevice
        Microsoft::WRL::ComPtr<ID3D12Debug> debug0;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug0))) && debug0)
        {
            debug0->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            Core::Log::Info("RHI.DX12.Debug", "D3D12 Debug Layer enabled (Release-compatible runtime activation).");

            if (gbvActive)
            {
                Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug0.As(&debug1)) && debug1)
                {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                    debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                    Core::Log::Info("RHI.DX12.Debug", "D3D12 GPU-Based Validation (GBV) & Synchronized Queue Validation enabled (--gpu-gbv).");
                }
            }
        }
        else
        {
            Core::Log::Warn("RHI.DX12.Debug", "D3D12GetDebugInterface failed (Graphics Tools Windows optional feature may not be installed).");
        }

        // 2. Enable DXGI Debug Interface tracking
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiDebug))) && m_dxgiDebug)
        {
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            Core::Log::Trace("RHI.DX12.Debug", "DXGIGetDebugInterface1 active for live object tracking.");
        }
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue))) && m_dxgiInfoQueue)
        {
            m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, FALSE);
        }

        // 3. Force-Enable DRED (AutoBreadcrumbs + PageFault + BreadcrumbContexts) BEFORE D3D12CreateDevice
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))) && dredSettings)
        {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings1;
            if (SUCCEEDED(dredSettings.As(&dredSettings1)) && dredSettings1)
            {
                dredSettings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            }

            Core::Log::Info("RHI.DX12.Debug", "DRED (AutoBreadcrumbs=FORCED_ON, PageFault=FORCED_ON) armed.");
        }
        else
        {
            Core::Log::Warn("RHI.DX12.Debug", "ID3D12DeviceRemovedExtendedDataSettings unavailable on this OS/SDK runtime.");
        }

        return dxgiFactoryFlags;
    }

    void DX12Debug::InitializePostDevice(ID3D12Device* device) noexcept
    {
        if (!m_active || device == nullptr)
        {
            return;
        }

        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&m_infoQueue))) && m_infoQueue)
        {
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
            m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

            if (SUCCEEDED(m_infoQueue.As(&m_infoQueue1)) && m_infoQueue1)
            {
                const HRESULT cbHr = m_infoQueue1->RegisterMessageCallback(
                    D3D12MessageCallbackProc,
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                    this,
                    &m_callbackCookie);
                if (SUCCEEDED(cbHr))
                {
                    m_callbackRegistered = true;
                    Core::Log::Info("RHI.DX12.Debug", "Registered ID3D12InfoQueue1 real-time message callback.");
                }
            }

            if (!m_callbackRegistered)
            {
                Core::Log::Info("RHI.DX12.Debug", "ID3D12InfoQueue active (polling mode; ID3D12InfoQueue1 callback unavailable).");
            }
        }
    }

    void DX12Debug::Shutdown(ID3D12Device* device) noexcept
    {
        (void)device;
        FlushMessages();

        if (m_callbackRegistered && m_infoQueue1)
        {
            m_infoQueue1->UnregisterMessageCallback(m_callbackCookie);
            m_callbackRegistered = false;
            m_callbackCookie = 0;
        }

        m_infoQueue1.Reset();
        m_infoQueue.Reset();

        if (m_active && m_dxgiDebug)
        {
            Core::Log::Trace("RHI.DX12.Debug", "Reporting live DXGI/D3D12 objects on shutdown...");
            m_dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        }

        m_dxgiInfoQueue.Reset();
        m_dxgiDebug.Reset();
        m_active = false;
    }

    void DX12Debug::FlushMessages() noexcept
    {
        if (!m_active || !m_infoQueue || m_callbackRegistered)
        {
            return;
        }

        const UINT64 count = m_infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T msgLen = 0;
            if (FAILED(m_infoQueue->GetMessage(i, nullptr, &msgLen)) || msgLen == 0)
            {
                continue;
            }

            std::vector<u8> storage(msgLen);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(m_infoQueue->GetMessage(i, msg, &msgLen)))
            {
                LogD3D12Message(msg->Category, msg->Severity, msg->ID, msg->pDescription);
            }
        }
        m_infoQueue->ClearStoredMessages();
    }

    bool DX12Debug::CheckHResult(HRESULT hr, ID3D12Device* device, const char* operationContext) noexcept
    {
        FlushMessages();

        if (SUCCEEDED(hr))
        {
            return true;
        }

        const char* ctx = operationContext ? operationContext : "D3D12 Operation";
        Core::Log::Error("RHI.DX12.Debug", std::string(ctx) + " failed with " + FormatHResult(hr));

        if (hr == DXGI_ERROR_DEVICE_REMOVED ||
            hr == DXGI_ERROR_DEVICE_HUNG ||
            hr == DXGI_ERROR_DEVICE_RESET ||
            hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        {
            DumpDeviceRemovedExtendedData(device, hr, ctx);
        }

        return false;
    }

    void DX12Debug::DumpDeviceRemovedExtendedData(ID3D12Device* device, HRESULT triggerHr, const char* operationContext) noexcept
    {
        Core::Log::Fatal("RHI.DX12.DRED",
            std::string("GPU DEVICE LOST / REMOVED at '") + (operationContext ? operationContext : "Unknown") +
            "' | Trigger HRESULT=" + FormatHResult(triggerHr));

        if (device == nullptr)
        {
            Core::Log::Error("RHI.DX12.DRED", "ID3D12Device pointer is null; cannot query DRED.");
            return;
        }

        const HRESULT reason = device->GetDeviceRemovedReason();
        Core::Log::Fatal("RHI.DX12.DRED", "GetDeviceRemovedReason() = " + FormatHResult(reason));

        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred1))) && dred1)
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
            if (SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
            {
                if (breadcrumbs.pHeadAutoBreadcrumbNode == nullptr)
                {
                    Core::Log::Warn("RHI.DX12.DRED", "DRED AutoBreadcrumbs1 output has no breadcrumb nodes.");
                }
                else
                {
                    LogAutoBreadcrumbNodes(breadcrumbs.pHeadAutoBreadcrumbNode);
                }
            }

            D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
            if (SUCCEEDED(dred1->GetPageFaultAllocationOutput1(&pageFault)))
            {
                char vaBuf[64]{};
                std::snprintf(vaBuf, sizeof(vaBuf), "0x%016llX", static_cast<unsigned long long>(pageFault.PageFaultVA));
                Core::Log::Fatal("RHI.DX12.DRED", std::string("PageFault GPU Virtual Address (VA): ") + vaBuf);

                u32 idx = 0;
                for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadExistingAllocationNode;
                     node != nullptr;
                     node = node->pNext, ++idx)
                {
                    std::string objName = node->ObjectNameA
                        ? std::string(node->ObjectNameA)
                        : WideToUtf8Safe(node->ObjectNameW);
                    if (objName.empty()) objName = "<Unnamed>";
                    Core::Log::Error("RHI.DX12.DRED",
                        "  ExistingAllocation[" + std::to_string(idx) + "]: Type=" +
                        DredAllocationTypeName(node->AllocationType) + ", Name='" + objName + "'");
                }

                idx = 0;
                for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadRecentFreedAllocationNode;
                     node != nullptr;
                     node = node->pNext, ++idx)
                {
                    std::string objName = node->ObjectNameA
                        ? std::string(node->ObjectNameA)
                        : WideToUtf8Safe(node->ObjectNameW);
                    if (objName.empty()) objName = "<Unnamed>";
                    Core::Log::Error("RHI.DX12.DRED",
                        "  RecentlyFreedAllocation[" + std::to_string(idx) + "]: Type=" +
                        DredAllocationTypeName(node->AllocationType) + ", Name='" + objName + "'");
                }
            }
        }
        else
        {
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred0;
            if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred0))) && dred0)
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
                if (SUCCEEDED(dred0->GetAutoBreadcrumbsOutput(&breadcrumbs)) && breadcrumbs.pHeadAutoBreadcrumbNode != nullptr)
                {
                    LogAutoBreadcrumbNodes(breadcrumbs.pHeadAutoBreadcrumbNode);
                }

                D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
                if (SUCCEEDED(dred0->GetPageFaultAllocationOutput(&pageFault)))
                {
                    char vaBuf[64]{};
                    std::snprintf(vaBuf, sizeof(vaBuf), "0x%016llX", static_cast<unsigned long long>(pageFault.PageFaultVA));
                    Core::Log::Fatal("RHI.DX12.DRED", std::string("PageFault GPU Virtual Address (VA): ") + vaBuf);

                    u32 idx = 0;
                    for (const D3D12_DRED_ALLOCATION_NODE* node = pageFault.pHeadExistingAllocationNode;
                         node != nullptr;
                         node = node->pNext, ++idx)
                    {
                        std::string objName = node->ObjectNameA
                            ? std::string(node->ObjectNameA)
                            : WideToUtf8Safe(node->ObjectNameW);
                        if (objName.empty()) objName = "<Unnamed>";
                        Core::Log::Error("RHI.DX12.DRED",
                            "  ExistingAllocation[" + std::to_string(idx) + "]: Type=" +
                            DredAllocationTypeName(node->AllocationType) + ", Name='" + objName + "'");
                    }

                    idx = 0;
                    for (const D3D12_DRED_ALLOCATION_NODE* node = pageFault.pHeadRecentFreedAllocationNode;
                         node != nullptr;
                         node = node->pNext, ++idx)
                    {
                        std::string objName = node->ObjectNameA
                            ? std::string(node->ObjectNameA)
                            : WideToUtf8Safe(node->ObjectNameW);
                        if (objName.empty()) objName = "<Unnamed>";
                        Core::Log::Error("RHI.DX12.DRED",
                            "  RecentlyFreedAllocation[" + std::to_string(idx) + "]: Type=" +
                            DredAllocationTypeName(node->AllocationType) + ", Name='" + objName + "'");
                    }
                }
            }
            else
            {
                Core::Log::Warn("RHI.DX12.DRED", "ID3D12DeviceRemovedExtendedData interface not supported on this device.");
            }
        }

        Core::Log::Flush();
    }

    void DX12Debug::SetObjectName(ID3D12Object* obj, std::string_view name) noexcept
    {
        if (!Core::Diagnostics::IsDebugMode() || obj == nullptr || name.empty())
        {
            return;
        }
        const int needed = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
        if (needed <= 0)
        {
            return;
        }
        std::wstring wide(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), needed);
        obj->SetName(wide.c_str());
    }

    std::string DX12Debug::FormatHResult(HRESULT hr)
    {
        char hexBuf[32]{};
        std::snprintf(hexBuf, sizeof(hexBuf), "0x%08lX", static_cast<unsigned long>(hr));

        const char* knownName = nullptr;
        switch (hr)
        {
        case DXGI_ERROR_DEVICE_REMOVED:      knownName = "DXGI_ERROR_DEVICE_REMOVED"; break;
        case DXGI_ERROR_DEVICE_HUNG:         knownName = "DXGI_ERROR_DEVICE_HUNG"; break;
        case DXGI_ERROR_DEVICE_RESET:        knownName = "DXGI_ERROR_DEVICE_RESET"; break;
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: knownName = "DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
        case DXGI_ERROR_INVALID_CALL:        knownName = "DXGI_ERROR_INVALID_CALL"; break;
        case E_OUTOFMEMORY:                  knownName = "E_OUTOFMEMORY"; break;
        case E_INVALIDARG:                   knownName = "E_INVALIDARG"; break;
        default:                             break;
        }

        if (knownName != nullptr)
        {
            return std::string(hexBuf) + " (" + knownName + ")";
        }

        LPSTR msgBuf = nullptr;
        const DWORD len = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&msgBuf),
            0,
            nullptr);

        std::string result(hexBuf);
        if (len > 0 && msgBuf != nullptr)
        {
            std::string msg(msgBuf, len);
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
            {
                msg.pop_back();
            }
            result += " (" + msg + ")";
            LocalFree(msgBuf);
        }
        return result;
    }
#endif
}
