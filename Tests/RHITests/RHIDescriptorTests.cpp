#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Platform/Window.hpp"
#include "Morbi/RHI/IDevice.hpp"

using namespace Morbi;
using namespace Morbi::RHI;

namespace
{
    int g_failures = 0;
    int g_tests_run = 0;

    void Check(bool cond, const std::string& expr, const std::string& msg = "")
    {
        if (!cond)
        {
            std::cerr << "[FAIL] " << expr << (msg.empty() ? "" : " | " + msg) << "\n";
            ++g_failures;
        }
        else
        {
            std::cout << "[PASS] " << expr << "\n";
        }
        ++g_tests_run;
    }

    #define EXPECT_TRUE(cond, msg) Check((cond), #cond, (msg))
    #define EXPECT_FALSE(cond, msg) Check(!(cond), #cond, (msg))
    #define EXPECT_EQ(a, b, msg) Check((a) == (b), std::string(#a) + " == " + #b, (msg))

    struct TestDeviceContext
    {
        std::unique_ptr<IDevice> Device;
        Platform::NativeWindowHandle WindowHandle{};
    };

    TestDeviceContext CreateTestDevice(BackendAPI api)
    {
        TestDeviceContext ctx{};
        ctx.WindowHandle.Backend = Platform::NativeWindowBackend::Win32;
        ctx.WindowHandle.Window = CreateWindowExW(
            0, L"STATIC", L"RHI Test Window",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (!ctx.WindowHandle.Window)
        {
            std::cerr << "Failed to create test window\n";
            return ctx;
        }

        ctx.Device = CreateDevice(api);

        DeviceDesc desc{
            .PreferredAPI = api,
            .EnableValidation = true,
            .ApplicationName = "MorbiRHI_DescriptorTests"
        };

        auto result = ctx.Device->Initialize(desc, ctx.WindowHandle, 800, 600);
        if (!result)
        {
            std::cerr << "Failed to initialize " << (api == BackendAPI::Vulkan ? "Vulkan" : "DX12") << " device: " << result.Error() << "\n";
            ctx.Device.reset();
            DestroyWindow(static_cast<HWND>(ctx.WindowHandle.Window));
            ctx.WindowHandle.Window = nullptr;
        }

        return ctx;
    }

    void DestroyTestDevice(TestDeviceContext& ctx)
    {
        if (ctx.Device)
        {
            ctx.Device->Shutdown();
            ctx.Device.reset();
        }
        if (ctx.WindowHandle.Window)
        {
            DestroyWindow(static_cast<HWND>(ctx.WindowHandle.Window));
            ctx.WindowHandle.Window = nullptr;
        }
    }

    // Test 1: Allocate, release, and verify exact slot reuse for storage buffers
    void Test_StorageBufferAllocation(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        const u32 kMaxBuffers = 4096;
        std::vector<BufferHandle> handles;
        std::unordered_set<u32> freedIndices;
        handles.reserve(150);

        BufferDesc desc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestStorageBuffer"
        };

        for (u32 i = 0; i < 100; ++i)
        {
            BufferHandle h = device->CreateBuffer(desc, nullptr);
            EXPECT_TRUE(h.IsValid(), "Storage buffer creation should succeed");
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            EXPECT_TRUE(idx.IsValid() && idx.Index < kMaxBuffers, "Storage buffer bindless index within bounds");
            handles.push_back(h);
        }

        // Release first 50 and record their exact slot indices
        for (u32 i = 0; i < 50; ++i)
        {
            BindlessResourceIndex idx = device->GetBindlessIndex(handles[i]);
            freedIndices.insert(idx.Index);
            device->DestroyBuffer(handles[i]);
        }
        device->WaitIdle();

        // Allocate 50 more - every single new allocation must reuse one of the 50 freed indices
        u32 reusedCount = 0;
        for (u32 i = 0; i < 50; ++i)
        {
            BufferHandle h = device->CreateBuffer(desc, nullptr);
            EXPECT_TRUE(h.IsValid(), "Storage buffer creation after reclaim should succeed");
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            if (idx.IsValid() && freedIndices.contains(idx.Index))
            {
                ++reusedCount;
            }
            handles.push_back(h);
        }
        EXPECT_EQ(reusedCount, 50u, "All 50 newly allocated storage buffers must reuse the 50 reclaimed slots");

        for (u32 i = 50; i < handles.size(); ++i)
        {
            device->DestroyBuffer(handles[i]);
        }
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 2: Uniform buffer allocation, reclamation, exact slot reuse, and 256-slot exhaustion & recovery
    void Test_UniformBufferAllocationAndExhaustion(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        const u32 kMaxUniformBuffers = 256;
        std::vector<BufferHandle> handles;
        handles.reserve(kMaxUniformBuffers);

        BufferDesc desc{
            .SizeBytes = 256,
            .Usage = BufferUsage::Uniform,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestUniformBuffer"
        };

        // Allocate all 256 bindless uniform buffer slots
        std::unordered_set<u32> uniqueSlots;
        for (u32 i = 0; i < kMaxUniformBuffers; ++i)
        {
            BufferHandle h = device->CreateBuffer(desc, nullptr);
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            if (h.IsValid() && idx.IsValid() && idx.Index < kMaxUniformBuffers)
            {
                uniqueSlots.insert(idx.Index);
            }
            handles.push_back(h);
        }
        EXPECT_EQ(uniqueSlots.size(), static_cast<size_t>(kMaxUniformBuffers), "All 256 uniform buffer bindless slots should be uniquely allocated");

        // 257th uniform buffer should still create a valid buffer, but its bindless slot must be invalid (exhausted)
        BufferHandle overflowBuf = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(overflowBuf.IsValid(), "Overflow uniform buffer resource creation succeeds");
        BindlessResourceIndex overflowIdx = device->GetBindlessIndex(overflowBuf);
        EXPECT_FALSE(overflowIdx.IsValid(), "257th uniform buffer bindless index must be invalid when 256-slot capacity is exhausted");
        device->DestroyBuffer(overflowBuf);

        // Release 25 uniform buffers and record their freed slots
        std::unordered_set<u32> freedSlots;
        for (u32 i = 0; i < 25; ++i)
        {
            freedSlots.insert(device->GetBindlessIndex(handles[i]).Index);
            device->DestroyBuffer(handles[i]);
        }
        device->WaitIdle();

        // Re-allocate 25 uniform buffers - must succeed and reuse the exact 25 freed slots
        std::unordered_set<u32> reallocatedSlots;
        for (u32 i = 0; i < 25; ++i)
        {
            BufferHandle h = device->CreateBuffer(desc, nullptr);
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            if (idx.IsValid())
            {
                reallocatedSlots.insert(idx.Index);
            }
            handles[i] = h;
        }
        EXPECT_EQ(reallocatedSlots.size(), 25u, "25 uniform buffers re-allocated after exhaustion recovery");
        EXPECT_TRUE(reallocatedSlots == freedSlots, "Re-allocated uniform buffers must reuse the exact 25 freed slots");

        for (BufferHandle h : handles)
        {
            device->DestroyBuffer(h);
        }
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 3: Allocate, release, and verify exact slot reuse for sampled textures
    void Test_TextureAllocation(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        const u32 kMaxTextures = 4096;
        std::vector<TextureHandle> handles;
        std::unordered_set<u32> freedIndices;
        handles.reserve(75);

        TextureDesc desc{
            .Width = 64,
            .Height = 64,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "TestTexture"
        };

        std::vector<u8> pixelData(64 * 64 * 4, 255);

        for (u32 i = 0; i < 50; ++i)
        {
            TextureHandle h = device->CreateTexture(desc, pixelData.data());
            EXPECT_TRUE(h.IsValid(), "Texture creation should succeed");
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            EXPECT_TRUE(idx.IsValid() && idx.Index < kMaxTextures, "Texture bindless index within bounds");
            handles.push_back(h);
        }

        // Release first 25 textures
        for (u32 i = 0; i < 25; ++i)
        {
            freedIndices.insert(device->GetBindlessIndex(handles[i]).Index);
            device->DestroyTexture(handles[i]);
        }
        device->WaitIdle();

        // Allocate 25 more - must reuse the exact 25 freed texture slots
        u32 reusedCount = 0;
        for (u32 i = 0; i < 25; ++i)
        {
            TextureHandle h = device->CreateTexture(desc, pixelData.data());
            EXPECT_TRUE(h.IsValid(), "Texture creation after reclaim should succeed");
            BindlessResourceIndex idx = device->GetBindlessIndex(h);
            if (idx.IsValid() && freedIndices.contains(idx.Index))
            {
                ++reusedCount;
            }
            handles.push_back(h);
        }
        EXPECT_EQ(reusedCount, 25u, "All 25 newly allocated textures must reuse the 25 reclaimed texture slots");

        for (u32 i = 25; i < handles.size(); ++i)
        {
            device->DestroyTexture(handles[i]);
        }
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 4: Verify freed slots are deterministically reused across cycles
    void Test_FreedSlotReuse(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestSlotReuse"
        };

        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "TestSlotReuse"
        };
        std::vector<u8> pixelData(32 * 32 * 4, 128);

        u32 expectedBufSlot = 0xFFFFFFFFu;
        u32 expectedTexSlot = 0xFFFFFFFFu;

        for (int cycle = 0; cycle < 4; ++cycle)
        {
            BufferHandle buf = device->CreateBuffer(bufferDesc, nullptr);
            TextureHandle tex = device->CreateTexture(texDesc, pixelData.data());

            EXPECT_TRUE(buf.IsValid(), "Buffer creation in cycle should succeed");
            EXPECT_TRUE(tex.IsValid(), "Texture creation in cycle should succeed");

            BindlessResourceIndex bufIdx = device->GetBindlessIndex(buf);
            BindlessResourceIndex texIdx = device->GetBindlessIndex(tex);

            EXPECT_TRUE(bufIdx.IsValid(), "Buffer bindless index should be valid");
            EXPECT_TRUE(texIdx.IsValid(), "Texture bindless index should be valid");

            if (cycle == 0)
            {
                expectedBufSlot = bufIdx.Index;
                expectedTexSlot = texIdx.Index;
            }
            else
            {
                EXPECT_EQ(bufIdx.Index, expectedBufSlot, "Buffer must reuse exact freed slot index from cycle 0");
                EXPECT_EQ(texIdx.Index, expectedTexSlot, "Texture must reuse exact freed slot index from cycle 0");
            }

            device->DestroyBuffer(buf);
            device->DestroyTexture(tex);
            device->WaitIdle();
        }

        DestroyTestDevice(ctx);
    }

    // Test 5: Verify double-free is rejected and does NOT push duplicate slot indices onto the free list
    void Test_DoubleFreePrevention(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc desc{
            .SizeBytes = 256,
            .Usage = BufferUsage::Uniform,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestDoubleFree"
        };

        BufferHandle h = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h.IsValid(), "Buffer creation should succeed");
        const u32 originalSlot = device->GetBindlessIndex(h).Index;

        // First destroy followed immediately by duplicate destroy before WaitIdle
        device->DestroyBuffer(h);
        device->DestroyBuffer(h);
        device->WaitIdle();
        // Third destroy after WaitIdle (stale generation handle)
        device->DestroyBuffer(h);

        BindlessResourceIndex idx = device->GetBindlessIndex(h);
        EXPECT_FALSE(idx.IsValid(), "Bindless index should be invalid after destroy");

        // Allocate two new uniform buffers: if double-destroy corrupted the free list,
        // both new buffers would receive `originalSlot`!
        BufferHandle a = device->CreateBuffer(desc, nullptr);
        BufferHandle b = device->CreateBuffer(desc, nullptr);
        BindlessResourceIndex idxA = device->GetBindlessIndex(a);
        BindlessResourceIndex idxB = device->GetBindlessIndex(b);

        EXPECT_EQ(idxA.Index, originalSlot, "First new buffer reuses the single freed slot");
        EXPECT_TRUE(idxA.Index != idxB.Index, "Second new buffer must NOT receive the same slot (no duplicate free-list entry)");

        device->DestroyBuffer(a);
        device->DestroyBuffer(b);
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 6: Verify a slot is not reused while referenced by an unsubmitted command list or open frame
    void Test_NoPrematureReuseBeforeGPUCompletion(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc desc{
            .SizeBytes = 256,
            .Usage = BufferUsage::Storage,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestPrematureReuse"
        };

        BufferHandle h1 = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h1.IsValid(), "First buffer creation should succeed");
        BindlessResourceIndex idx1 = device->GetBindlessIndex(h1);
        EXPECT_TRUE(idx1.IsValid(), "First buffer bindless index should be valid");
        const u32 firstIndex = idx1.Index;

        // Reference h1 in an active command list and end recording (unsubmitted)
        ICommandList& cmd = device->GetActiveCommandList();
        cmd.Begin();
        cmd.TrackBuffer(h1);
        cmd.End();

        // Request destruction of h1 while cmd is still unsubmitted
        device->DestroyBuffer(h1);

        // Run completed destruction processing: h1 MUST NOT be destroyed yet!
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(h1), "Buffer referenced by unsubmitted command list must stay alive across ProcessCompletedDestructions");

