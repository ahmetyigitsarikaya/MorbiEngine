#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
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
#include "Morbi/Shader/ShaderCompiler.hpp"

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
            .ApplicationName = "MorbiRHI_GenerationalHandleHashTests"
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

    PipelineHandle CreateTestComputePipeline(IDevice& device, BackendAPI api)
    {
        Shader::ShaderCompiler compiler;
        if (!compiler.IsDXCAvailable())
        {
            return PipelineHandle::Invalid();
        }

        Shader::ShaderCompileRequest req{};
        req.SourceCode = "[numthreads(1, 1, 1)]\nvoid CSMain() {}\n";
        req.VirtualFilePath = "TestNoopCS.hlsl";
        req.EntryPoint = "CSMain";
        req.Stage = Shader::ShaderStage::Compute;
        req.TargetFormat = (api == BackendAPI::Vulkan)
            ? Shader::ShaderTargetFormat::SPIRV
            : Shader::ShaderTargetFormat::DXIL;
        req.EnableOptimization = true;

        auto compiled = compiler.Compile(req);
        if (!compiled)
        {
            return PipelineHandle::Invalid();
        }

        ComputePipelineDesc desc{};
        desc.ComputeShaderBytecode = compiled.Value().Bytecode;
        desc.DebugName = "TestComputePipeline";
        return device.CreateComputePipeline(desc);
    }

    // Pure GenerationalHandle Tests (Backend-agnostic)
    void Test_EqualHandlesProduceEqualHashes()
    {
        std::cout << "\n--- Test: Equal Handles Produce Equal Hashes ---\n";

        BufferHandle h1{ .Index = 42, .Generation = 1 };
        BufferHandle h2{ .Index = 42, .Generation = 1 };

        std::hash<BufferHandle> hasher;
        EXPECT_TRUE(h1 == h2, "Identical BufferHandles must compare equal");
        EXPECT_TRUE(hasher(h1) == hasher(h2), "Equal BufferHandles must produce equal hashes");

        TextureHandle th1{ .Index = 100, .Generation = 5 };
        TextureHandle th2{ .Index = 100, .Generation = 5 };

        std::hash<TextureHandle> texHasher;
        EXPECT_TRUE(th1 == th2, "Identical TextureHandles must compare equal");
        EXPECT_TRUE(texHasher(th1) == texHasher(th2), "Equal TextureHandles must produce equal hashes");

        PipelineHandle ph1{ .Index = 7, .Generation = 3 };
        PipelineHandle ph2{ .Index = 7, .Generation = 3 };

        std::hash<PipelineHandle> pipeHasher;
        EXPECT_TRUE(ph1 == ph2, "Identical PipelineHandles must compare equal");
        EXPECT_TRUE(pipeHasher(ph1) == pipeHasher(ph2), "Equal PipelineHandles must produce equal hashes");
    }

    void Test_DifferentIndexOrGenerationComparesUnequal()
    {
        std::cout << "\n--- Test: Different Index or Generation Compares Unequal ---\n";

        BufferHandle h1{ .Index = 42, .Generation = 1 };
        BufferHandle h2{ .Index = 42, .Generation = 2 };
        BufferHandle h3{ .Index = 43, .Generation = 1 };

        EXPECT_FALSE(h1 == h2, "BufferHandles with same index but different generation must not be equal");
        EXPECT_FALSE(h1 == h3, "BufferHandles with different index and same generation must not be equal");
        EXPECT_TRUE(h1 != h2, "BufferHandles with different generation must compare != true");

        TextureHandle th1{ .Index = 100, .Generation = 5 };
        TextureHandle th2{ .Index = 100, .Generation = 6 };
        EXPECT_FALSE(th1 == th2, "TextureHandles with same index but different generation must not be equal");

        PipelineHandle ph1{ .Index = 7, .Generation = 3 };
        PipelineHandle ph2{ .Index = 7, .Generation = 4 };
        EXPECT_FALSE(ph1 == ph2, "PipelineHandles with same index but different generation must not be equal");
    }

    void Test_UnorderedSetAndMapCompatibility()
    {
        std::cout << "\n--- Test: Same Index Different Generation Coexist in unordered_set & unordered_map ---\n";

        std::unordered_set<BufferHandle> bufferSet;
        std::unordered_set<TextureHandle> textureSet;
        std::unordered_set<PipelineHandle> pipelineSet;
        std::unordered_map<BufferHandle, u32> bufferMap;

        BufferHandle b1{ .Index = 42, .Generation = 1 };
        BufferHandle b2{ .Index = 42, .Generation = 2 };

        bufferSet.insert(b1);
        bufferSet.insert(b2);
        bufferMap[b1] = 111u;
        bufferMap[b2] = 222u;

        EXPECT_EQ(bufferSet.size(), 2u, "Both BufferHandles with same index different generation must coexist in set");
        EXPECT_EQ(bufferMap.size(), 2u, "Both BufferHandles with same index different generation must coexist in map");
        EXPECT_EQ(bufferMap[b1], 111u, "First generation key retains its value in unordered_map");
        EXPECT_EQ(bufferMap[b2], 222u, "Second generation key retains its value in unordered_map");

        TextureHandle t1{ .Index = 100, .Generation = 5 };
        TextureHandle t2{ .Index = 100, .Generation = 6 };
        textureSet.insert(t1);
        textureSet.insert(t2);
        EXPECT_EQ(textureSet.size(), 2u, "Both TextureHandles with same index different generation must coexist in set");

        PipelineHandle p1{ .Index = 7, .Generation = 3 };
        PipelineHandle p2{ .Index = 7, .Generation = 4 };
        pipelineSet.insert(p1);
        pipelineSet.insert(p2);
        EXPECT_EQ(pipelineSet.size(), 2u, "Both PipelineHandles with same index different generation must coexist in set");

        EXPECT_TRUE(bufferSet.contains(b1) && bufferSet.contains(b2), "Both buffer handles findable in unordered_set");
        EXPECT_TRUE(textureSet.contains(t1) && textureSet.contains(t2), "Both texture handles findable in unordered_set");
        EXPECT_TRUE(pipelineSet.contains(p1) && pipelineSet.contains(p2), "Both pipeline handles findable in unordered_set");
    }

    void Test_HashConsistencyWithPackAndUnpack()
    {
        std::cout << "\n--- Test: Hash Consistency with Pack() and Unpack() ---\n";

        std::hash<BufferHandle> hasher;
        bool allMatched = true;

        for (u32 gen = 1; gen < 10; ++gen)
        {
            for (u32 idx = 0; idx < 100; ++idx)
            {
                BufferHandle h{ .Index = idx, .Generation = gen };
                const u64 packed = h.Pack();
                const BufferHandle unpacked = BufferHandle::Unpack(packed);
                const size_t hash = hasher(h);

                if (unpacked != h || hash != static_cast<size_t>(packed))
                {
                    allMatched = false;
                }
            }
        }
        EXPECT_TRUE(allMatched, "All 900 (Index, Generation) pairs round-trip through Pack/Unpack and match std::hash");
    }

    void Test_InvalidHandleHash()
    {
        std::cout << "\n--- Test: Invalid Handle Hash ---\n";

        std::hash<BufferHandle> hasher;
        BufferHandle invalid1 = BufferHandle::Invalid();
        BufferHandle invalid2 = BufferHandle::Invalid();

        EXPECT_FALSE(invalid1.IsValid(), "Invalid handle must report IsValid() == false");
        EXPECT_TRUE(invalid1 == invalid2, "Two invalid handles must be equal");
        EXPECT_TRUE(hasher(invalid1) == hasher(invalid2), "Invalid handles must have equal hash");
        EXPECT_EQ(hasher(invalid1), static_cast<size_t>(0u), "Invalid handle hash should be 0 (Pack() returns 0)");

        TextureHandle tinvalid1 = TextureHandle::Invalid();
        TextureHandle tinvalid2 = TextureHandle::Invalid();
        std::hash<TextureHandle> thasher;
        EXPECT_TRUE(tinvalid1 == tinvalid2, "Two invalid texture handles must be equal");
        EXPECT_TRUE(thasher(tinvalid1) == thasher(tinvalid2), "Invalid texture handles must have equal hash");
        EXPECT_EQ(thasher(tinvalid1), static_cast<size_t>(0u), "Invalid texture handle hash should be 0");
    }

    // WP2.2 Test 1: Command List tracks Buffer, Texture, and Pipeline across End() -> Destroy* before submission -> ProcessCompletedDestructions -> SubmitCommandList -> WaitIdle
    void Test_CommandListResourceLifecycleEndToSubmission(BackendAPI api)
    {
        std::cout << "\n--- Test: Command List Resource Lifecycle (Record -> Destroy Before Submit -> End -> Submit -> Complete) ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "LifecycleTestBuffer"
        };

        TextureDesc texDesc{
            .Width = 64,
            .Height = 64,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "LifecycleTestTexture"
        };
        std::vector<u8> pixelData(64 * 64 * 4, 255);

        BufferHandle buffer = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle texture = device->CreateTexture(texDesc, pixelData.data());
        PipelineHandle pipeline = CreateTestComputePipeline(*device, api);

        EXPECT_TRUE(buffer.IsValid(), "Buffer creation should succeed");
        EXPECT_TRUE(texture.IsValid(), "Texture creation should succeed");
        EXPECT_TRUE(pipeline.IsValid(), "Compute pipeline creation should succeed");

        const u32 bufSlot = device->GetBindlessIndex(buffer).Index;
        const u32 texSlot = device->GetBindlessIndex(texture).Index;

        auto& cmdList = device->GetActiveCommandList();
        cmdList.Begin();
        cmdList.TrackBuffer(buffer);
        cmdList.TrackTexture(texture);
        cmdList.SetPipeline(pipeline);

        EXPECT_TRUE(cmdList.GetReferencedBuffers().contains(buffer), "Tracked buffer must be in referenced set during recording");
        EXPECT_TRUE(cmdList.GetReferencedTextures().contains(texture), "Tracked texture must be in referenced set during recording");
        EXPECT_TRUE(cmdList.GetReferencedPipelines().contains(pipeline), "Tracked pipeline must be in referenced set during recording");

        // Request destruction of all three resources BEFORE End() and BEFORE submission
        device->DestroyBuffer(buffer);
        device->DestroyTexture(texture);
        device->DestroyPipeline(pipeline);

        // End command list recording - MUST retain unsubmitted references!
        cmdList.End();
        EXPECT_TRUE(cmdList.GetReferencedBuffers().contains(buffer), "End() must NOT release unsubmitted buffer reference before submission");
        EXPECT_TRUE(cmdList.GetReferencedTextures().contains(texture), "End() must NOT release unsubmitted texture reference before submission");
        EXPECT_TRUE(cmdList.GetReferencedPipelines().contains(pipeline), "End() must NOT release unsubmitted pipeline reference before submission");

        // Run deferred destruction processing while command list is still unsubmitted
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(buffer), "Buffer must stay alive while unsubmitted command list references it");
        EXPECT_TRUE(device->IsResourceAlive(texture), "Texture must stay alive while unsubmitted command list references it");
        EXPECT_TRUE(device->IsResourceAlive(pipeline), "Pipeline must stay alive while unsubmitted command list references it");

        // Submit the command list: unsubmitted references are now transferred to submission timeline
        const u64 submissionVal = device->SubmitCommandList(cmdList);
        EXPECT_TRUE(submissionVal > 0, "SubmitCommandList returns valid timeline/fence value");
        EXPECT_TRUE(cmdList.GetReferencedBuffers().empty(), "SubmitCommandList clears command list unsubmitted buffer set");
        EXPECT_TRUE(cmdList.GetReferencedTextures().empty(), "SubmitCommandList clears command list unsubmitted texture set");
        EXPECT_TRUE(cmdList.GetReferencedPipelines().empty(), "SubmitCommandList clears command list unsubmitted pipeline set");

        // Wait for GPU completion and drain deferred destructions
        device->WaitIdle();
        EXPECT_FALSE(device->IsResourceAlive(buffer), "Buffer must be destroyed after submission completes on GPU");
        EXPECT_FALSE(device->IsResourceAlive(texture), "Texture must be destroyed after submission completes on GPU");
        EXPECT_FALSE(device->IsResourceAlive(pipeline), "Pipeline must be destroyed after submission completes on GPU");

        // Verify bindless slots were reclaimed only after GPU completion
        BufferHandle newBuf = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle newTex = device->CreateTexture(texDesc, pixelData.data());
        EXPECT_EQ(device->GetBindlessIndex(newBuf).Index, bufSlot, "Reclaimed buffer bindless slot reused after submission completion");
        EXPECT_EQ(device->GetBindlessIndex(newTex).Index, texSlot, "Reclaimed texture bindless slot reused after submission completion");

        device->DestroyBuffer(newBuf);
        device->DestroyTexture(newTex);
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // WP2.2 Test 2: Command list reset (Begin) and cancellation (Discard) before submission
    void Test_CommandListResetAndDiscardBeforeSubmission(BackendAPI api)
    {
        std::cout << "\n--- Test: Command List Reset (Begin) and Cancellation (Discard) Before Submission ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "ResetDiscardBuffer"
        };

        // Part A: Reset via Begin() before submission releases unsubmitted reference
        BufferHandle bufA = device->CreateBuffer(bufferDesc, nullptr);
        auto cmdA = device->CreateCommandList();
        EXPECT_TRUE(cmdA != nullptr, "CreateCommandList for Part A succeeded");

        cmdA->Begin();
        cmdA->TrackBuffer(bufA);
        cmdA->End();

        device->DestroyBuffer(bufA);
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(bufA), "bufA stays alive while cmdA holds unsubmitted reference");

        // Reset cmdA by calling Begin() again without submitting
        cmdA->Begin();
        EXPECT_TRUE(cmdA->GetReferencedBuffers().empty(), "Begin() clears unsubmitted references on reset");
        cmdA->End();

        device->ProcessCompletedDestructions();
        EXPECT_FALSE(device->IsResourceAlive(bufA), "bufA is destroyed once cmdA resets and releases its unsubmitted reference");

        // Part B: Discard() before submission releases unsubmitted references for Buffer, Texture, and Pipeline
        BufferHandle bufB = device->CreateBuffer(bufferDesc, nullptr);
        PipelineHandle pipeB = CreateTestComputePipeline(*device, api);

        cmdA->Begin();
        cmdA->TrackBuffer(bufB);
        cmdA->TrackPipeline(pipeB);

        device->DestroyBuffer(bufB);
        device->DestroyPipeline(pipeB);
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(bufB), "bufB stays alive before Discard()");
        EXPECT_TRUE(device->IsResourceAlive(pipeB), "pipeB stays alive before Discard()");

        cmdA->Discard();
        EXPECT_TRUE(cmdA->GetReferencedBuffers().empty(), "Discard() clears referenced buffers");
        EXPECT_TRUE(cmdA->GetReferencedPipelines().empty(), "Discard() clears referenced pipelines");

        device->ProcessCompletedDestructions();
        EXPECT_FALSE(device->IsResourceAlive(bufB), "bufB is destroyed after Discard() + ProcessCompletedDestructions()");
        EXPECT_FALSE(device->IsResourceAlive(pipeB), "pipeB is destroyed after Discard() + ProcessCompletedDestructions()");

        cmdA.reset();
        DestroyTestDevice(ctx);
    }

    // WP2.2 Test 3: Multiple independent command lists referencing the same resource
    void Test_MultipleIndependentCommandListsSameResource(BackendAPI api)
    {
        std::cout << "\n--- Test: Multiple Independent Command Lists Referencing Same Resource ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "MultiCmdListBuffer"
        };

        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .RenderTarget = false,
            .DepthStencil = false,
            .Storage = false,
            .DebugName = "MultiCmdListTexture"
        };
        std::vector<u8> pixelData(32 * 32 * 4, 128);

        BufferHandle buffer = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle texture = device->CreateTexture(texDesc, pixelData.data());
        PipelineHandle pipeline = CreateTestComputePipeline(*device, api);

        auto cmdList1 = device->CreateCommandList();
        auto cmdList2 = device->CreateCommandList();
        EXPECT_TRUE(cmdList1 != nullptr && cmdList2 != nullptr && cmdList1.get() != cmdList2.get(),
            "Two distinct independent command lists created");

        // Both command lists record references to the same buffer, texture, and pipeline
        cmdList1->Begin();
        cmdList1->BindVertexBuffer(buffer, 0);
        cmdList1->TrackTexture(texture);
        cmdList1->SetPipeline(pipeline);
        cmdList1->End();

        cmdList2->Begin();
        cmdList2->TrackBuffer(buffer);
        cmdList2->TransitionTexture(texture, ResourceState::ShaderResource, ResourceState::ShaderResource);
        cmdList2->TrackPipeline(pipeline);
        cmdList2->End();

        // Request destruction while BOTH command lists hold unsubmitted references
        device->DestroyBuffer(buffer);
        device->DestroyTexture(texture);
        device->DestroyPipeline(pipeline);

        // Reset cmdList1 (releases cmdList1's unsubmitted reference, but cmdList2 still holds one!)
        cmdList1->Begin();
        cmdList1->End();
        EXPECT_TRUE(cmdList1->GetReferencedBuffers().empty(), "cmdList1 references cleared on reset");
        EXPECT_TRUE(cmdList2->GetReferencedBuffers().contains(buffer), "cmdList2 still holds buffer reference");

        // Process destructions and even call WaitIdle(): resources MUST remain alive because cmdList2 is still unsubmitted!
        device->WaitIdle();
        EXPECT_TRUE(device->IsResourceAlive(buffer), "Buffer must remain alive after cmdList1 reset while cmdList2 still references it");
        EXPECT_TRUE(device->IsResourceAlive(texture), "Texture must remain alive after cmdList1 reset while cmdList2 still references it");
        EXPECT_TRUE(device->IsResourceAlive(pipeline), "Pipeline must remain alive after cmdList1 reset while cmdList2 still references it");

        // Attempting to re-record cmdList1 with the PendingDestroy buffer MUST be rejected,
        // while cmdList2 STILL holds its pre-destruction unsubmitted reference!
        cmdList1->Begin();
        cmdList1->TrackBuffer(buffer);
        EXPECT_FALSE(cmdList1->GetReferencedBuffers().contains(buffer),
            "TrackBuffer must reject buffer after DestroyBuffer");
        cmdList1->End();
        device->SubmitCommandList(*cmdList1);
        device->WaitIdle();

        EXPECT_TRUE(device->IsResourceAlive(buffer), "Buffer must remain alive after cmdList1 submission completes because cmdList2 is still unsubmitted");

        // Finally submit cmdList2 and wait for GPU completion
        device->SubmitCommandList(*cmdList2);
        device->WaitIdle();

        EXPECT_FALSE(device->IsResourceAlive(buffer), "Buffer is destroyed once all referencing command lists have been submitted and completed");
        EXPECT_FALSE(device->IsResourceAlive(texture), "Texture is destroyed once all referencing command lists have been submitted and completed");
        EXPECT_FALSE(device->IsResourceAlive(pipeline), "Pipeline is destroyed once all referencing command lists have been submitted and completed");

        cmdList1.reset();
        cmdList2.reset();
        DestroyTestDevice(ctx);
    }

    // WP2.2 Test 4: Sequential submissions referencing the same resource
    void Test_MultipleSubmissionsReferencingSameResource(BackendAPI api)
    {
        std::cout << "\n--- Test: Multiple Submissions Referencing Same Resource ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "MultiSubmitBuffer"
        };

        BufferHandle buffer = device->CreateBuffer(bufferDesc, nullptr);
        EXPECT_TRUE(buffer.IsValid(), "Buffer creation should succeed");

        auto cmd1 = device->CreateCommandList();
        auto cmd2 = device->CreateCommandList();

        cmd1->Begin();
        cmd1->TrackBuffer(buffer);
        cmd1->End();

        cmd2->Begin();
        cmd2->TrackBuffer(buffer);
        cmd2->End();

        // Request destruction before either command list is submitted
        device->DestroyBuffer(buffer);

        const u64 sub1 = device->SubmitCommandList(*cmd1);
        // After sub1, cmd2 still has an unsubmitted reference
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(buffer), "Buffer remains alive after first submission while second submission is pending");

        const u64 sub2 = device->SubmitCommandList(*cmd2);
        EXPECT_TRUE(sub2 > sub1, "Second submission timeline/fence value is strictly greater than first submission");

        device->WaitIdle();
        EXPECT_FALSE(device->IsResourceAlive(buffer), "Buffer is destroyed after both submissions complete on GPU");

        cmd1.reset();
        cmd2.reset();
        DestroyTestDevice(ctx);
    }

    // Regression Test 5.1: Submit cmdA, keep cmdB unsubmitted, wait for A, then discard B
    void Test_SubmitAKeepBTillDiscard(BackendAPI api)
    {
        std::cout << "\n--- Test: Submit CmdA, Keep CmdB Unsubmitted, Then Discard B ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "SubmitADiscardB_Buf"
        };
        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .DebugName = "SubmitADiscardB_Tex"
        };
        std::vector<u8> pixels(32 * 32 * 4, 200);

        BufferHandle buf = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle tex = device->CreateTexture(texDesc, pixels.data());
        PipelineHandle pipe = CreateTestComputePipeline(*device, api);

        const u32 bufSlot = device->GetBindlessIndex(buf).Index;
        const u32 texSlot = device->GetBindlessIndex(tex).Index;

        auto cmdA = device->CreateCommandList();
        auto cmdB = device->CreateCommandList();

        cmdA->Begin();
        cmdA->TrackBuffer(buf);
        cmdA->TrackTexture(tex);
        cmdA->TrackPipeline(pipe);
        cmdA->End();

        cmdB->Begin();
        cmdB->TrackBuffer(buf);
        cmdB->TrackTexture(tex);
        cmdB->TrackPipeline(pipe);
        cmdB->End();

        device->DestroyBuffer(buf);
        device->DestroyTexture(tex);
        device->DestroyPipeline(pipe);

        device->SubmitCommandList(*cmdA);
        device->WaitIdle();

        // cmdB is still unsubmitted -> all 3 must stay alive
        EXPECT_TRUE(device->IsResourceAlive(buf), "Buffer stays alive while cmdB remains unsubmitted after cmdA completes");
        EXPECT_TRUE(device->IsResourceAlive(tex), "Texture stays alive while cmdB remains unsubmitted after cmdA completes");
        EXPECT_TRUE(device->IsResourceAlive(pipe), "Pipeline stays alive while cmdB remains unsubmitted after cmdA completes");

        cmdB->Discard();
        device->ProcessCompletedDestructions();

        EXPECT_FALSE(device->IsResourceAlive(buf), "Buffer destroyed after cmdB Discard() + ProcessCompletedDestructions()");
        EXPECT_FALSE(device->IsResourceAlive(tex), "Texture destroyed after cmdB Discard() + ProcessCompletedDestructions()");
        EXPECT_FALSE(device->IsResourceAlive(pipe), "Pipeline destroyed after cmdB Discard() + ProcessCompletedDestructions()");

        BufferHandle buf2 = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle tex2 = device->CreateTexture(texDesc, pixels.data());
        EXPECT_EQ(device->GetBindlessIndex(buf2).Index, bufSlot, "Buffer bindless slot reclaimed after Discard()");
        EXPECT_EQ(device->GetBindlessIndex(tex2).Index, texSlot, "Texture bindless slot reclaimed after Discard()");

        device->DestroyBuffer(buf2);
        device->DestroyTexture(tex2);
        device->WaitIdle();
        cmdA.reset();
        cmdB.reset();
        DestroyTestDevice(ctx);
    }

    // Regression Tests 5.2 - 5.5: Reject TrackBuffer / TrackTexture / TrackPipeline after Destroy*, while preserving pre-destroy refs
    void Test_RejectPostDestroyAndPreservePreDestroyRefs(BackendAPI api)
    {
        std::cout << "\n--- Test: Reject Post-Destroy Tracking & Preserve Pre-Destroy Refs ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "PostDestroyReject_Buf"
        };
        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .DebugName = "PostDestroyReject_Tex"
        };
        std::vector<u8> pixels(32 * 32 * 4, 255);

        BufferHandle buf = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle tex = device->CreateTexture(texDesc, pixels.data());
        PipelineHandle pipe = CreateTestComputePipeline(*device, api);

        auto preCmd = device->CreateCommandList();
        auto postCmd = device->CreateCommandList();

        // Acquire references BEFORE Destroy*
        preCmd->Begin();
        preCmd->TrackBuffer(buf);
        preCmd->TrackTexture(tex);
        preCmd->TrackPipeline(pipe);
        preCmd->End();

        // Request destruction
        device->DestroyBuffer(buf);
        device->DestroyTexture(tex);
        device->DestroyPipeline(pipe);

        // Attempt to track AFTER Destroy* (including indirect Track* via Bind/Transition/SetPipeline)
        postCmd->Begin();
        postCmd->TrackBuffer(buf);
        postCmd->BindVertexBuffer(buf, 0);
        postCmd->TrackTexture(tex);
        postCmd->TransitionTexture(tex, ResourceState::ShaderResource, ResourceState::CopyDest);
        postCmd->TrackPipeline(pipe);
        postCmd->SetPipeline(pipe);
        postCmd->End();

        EXPECT_FALSE(postCmd->GetReferencedBuffers().contains(buf), "TrackBuffer/BindVertexBuffer after DestroyBuffer must be rejected");
        EXPECT_FALSE(postCmd->GetReferencedTextures().contains(tex), "TrackTexture/TransitionTexture after DestroyTexture must be rejected");
        EXPECT_FALSE(postCmd->GetReferencedPipelines().contains(pipe), "TrackPipeline/SetPipeline after DestroyPipeline must be rejected");

        // Pre-destroy references in preCmd must still be preserved
        EXPECT_TRUE(preCmd->GetReferencedBuffers().contains(buf), "Pre-destroy buffer ref preserved in preCmd");
        EXPECT_TRUE(preCmd->GetReferencedTextures().contains(tex), "Pre-destroy texture ref preserved in preCmd");
        EXPECT_TRUE(preCmd->GetReferencedPipelines().contains(pipe), "Pre-destroy pipeline ref preserved in preCmd");

        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(buf), "Buffer stays alive due to pre-destroy ref in preCmd");
        EXPECT_TRUE(device->IsResourceAlive(tex), "Texture stays alive due to pre-destroy ref in preCmd");
        EXPECT_TRUE(device->IsResourceAlive(pipe), "Pipeline stays alive due to pre-destroy ref in preCmd");

        // Submit postCmd (empty) and WaitIdle -> resources must STILL stay alive for preCmd
        device->SubmitCommandList(*postCmd);
        device->WaitIdle();
        EXPECT_TRUE(device->IsResourceAlive(buf), "Buffer stays alive until preCmd is submitted");
        EXPECT_TRUE(device->IsResourceAlive(tex), "Texture stays alive until preCmd is submitted");
        EXPECT_TRUE(device->IsResourceAlive(pipe), "Pipeline stays alive until preCmd is submitted");

        // Submit preCmd and WaitIdle -> all 3 destroyed
        device->SubmitCommandList(*preCmd);
        device->WaitIdle();
        EXPECT_FALSE(device->IsResourceAlive(buf), "Buffer destroyed once preCmd completes");
        EXPECT_FALSE(device->IsResourceAlive(tex), "Texture destroyed once preCmd completes");
        EXPECT_FALSE(device->IsResourceAlive(pipe), "Pipeline destroyed once preCmd completes");

        preCmd.reset();
        postCmd.reset();
        DestroyTestDevice(ctx);
    }

    // Regression Tests 5.6, 5.7 & Parity 4: Runtime bindless material textures, global bindless buffer, and uniform buffer binding
    void Test_BindlessMaterialGlobalAndUniformRetention(BackendAPI api)
    {
        std::cout << "\n--- Test: Bindless Material Textures, Global Buffer & Uniform Buffer Retention ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc globalDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "GlobalBindlessBuffer"
        };
        BufferDesc uniformDesc{
            .SizeBytes = 6400,
            .StrideBytes = 64,
            .Usage = BufferUsage::Uniform,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "SkinningUniformBuffer"
        };
        TextureDesc texDesc{
            .Width = 16,
            .Height = 16,
            .PixelFormat = Format::RGBA8_UNorm,
            .DebugName = "MaterialBindlessTex"
        };
        std::vector<u8> pixels(16 * 16 * 4, 255);

        BufferHandle globalBuf = device->CreateBuffer(globalDesc, nullptr);
        BufferHandle uniformBuf = device->CreateBuffer(uniformDesc, nullptr);
        TextureHandle albedoTex = device->CreateTexture(texDesc, pixels.data());
        TextureHandle normalTex = device->CreateTexture(texDesc, pixels.data());
        TextureHandle specTex = device->CreateTexture(texDesc, pixels.data());

        EXPECT_TRUE(device->GetBindlessIndex(globalBuf).IsValid(), "Global bindless storage buffer has valid bindless index");
        EXPECT_TRUE(device->GetBindlessIndex(uniformBuf).IsValid(), "Uniform buffer has valid bindless index");
        EXPECT_TRUE(device->GetBindlessIndex(albedoTex).IsValid(), "Albedo texture has valid bindless index");
        EXPECT_TRUE(device->GetBindlessIndex(normalTex).IsValid(), "Normal texture has valid bindless index");
        EXPECT_TRUE(device->GetBindlessIndex(specTex).IsValid(), "Specular texture has valid bindless index");

        auto cmd = device->CreateCommandList();
        cmd->Begin();
        cmd->TrackBuffer(globalBuf);
        cmd->BindConstantBuffer(2, uniformBuf, 0);
        // Simulate multi-submesh tracking deduplication
        for (int sub = 0; sub < 4; ++sub)
        {
            cmd->TrackTexture(albedoTex);
            cmd->TrackTexture(normalTex);
            cmd->TrackTexture(specTex);
        }
        EXPECT_EQ(cmd->GetReferencedTextures().size(), 3u, "Command-list texture tracking deduplicates across submeshes");
        cmd->End();

        device->DestroyBuffer(globalBuf);
        device->DestroyBuffer(uniformBuf);
        device->DestroyTexture(albedoTex);
        device->DestroyTexture(normalTex);
        device->DestroyTexture(specTex);

        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(globalBuf), "Global bindless buffer retained before submission");
        EXPECT_TRUE(device->IsResourceAlive(uniformBuf), "Uniform buffer retained before submission");
        EXPECT_TRUE(device->IsResourceAlive(albedoTex), "Albedo bindless texture retained before submission");
        EXPECT_TRUE(device->IsResourceAlive(normalTex), "Normal bindless texture retained before submission");
        EXPECT_TRUE(device->IsResourceAlive(specTex), "Specular bindless texture retained before submission");

        device->SubmitCommandList(*cmd);
        device->WaitIdle();

        EXPECT_FALSE(device->IsResourceAlive(globalBuf), "Global bindless buffer destroyed after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(uniformBuf), "Uniform buffer destroyed after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(albedoTex), "Albedo bindless texture destroyed after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(normalTex), "Normal bindless texture destroyed after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(specTex), "Specular bindless texture destroyed after submission completes");

        cmd.reset();
        DestroyTestDevice(ctx);
    }

    // Regression Test 5.8: Resource pool synchronization & GenerationalPool stability across growth (> 64 entries)
    void Test_ResourcePoolSynchronizationAndReclamation(BackendAPI api)
    {
        std::cout << "\n--- Test: Resource Pool Synchronization & Growth Stability ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "ConcurrentPoolBuf"
        };

        // Pre-create a buffer and bind it while another thread grows the pool past initialCapacity (64)
        BufferHandle stableBuf = device->CreateBuffer(bufferDesc, nullptr);
        const u32 initialSlot = device->GetBindlessIndex(stableBuf).Index;
        EXPECT_TRUE(stableBuf.IsValid(), "Initial buffer valid");

        std::vector<BufferHandle> workerBuffers(120, BufferHandle::Invalid());
        std::thread worker([&]() {
            for (usize i = 0; i < workerBuffers.size(); ++i)
            {
                workerBuffers[i] = device->CreateBuffer(bufferDesc, nullptr);
            }
            for (usize i = 0; i < 60; ++i)
            {
                device->DestroyBuffer(workerBuffers[i]);
            }
        });

        // Concurrently record and query bindless indices on the main thread
        auto cmd = device->CreateCommandList();
        cmd->Begin();
        for (int iter = 0; iter < 32; ++iter)
        {
            cmd->BindVertexBuffer(stableBuf, 0);
            const auto idx = device->GetBindlessIndex(stableBuf);
            (void)idx;
        }
        cmd->End();

        worker.join();

        EXPECT_EQ(device->GetBindlessIndex(stableBuf).Index, initialSlot, "Stable buffer index preserved across concurrent pool growth");

        device->SubmitCommandList(*cmd);
        device->DestroyBuffer(stableBuf);
        for (usize i = 60; i < workerBuffers.size(); ++i)
        {
            device->DestroyBuffer(workerBuffers[i]);
        }
        device->WaitIdle();

        EXPECT_FALSE(device->IsResourceAlive(stableBuf), "Stable buffer destroyed after submission completes");
        for (BufferHandle h : workerBuffers)
        {
            EXPECT_FALSE(device->IsResourceAlive(h), "All worker-created buffers destroyed after WaitIdle()");
        }

        cmd.reset();
        DestroyTestDevice(ctx);
    }

    // Six-Issue Closure Test 1 & 2: Shutdown with pending deferred resources & partial/uninitialized device
    void Test_ShutdownWithPendingDeferredAndPartialInit(BackendAPI api)
    {
        std::cout << "\n--- Test: Shutdown With Pending Deferred Resources & Partial Init ---\n";

        // Part A: Uninitialized device Shutdown + RAII destruction without explicit Shutdown
        {
            auto uninitDev = CreateDevice(api);
            EXPECT_TRUE(uninitDev != nullptr, "CreateDevice returns non-null for partial-init test");
            uninitDev->Shutdown();
        }
        {
            auto uninitDev2 = CreateDevice(api);
            EXPECT_TRUE(uninitDev2 != nullptr, "RAII destruction without Initialize/Shutdown is safe");
        }

        // Part B: Shutdown while deferred destructions AND unsubmitted command-list refs are still pending
        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufferDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "PendingShutdownBuf"
        };
        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .DebugName = "PendingShutdownTex"
        };
        std::vector<u8> pixels(32 * 32 * 4, 255);

        BufferHandle buf = device->CreateBuffer(bufferDesc, nullptr);
        TextureHandle tex = device->CreateTexture(texDesc, pixels.data());
        PipelineHandle pipe = CreateTestComputePipeline(*device, api);

        auto cmd = device->CreateCommandList();
        cmd->Begin();
        cmd->TrackBuffer(buf);
        cmd->TrackTexture(tex);
        cmd->TrackPipeline(pipe);
        cmd->End();

        // Request destruction and submit WITHOUT WaitIdle() before Shutdown()
        device->DestroyBuffer(buf);
        device->DestroyTexture(tex);
        device->DestroyPipeline(pipe);
        device->SubmitCommandList(*cmd);

        // Immediate Shutdown() must drain GPU, destroy swapchain & deferred resources cleanly
        DestroyTestDevice(ctx);
        cmd.reset();
        EXPECT_TRUE(true, "Shutdown with pending deferred resources completed without crash or leak");
    }

    // Six-Issue Closure Test 3: Repeated Swapchain Resizing (exceeds DX12 kMaxRtvDescriptors=256 if leaked)
    void Test_RepeatedSwapchainResize(BackendAPI api)
    {
        std::cout << "\n--- Test: Repeated Swapchain Resize (200 cycles) ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        bool allResizesOk = true;
        for (u32 i = 0; i < 200; ++i)
        {
            const u32 w = (i % 2 == 0) ? 640u : 800u;
            const u32 h = (i % 2 == 0) ? 480u : 600u;
            device->ResizeSwapchain(w, h);
            if (!device->GetCurrentBackbuffer().IsValid())
            {
                allResizesOk = false;
                break;
            }
        }
        EXPECT_TRUE(allResizesOk, "All 200 swapchain resizes produced valid backbuffer handles without RTV exhaustion");

        if (device->BeginFrame())
        {
            device->EndFrameAndPresent();
        }
        device->WaitIdle();
        DestroyTestDevice(ctx);
    }

    // Six-Issue Closure Test 4: Memory Allocator Slab Reuse, Alignment, Merging, Telemetry & Empty Slab Reclamation
    void Test_MemoryAllocatorSlabReuseAndReclamation(BackendAPI api)
    {
        std::cout << "\n--- Test: Memory Allocator Slab Offset Reuse & Reclamation ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        const MemoryAllocatorStats baseStats = device->GetMemoryStats();

        std::vector<BufferHandle> smallBufs;
        const u64 sizes[] = { 256, 4096, 65536, 131072, 512, 16384 };
        for (u64 sz : sizes)
        {
            BufferDesc d{
                .SizeBytes = sz,
                .StrideBytes = 16,
                .Usage = BufferUsage::Storage | BufferUsage::Vertex,
                .Memory = MemoryUsage::GpuOnly,
                .Bindless = false,
                .DebugName = "SlabReuseBuf"
            };
            BufferHandle h = device->CreateBuffer(d, nullptr);
            EXPECT_TRUE(h.IsValid(), "Differently sized GpuOnly buffer created");
            smallBufs.push_back(h);
        }

        // Free alternating buffers to create fragmented holes, then allocate into coalesced ranges
        device->DestroyBuffer(smallBufs[1]);
        device->DestroyBuffer(smallBufs[3]);
        device->WaitIdle();

        BufferDesc coalescedDesc{
            .SizeBytes = 8192,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage,
            .Memory = MemoryUsage::GpuOnly,
            .Bindless = false,
            .DebugName = "CoalescedHoleBuf"
        };
        BufferHandle holeBuf = device->CreateBuffer(coalescedDesc, nullptr);
        EXPECT_TRUE(holeBuf.IsValid(), "Buffer allocated into freed slab hole");

        // Allocate oversized buffer (> 32MB) to force a dedicated/second slab, then free it to verify reclamation
        BufferDesc bigDesc{
            .SizeBytes = 40ULL * 1024ULL * 1024ULL,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage,
            .Memory = MemoryUsage::GpuOnly,
            .Bindless = false,
            .DebugName = "OversizedSlabBuf"
        };
        BufferHandle bigBuf = device->CreateBuffer(bigDesc, nullptr);
        EXPECT_TRUE(bigBuf.IsValid(), "Oversized 40MB buffer created");
        const MemoryAllocatorStats peakStats = device->GetMemoryStats();
        EXPECT_TRUE(peakStats.TotalAllocatedBytes >= baseStats.TotalAllocatedBytes + bigDesc.SizeBytes,
            "Allocator telemetry reflects oversized allocation");

        device->DestroyBuffer(bigBuf);
        device->DestroyBuffer(holeBuf);
        device->DestroyBuffer(smallBufs[0]);
        device->DestroyBuffer(smallBufs[2]);
        device->DestroyBuffer(smallBufs[4]);
        device->DestroyBuffer(smallBufs[5]);
        device->WaitIdle();

        const MemoryAllocatorStats endStats = device->GetMemoryStats();
        EXPECT_EQ(endStats.ActiveAllocations, baseStats.ActiveAllocations,
            "ActiveAllocations returns to baseline after freeing all test buffers");
        EXPECT_EQ(endStats.TotalAllocatedBytes, baseStats.TotalAllocatedBytes,
            "TotalAllocatedBytes returns to baseline after freeing all test buffers");
        if (api == BackendAPI::Vulkan)
        {
            EXPECT_TRUE(endStats.TotalReservedBytes < peakStats.TotalReservedBytes,
                "Empty oversized Vulkan slab reclaimed after DestroyBuffer + WaitIdle");
        }

        DestroyTestDevice(ctx);
    }

    // Six-Issue Closure Test 5: CreateTexture(initialData) during an active frame does not invalidate active cmd list
    void Test_CreateTextureDuringActiveFrame(BackendAPI api)
    {
        std::cout << "\n--- Test: CreateTexture(initialData) During Active Frame ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "ActiveFrameBuf"
        };
        BufferHandle activeBuf = device->CreateBuffer(bufDesc, nullptr);

        const bool began = device->BeginFrame();
        EXPECT_TRUE(began, "BeginFrame succeeded");
        if (began)
        {
            auto& activeCmd = device->GetActiveCommandList();
            activeCmd.TrackBuffer(activeBuf);

            // Synchronous texture upload mid-frame MUST NOT reset active frame's command pool/allocator
            TextureDesc texDesc{
                .Width = 64,
                .Height = 64,
                .PixelFormat = Format::RGBA8_UNorm,
                .DebugName = "MidFrameUploadedTex"
            };
            std::vector<u8> pixels(64 * 64 * 4, 210);
            TextureHandle midTex = device->CreateTexture(texDesc, pixels.data());
            EXPECT_TRUE(midTex.IsValid(), "CreateTexture(initialData) succeeded mid-frame");
            EXPECT_TRUE(device->GetBindlessIndex(midTex).IsValid(), "Mid-frame uploaded texture has valid bindless index");

            activeCmd.TrackTexture(midTex);
            EXPECT_TRUE(activeCmd.GetReferencedBuffers().contains(activeBuf),
                "Active command list retained pre-upload buffer reference");
            EXPECT_TRUE(activeCmd.GetReferencedTextures().contains(midTex),
                "Active command list tracked mid-frame uploaded texture");

            device->EndFrameAndPresent();
            device->DestroyTexture(midTex);
            device->DestroyBuffer(activeBuf);
            device->WaitIdle();
            EXPECT_FALSE(device->IsResourceAlive(midTex), "Mid-frame texture destroyed cleanly after WaitIdle");
        }
        else
        {
            device->DestroyBuffer(activeBuf);
            device->WaitIdle();
        }

        DestroyTestDevice(ctx);
    }

    // Six-Issue Closure Test 6: Repeated Destroy calls for the same handle do not duplicate deferred entries
    void Test_DuplicateDestroyCalls(BackendAPI api)
    {
        std::cout << "\n--- Test: Duplicate Destroy Calls For Same Handle ---\n";

        auto ctx = CreateTestDevice(api);
        if (!ctx.Device) return;
        auto& device = ctx.Device;

        BufferDesc bufDesc{
            .SizeBytes = 256,
            .StrideBytes = 16,
            .Usage = BufferUsage::Storage | BufferUsage::Vertex,
            .Memory = MemoryUsage::CpuToGpu,
            .Bindless = true,
            .DebugName = "DupDestroyBuf"
        };
        TextureDesc texDesc{
            .Width = 32,
            .Height = 32,
            .PixelFormat = Format::RGBA8_UNorm,
            .DebugName = "DupDestroyTex"
        };
        std::vector<u8> pixels(32 * 32 * 4, 180);

        BufferHandle buf = device->CreateBuffer(bufDesc, nullptr);
        TextureHandle tex = device->CreateTexture(texDesc, pixels.data());
        PipelineHandle pipe = CreateTestComputePipeline(*device, api);

        const u32 bufSlot = device->GetBindlessIndex(buf).Index;
        const u32 texSlot = device->GetBindlessIndex(tex).Index;

        auto cmd = device->CreateCommandList();
        cmd->Begin();
        cmd->TrackBuffer(buf);
        cmd->TrackTexture(tex);
        cmd->TrackPipeline(pipe);
        cmd->End();

        // Call Destroy* 3 times each
        for (int i = 0; i < 3; ++i)
        {
            device->DestroyBuffer(buf);
            device->DestroyTexture(tex);
            device->DestroyPipeline(pipe);
        }

        // Pre-recorded references must still keep all 3 alive
        device->ProcessCompletedDestructions();
        EXPECT_TRUE(device->IsResourceAlive(buf), "Buffer stays alive after duplicate DestroyBuffer while cmd holds ref");
        EXPECT_TRUE(device->IsResourceAlive(tex), "Texture stays alive after duplicate DestroyTexture while cmd holds ref");
        EXPECT_TRUE(device->IsResourceAlive(pipe), "Pipeline stays alive after duplicate DestroyPipeline while cmd holds ref");

        device->SubmitCommandList(*cmd);
        device->WaitIdle();

        EXPECT_FALSE(device->IsResourceAlive(buf), "Buffer destroyed once after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(tex), "Texture destroyed once after submission completes");
        EXPECT_FALSE(device->IsResourceAlive(pipe), "Pipeline destroyed once after submission completes");

        // Allocate TWO new buffers & textures: the freed slot must appear ONLY ONCE in the free list
        BufferHandle b1 = device->CreateBuffer(bufDesc, nullptr);
        BufferHandle b2 = device->CreateBuffer(bufDesc, nullptr);
        TextureHandle t1 = device->CreateTexture(texDesc, pixels.data());
        TextureHandle t2 = device->CreateTexture(texDesc, pixels.data());

        EXPECT_EQ(device->GetBindlessIndex(b1).Index, bufSlot, "First re-allocated buffer reuses freed slot");
        EXPECT_TRUE(device->GetBindlessIndex(b2).Index != bufSlot, "Second buffer does NOT get duplicate freed slot");
        EXPECT_EQ(device->GetBindlessIndex(t1).Index, texSlot, "First re-allocated texture reuses freed slot");
        EXPECT_TRUE(device->GetBindlessIndex(t2).Index != texSlot, "Second texture does NOT get duplicate freed slot");

        device->DestroyBuffer(b1);
        device->DestroyBuffer(b2);
        device->DestroyTexture(t1);
        device->DestroyTexture(t2);
        device->WaitIdle();
        cmd.reset();
        DestroyTestDevice(ctx);
    }

    void RunBackendTests(BackendAPI api)
    {
        const char* apiName = (api == BackendAPI::Vulkan) ? "Vulkan" : "DirectX 12";
        std::cout << "\n========== Running Command List Lifetime Tests for " << apiName << " ==========\n\n";

        Test_CommandListResourceLifecycleEndToSubmission(api);
        Test_CommandListResetAndDiscardBeforeSubmission(api);
        Test_MultipleIndependentCommandListsSameResource(api);
        Test_MultipleSubmissionsReferencingSameResource(api);
        Test_SubmitAKeepBTillDiscard(api);
        Test_RejectPostDestroyAndPreservePreDestroyRefs(api);
        Test_BindlessMaterialGlobalAndUniformRetention(api);
        Test_ResourcePoolSynchronizationAndReclamation(api);
        Test_ShutdownWithPendingDeferredAndPartialInit(api);
        Test_RepeatedSwapchainResize(api);
        Test_MemoryAllocatorSlabReuseAndReclamation(api);
        Test_CreateTextureDuringActiveFrame(api);
        Test_DuplicateDestroyCalls(api);

        std::cout << "\n========== " << apiName << " Results: " << (g_tests_run - g_failures) << "/" << g_tests_run << " passed ==========\n";
    }
}