        // Allocate h2: must get a DIFFERENT slot because h1's slot cannot be reclaimed yet
        BufferHandle h2 = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h2.IsValid(), "Second buffer creation should succeed");
        BindlessResourceIndex idx2 = device->GetBindlessIndex(h2);
        EXPECT_TRUE(idx2.IsValid(), "Second buffer bindless index should be valid");
        EXPECT_TRUE(idx2.Index != firstIndex, "Slot must not be reused while command list is unsubmitted");

        // Now submit the command list and wait for GPU completion
        const u64 submitVal = device->SubmitCommandList(cmd);
        EXPECT_TRUE(submitVal > 0, "SubmitCommandList must return a valid timeline/fence value");
        device->WaitIdle();
        EXPECT_FALSE(device->IsResourceAlive(h1), "Buffer must be destroyed after submission completes and WaitIdle drains queue");

        // Allocate h3: must now reuse firstIndex!
        BufferHandle h3 = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h3.IsValid(), "Third buffer creation should succeed");
        BindlessResourceIndex idx3 = device->GetBindlessIndex(h3);
        EXPECT_EQ(idx3.Index, firstIndex, "Third buffer must reuse first buffer's reclaimed slot after GPU completion");

        device->DestroyBuffer(h2);
        device->DestroyBuffer(h3);
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 7: Verify a slot becomes reusable after GPU completion
    void Test_SlotReusableAfterGPUCompletion(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc desc{
            .SizeBytes = 256,
            .Usage = BufferUsage::Uniform,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "TestReusableAfterGPU"
        };

        BufferHandle h1 = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h1.IsValid(), "Buffer creation should succeed");
        BindlessResourceIndex idx1 = device->GetBindlessIndex(h1);
        const u32 firstIndex = idx1.Index;

        device->DestroyBuffer(h1);
        device->WaitIdle();

        BufferHandle h2 = device->CreateBuffer(desc, nullptr);
        EXPECT_TRUE(h2.IsValid(), "Buffer creation after GPU completion should succeed");
        BindlessResourceIndex idx2 = device->GetBindlessIndex(h2);
        EXPECT_TRUE(idx2.IsValid(), "Second buffer bindless index should be valid");
        EXPECT_EQ(idx2.Index, firstIndex, "Slot should be reused after GPU completion");

        device->DestroyBuffer(h2);
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Test 8: Stress repeated allocation/release cycles and verify memory allocator slab reclamation
    void Test_StressAllocationReleaseAndMemoryReclamation(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        const MemoryAllocatorStats baselineStats = device->GetMemoryStats();

        BufferDesc bufferDesc{
            .SizeBytes = 4096,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "StressBuffer"
        };

        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "StressTexture"
        };
        std::vector<u8> pixelData(32 * 32 * 4, 200);

        constexpr int kCycles = 60;
        constexpr int kBuffersPerCycle = 10;
        constexpr int kTexturesPerCycle = 5;

        bool allCyclesSucceeded = true;
        for (int cycle = 0; cycle < kCycles; ++cycle)
        {
            std::vector<BufferHandle> buffers;
            std::vector<TextureHandle> textures;
            buffers.reserve(kBuffersPerCycle);
            textures.reserve(kTexturesPerCycle);

            for (int i = 0; i < kBuffersPerCycle; ++i)
            {
                BufferHandle h = device->CreateBuffer(bufferDesc, nullptr);
                if (!h.IsValid() || !device->GetBindlessIndex(h).IsValid())
                {
                    allCyclesSucceeded = false;
                }
                buffers.push_back(h);
            }

            for (int i = 0; i < kTexturesPerCycle; ++i)
            {
                TextureHandle h = device->CreateTexture(texDesc, pixelData.data());
                if (!h.IsValid() || !device->GetBindlessIndex(h).IsValid())
                {
                    allCyclesSucceeded = false;
                }
                textures.push_back(h);
            }

            for (const auto& h : buffers)
                device->DestroyBuffer(h);
            for (const auto& h : textures)
                device->DestroyTexture(h);

            if (cycle % 10 == 9)
            {
                device->WaitIdle();
            }
        }

        device->WaitIdle();
        EXPECT_TRUE(allCyclesSucceeded, "All 60 stress cycles of buffers and textures succeeded with valid bindless indices");

        const MemoryAllocatorStats finalStats = device->GetMemoryStats();
        EXPECT_EQ(finalStats.ActiveAllocations, baselineStats.ActiveAllocations, "ActiveAllocations must return to baseline after WaitIdle");
        EXPECT_EQ(finalStats.TotalAllocatedBytes, baselineStats.TotalAllocatedBytes, "TotalAllocatedBytes must return to baseline after WaitIdle");

        // Also test swapchain RTV reclamation across 150 resizes (300 backbuffer RTVs > 256 RTV heap capacity)
        for (u32 i = 0; i < 150; ++i)
        {
            const u32 w = 640 + (i % 16) * 4;
            const u32 h = 480 + (i % 16) * 4;
            device->ResizeSwapchain(w, h);
        }
        EXPECT_TRUE(device->GetCurrentBackbuffer().IsValid(), "Backbuffer remains valid after 150 swapchain resizes (RTV slots reclaimed)");

        DestroyTestDevice(ctx);
    }

    // Test 9: Verify shutdown with pending deferred destructions and unsubmitted command lists
    void Test_ShutdownWithPendingDestructions(BackendAPI api)
    {
        auto ctx = CreateTestDevice(api);
        EXPECT_TRUE(ctx.Device != nullptr, "Device initialization should succeed");
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .Usage = BufferUsage::Uniform,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "ShutdownTestBuffer"
        };

        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "ShutdownTestTexture"
        };
        std::vector<u8> pixelData(32 * 32 * 4, 100);

        BufferHandle buf = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle tex = device->CreateTexture(texDesc, pixelData.data());

        EXPECT_TRUE(buf.IsValid(), "Buffer creation should succeed");
        EXPECT_TRUE(tex.IsValid(), "Texture creation should succeed");

        // Hold unsubmitted reference on an independent command list and request destruction before Shutdown
        auto extraCmd = device->CreateCommandList();
        EXPECT_TRUE(extraCmd != nullptr, "CreateCommandList should succeed");
        if (extraCmd)
        {
            extraCmd->Begin();
            extraCmd->TrackBuffer(buf);
            extraCmd->TrackTexture(tex);
            extraCmd->End();
        }

        device->DestroyBuffer(buf);
        device->DestroyTexture(tex);

        // Shutdown without explicit WaitIdle - Shutdown must safely release unsubmitted refs, drain destructions, and clean up
        DestroyTestDevice(ctx);
    }

    void RunAllTests(BackendAPI api)
    {
        const char* apiName = (api == BackendAPI::Vulkan) ? "Vulkan" : "DirectX 12";
        std::cout << "\n========== Running RHI Descriptor Tests for " << apiName << " ==========\n\n";

        Test_StorageBufferAllocation(api);
        Test_UniformBufferAllocationAndExhaustion(api);
        Test_TextureAllocation(api);
        Test_FreedSlotReuse(api);
        Test_DoubleFreePrevention(api);
        Test_NoPrematureReuseBeforeGPUCompletion(api);
        Test_SlotReusableAfterGPUCompletion(api);
        Test_StressAllocationReleaseAndMemoryReclamation(api);
        Test_ShutdownWithPendingDestructions(api);

        std::cout << "\n========== " << apiName << " Results: " << (g_tests_run - g_failures) << "/" << g_tests_run << " passed ==========\n";
    }
}

int main()
{
    int totalFailures = 0;
    int totalTests = 0;

    g_failures = 0;
    g_tests_run = 0;
    RunAllTests(BackendAPI::Vulkan);
    totalFailures += g_failures;
    totalTests += g_tests_run;

    g_failures = 0;
    g_tests_run = 0;
    RunAllTests(BackendAPI::DirectX12);
    totalFailures += g_failures;
    totalTests += g_tests_run;

    std::cout << "\n========================================\n";
    std::cout << "TOTAL: " << (totalTests - totalFailures) << "/" << totalTests << " assertions passed\n";
    std::cout << "========================================\n";

    return totalFailures == 0 ? 0 : 1;
}