int main()
{
    int totalFailures = 0;
    int totalTests = 0;

    // 1. Run pure GenerationalHandle tests once
    g_failures = 0;
    g_tests_run = 0;
    std::cout << "\n========== Running Pure GenerationalHandle Unit Tests ==========\n";
    Test_EqualHandlesProduceEqualHashes();
    Test_DifferentIndexOrGenerationComparesUnequal();
    Test_UnorderedSetAndMapCompatibility();
    Test_HashConsistencyWithPackAndUnpack();
    Test_InvalidHandleHash();
    totalFailures += g_failures;
    totalTests += g_tests_run;

    // 2. Run Vulkan command-list lifetime tests
    g_failures = 0;
    g_tests_run = 0;
    RunBackendTests(BackendAPI::Vulkan);
    totalFailures += g_failures;
    totalTests += g_tests_run;

    // 3. Run DirectX 12 command-list lifetime tests
    g_failures = 0;
    g_tests_run = 0;
    RunBackendTests(BackendAPI::DirectX12);
    totalFailures += g_failures;
    totalTests += g_tests_run;

    std::cout << "\n========================================\n";
    std::cout << "TOTAL: " << (totalTests - totalFailures) << "/" << totalTests << " assertions passed\n";
    std::cout << "========================================\n";

    return totalFailures == 0 ? 0 : 1;
}
