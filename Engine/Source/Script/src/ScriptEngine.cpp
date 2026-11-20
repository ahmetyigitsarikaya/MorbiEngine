#include "Morbi/Script/ScriptEngine.hpp"

#include "Morbi/Asset/ModelImporter.hpp"
#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Diagnostics/CrashHandler.h"
#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/EngineSettings.hpp"
#include "Morbi/Core/Log.hpp"
#include "Morbi/IO/PathService.hpp"
#include "Morbi/Platform/InputState.hpp"
#include "Morbi/UI/GameUIRenderer.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#if MORBI_HAS_DOTNET_HOST
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <coreclr_delegates.h>
    #include <hostfxr.h>
    #include <nethost.h>
#endif

namespace Morbi::Script
{
    namespace
    {
        struct ActiveRuntimeContext
        {
            u32 ActiveWorldId = 1;
            Scene::Scene* ActiveScene = nullptr;
            Physics::PhysicsWorld* Physics = nullptr;
            Audio::AudioWorld* Audio = nullptr;
            UI::GameUIRenderer* GameUI = nullptr;
            Platform::InputState* Input = nullptr;
            IO::PathService* Vfs = nullptr;
            ScriptEngine::SceneHandlerCallback SceneHandler{};
            NetworkBridgeCallbacks NetworkCallbacks{};
        };

        ActiveRuntimeContext g_runtimeCtx{};

        u32 __cdecl Bridge_GetActiveWorldId()
        {
            return g_runtimeCtx.ActiveWorldId;
        }

        u8 __cdecl Bridge_NetGetObjectState(
            u32 worldId,
            u64 netIdPacked,
            u64* outSceneEntityPacked,
            u64* outOwnerPlayerId,
            u8* outIsServer,
            u8* outIsClient,
            u8* outIsOwner)
        {
            if (!g_runtimeCtx.NetworkCallbacks.GetObjectState)
            {
                return 0u;
            }
            return g_runtimeCtx.NetworkCallbacks.GetObjectState(
                worldId,
                netIdPacked,
                outSceneEntityPacked,
                outOwnerPlayerId,
                outIsServer,
                outIsClient,
                outIsOwner) ? 1u : 0u;
        }

        u8 __cdecl Bridge_NetResolveEntityNetworkId(
            u32 worldId,
            u64 sceneEntityPacked,
            u64* outNetIdPacked)
        {
            if (!g_runtimeCtx.NetworkCallbacks.ResolveEntityNetworkId)
            {
                return 0u;
            }
            return g_runtimeCtx.NetworkCallbacks.ResolveEntityNetworkId(
                worldId,
                sceneEntityPacked,
                outNetIdPacked) ? 1u : 0u;
        }

        u8 __cdecl Bridge_NetSendManagedRpc(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            u32 methodHash,
            u8 isServerRpc,
            u8 flags,
            const u8* argsBytes,
            u32 argsLen)
        {
            if (!g_runtimeCtx.NetworkCallbacks.SendManagedRpc)
            {
                return 0u;
            }
            const std::span<const u8> span = (argsBytes && argsLen > 0)
                ? std::span<const u8>(argsBytes, argsLen)
                : std::span<const u8>{};
            return g_runtimeCtx.NetworkCallbacks.SendManagedRpc(
                worldId,
                netIdPacked,
                behaviourHash,
                methodHash,
                isServerRpc,
                flags,
                span) ? 1u : 0u;
        }

        u8 __cdecl Bridge_NetSyncManagedProperties(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            const u8* propsBytes,
            u32 propsLen)
        {
            if (!g_runtimeCtx.NetworkCallbacks.SyncManagedProperties || !propsBytes || propsLen == 0)
            {
                return 0u;
            }
            return g_runtimeCtx.NetworkCallbacks.SyncManagedProperties(
                worldId,
                netIdPacked,
                behaviourHash,
                std::span<const u8>(propsBytes, propsLen)) ? 1u : 0u;
        }

        void __cdecl Bridge_LogInfo(const char* category, const char* message)
        {
            Core::Log::Info(category ? category : "C#.Managed", message ? message : "");
        }

        void __cdecl Bridge_LogFatalException(const char* exceptionType, const char* message, const char* stackTrace)
        {
            Core::Diagnostics::CrashHandler::ReportManagedException(exceptionType, message, stackTrace);
        }

        u8 __cdecl Bridge_IsDebugMode()
        {
            return Core::Diagnostics::IsDebugMode() ? 1u : 0u;
        }

        void __cdecl Bridge_GetComponentArenaView(u8 poolKind, Scene::NativeComponentArenaView* outView)
        {
            if (g_runtimeCtx.ActiveScene && outView)
            {
                *outView = g_runtimeCtx.ActiveScene->GetNativeArenaView(static_cast<Scene::ComponentPoolKind>(poolKind));
            }
        }

        bool __cdecl Bridge_AddLinearImpulse(u64 packedEntityId, const Math::Vec3* impulse)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !impulse)
            {
                return false;
            }
            return g_runtimeCtx.Physics->AddLinearImpulse(
                *g_runtimeCtx.ActiveScene,
                Scene::EntityId::Unpack(packedEntityId),
                *impulse);
        }

        bool __cdecl Bridge_SetLinearVelocity(u64 packedEntityId, const Math::Vec3* velocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !velocity)
            {
                return false;
            }
            return g_runtimeCtx.Physics->SetLinearVelocity(
                *g_runtimeCtx.ActiveScene,
                Scene::EntityId::Unpack(packedEntityId),
                *velocity);
        }

        bool __cdecl Bridge_Raycast(const Math::Vec3* origin, const Math::Vec3* direction, f32 maxDistance, Math::Vec3* outHitPoint)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !direction || !outHitPoint)
            {
                return false;
            }
            Physics::RaycastHit hit{};
            if (g_runtimeCtx.Physics->Raycast(*g_runtimeCtx.ActiveScene, *origin, *direction, maxDistance, hit))
            {
                *outHitPoint = hit.Point;
                return true;
            }
            return false;
        }

        bool __cdecl Bridge_PlaySpatialSound3D(const char* soundKey, const Math::Vec3* position, f32 volume, f32 pitch)
        {
            if (!g_runtimeCtx.Audio || !soundKey || !position)
            {
                return false;
            }
            return g_runtimeCtx.Audio->PlayOneShot3D(soundKey, *position, volume, pitch);
        }

        void __cdecl Bridge_SetVSync(bool enabled)
        {
            Core::EngineSettings::Global().SetVSync(enabled);
        }

        void __cdecl Bridge_SetGameUIProperty(const char* propertyKey, f32 value, const char* stringValue)
        {
            if (g_runtimeCtx.GameUI && propertyKey)
            {
                g_runtimeCtx.GameUI->SetDataProperty(propertyKey, value, stringValue ? stringValue : "");
            }
        }

        void __cdecl Bridge_TriggerGameUIEvent(const char* eventName, i32 eventPayload)
        {
            if (g_runtimeCtx.GameUI && eventName)
            {
                g_runtimeCtx.GameUI->TriggerEvent(eventName, eventPayload);
            }
        }

        // Phase 24: Scene Management Bridge
        u64 __cdecl Bridge_FindEntityByName(const char* name)
        {
            if (!g_runtimeCtx.ActiveScene || !name)
            {
                return Scene::EntityId::Invalid().Pack();
            }
            Scene::EntityId ent = g_runtimeCtx.ActiveScene->FindEntityByName(name);
            if (!ent.IsValid() && (_stricmp(name, "MainCamera") == 0 || _stricmp(name, "Camera") == 0))
            {
                ent = g_runtimeCtx.ActiveScene->FindEntityByName("Camera");
                if (!ent.IsValid()) ent = g_runtimeCtx.ActiveScene->FindEntityByName("MainCamera");
            }
            return ent.Pack();
        }

        u64 __cdecl Bridge_CreateEntity(const char* name)
        {
            if (!g_runtimeCtx.ActiveScene)
            {
                return Scene::EntityId::Invalid().Pack();
            }
            return g_runtimeCtx.ActiveScene->CreateEntity(name ? name : "Entity").Pack();
        }

        bool __cdecl Bridge_DestroyEntity(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene)
            {
                return false;
            }
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent))
            {
                return false;
            }
            g_runtimeCtx.ActiveScene->DestroyEntity(ent);
            return true;
        }

        bool __cdecl Bridge_IsEntityAlive(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene)
            {
                return false;
            }
            return g_runtimeCtx.ActiveScene->IsAlive(Scene::EntityId::Unpack(packedEntityId));
        }

        bool __cdecl Bridge_SetEntityParent(u64 childPackedId, u64 parentPackedId)
        {
            if (!g_runtimeCtx.ActiveScene)
            {
                return false;
            }
            g_runtimeCtx.ActiveScene->SetParent(
                Scene::EntityId::Unpack(childPackedId),
                Scene::EntityId::Unpack(parentPackedId));
            return true;
        }

        // Phase 24: Animation Bridge
        bool __cdecl Bridge_PlayAnimation(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            if (!anim) return false;
            anim->Play();
            return true;
        }

        bool __cdecl Bridge_PauseAnimation(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            if (!anim) return false;
            anim->Pause();
            return true;
        }

        bool __cdecl Bridge_StopAnimation(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            if (!anim) return false;
            anim->Stop();
            return true;
        }

        std::string PathToUtf8(const std::filesystem::path& path, bool generic = false)
        {
            const std::u8string u8 = generic ? path.generic_u8string() : path.u8string();
            return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
        }

        std::filesystem::path PathFromUtf8(std::string_view utf8)
        {
            return std::filesystem::path(std::u8string_view(
                reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
        }

        static std::filesystem::path ResolveAnimationClipPath(std::string_view clipPath)
        {
            std::error_code ec;
            if (g_runtimeCtx.Vfs != nullptr)
            {
                if (clipPath.find("://") != std::string_view::npos)
                {
                    if (auto vfsResolved = g_runtimeCtx.Vfs->ResolveOrStreamToCache(clipPath))
                    {
                        return *vfsResolved;
                    }
                }

                std::string_view trimmed = clipPath;
                if (trimmed.starts_with("Assets/") || trimmed.starts_with("Assets\\"))
                {
                    trimmed = trimmed.substr(7);
                }

                const std::string assetsUri = "assets://" + std::string(trimmed);
                if (auto vfsAssets = g_runtimeCtx.Vfs->ResolveOrStreamToCache(assetsUri))
                {
                    return *vfsAssets;
                }

                const std::string projectUri = "project://" + std::string(clipPath);
                if (auto vfsProject = g_runtimeCtx.Vfs->ResolveOrStreamToCache(projectUri))
                {
                    return *vfsProject;
                }
            }

            std::filesystem::path resolvedPath = PathFromUtf8(clipPath);
            if (std::filesystem::exists(resolvedPath, ec) && !ec)
            {
                return resolvedPath;
            }

#if defined(_WIN32)
            wchar_t exeBuf[MAX_PATH]{};
            const DWORD exeLen = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
            if (exeLen > 0 && exeLen < MAX_PATH)
            {
                const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
                const std::filesystem::path relResolved = exeDir / PathFromUtf8(clipPath);
                if (std::filesystem::exists(relResolved, ec) && !ec)
                {
                    return relResolved;
                }
            }
#endif

            return resolvedPath;
        }

        u32 __cdecl Bridge_ResolveVirtualPath(const char* virtualPath, char* outBuffer, u32 bufferSize)
        {
            if (!virtualPath || !outBuffer || bufferSize == 0)
            {
                return 0;
            }

            const std::filesystem::path resolved = ResolveAnimationClipPath(virtualPath);
            const std::string utf8 = PathToUtf8(resolved, true);
            const u32 copyLen = static_cast<u32>(std::min<usize>(utf8.size(), static_cast<usize>(bufferSize - 1)));
            std::memcpy(outBuffer, utf8.data(), copyLen);
            outBuffer[copyLen] = '\0';
            return copyLen;
        }

        bool PrepareAnimatorWithClip(
            u64 packedEntityId,
            const char* clipPath,
            const char* callerName,
            Scene::AnimatorComponent*& outAnim,
            std::shared_ptr<Asset::AnimationClip>& outClip)
        {
            outAnim = nullptr;
            outClip.reset();
            if (!g_runtimeCtx.ActiveScene || !clipPath) return false;

            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            if (!anim)
            {
                if (const auto* mr = g_runtimeCtx.ActiveScene->MeshRenderers().TryGet(ent))
                {
                    static std::unordered_map<std::string, Asset::ImportedMeshData> s_skeletonCache;
                    std::filesystem::path modelPath = ResolveAnimationClipPath("Assets/Models/Remy.fbx");
                    if (mr->MeshAssetId != 0)
                    {
                        std::error_code scanEc;
                        const std::filesystem::path modelsDir = ResolveAnimationClipPath("Assets/Models");
                        if (std::filesystem::exists(modelsDir, scanEc) && std::filesystem::is_directory(modelsDir, scanEc))
                        {
                            for (const auto& entry : std::filesystem::directory_iterator(modelsDir, scanEc))
                            {
                                if (!entry.is_regular_file(scanEc)) continue;
                                const std::string fname = PathToUtf8(entry.path().filename());
                                const std::string vfsCandidate = "assets://Models/" + fname;
                                if (Core::StringId::FromString(vfsCandidate).Value() == mr->MeshAssetId)
                                {
                                    modelPath = entry.path();
                                    break;
                                }
                            }
                        }
                    }

                    const std::string skelKey = PathToUtf8(modelPath.lexically_normal());
                    auto skelIt = s_skeletonCache.find(skelKey);
                    if (skelIt == s_skeletonCache.end())
                    {
                        std::error_code ec;
                        if (std::filesystem::exists(modelPath, ec))
                        {
                            auto meshImport = Asset::ModelImporter::ImportFromFile(modelPath);
                            if (meshImport && !meshImport.Value().SkeletonHierarchy.empty())
                            {
                                skelIt = s_skeletonCache.emplace(skelKey, std::move(meshImport.Value())).first;
                            }
                            else
                            {
                                Core::Log::Error("ScriptEngine", std::string(callerName) + ": failed to load skeleton from '" +
                                    skelKey + "': " + (meshImport ? "No SkeletonHierarchy" : meshImport.Error()));
                            }
                        }
                    }

                    if (skelIt != s_skeletonCache.end())
                    {
                        Scene::AnimatorComponent newAnim{};
                        newAnim.Evaluator.Initialize(skelIt->second, nullptr);

                        const std::filesystem::path remyRefPath = ResolveAnimationClipPath("Assets/Models/Remy.fbx");
                        const std::string remyKey = PathToUtf8(remyRefPath.lexically_normal());
                        if (remyKey != skelKey)
                        {
                            auto remyIt = s_skeletonCache.find(remyKey);
                            if (remyIt == s_skeletonCache.end())
                            {
                                std::error_code remyEc;
                                if (std::filesystem::exists(remyRefPath, remyEc))
                                {
                                    auto remyImport = Asset::ModelImporter::ImportFromFile(remyRefPath);
                                    if (remyImport && !remyImport.Value().SkeletonHierarchy.empty())
                                    {
                                        remyIt = s_skeletonCache.emplace(remyKey, std::move(remyImport.Value())).first;
                                    }
                                }
                            }
                            if (remyIt != s_skeletonCache.end())
                            {
                                newAnim.Evaluator.SetRetargetSourceSkeleton(remyIt->second);
                            }
                        }

                        newAnim.IsPlaying = true;
                        newAnim.PlaybackSpeed = 1.0f;
                        newAnim.NeedsGpuUpload = true;
                        g_runtimeCtx.ActiveScene->Animators().EmplaceOrReplace(ent, newAnim);
                        anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
                        Core::Log::Info("ScriptEngine", std::string(callerName) + ": auto-attached AnimatorComponent to entity " +
                            std::to_string(ent.Index));
                    }
                }
            }
            if (!anim) return false;

            std::error_code ec;
            const std::filesystem::path resolvedPath = ResolveAnimationClipPath(clipPath);
            if (!std::filesystem::exists(resolvedPath, ec) || ec)
            {
                Core::Log::Error("ScriptEngine", std::string(callerName) + ": clip not found at path: " + std::string(clipPath));
                return false;
            }

            static std::unordered_map<std::string, std::shared_ptr<Asset::AnimationClip>> s_clipCache;
            const std::string cacheKey = PathToUtf8(resolvedPath.lexically_normal());
            auto cacheIt = s_clipCache.find(cacheKey);
            if (cacheIt != s_clipCache.end())
            {
                outClip = cacheIt->second;
            }
            else
            {
                auto res = Asset::ModelImporter::ImportAnimationsFromFile(resolvedPath);
                if (!res || res.Value().empty())
                {
                    Core::Log::Error("ScriptEngine", std::string(callerName) + ": failed to import clip from '" +
                        cacheKey + "': " + (res ? "Empty clips" : res.Error()));
                    return false;
                }

                outClip = std::make_shared<Asset::AnimationClip>(std::move(res.Value()[0]));
                s_clipCache[cacheKey] = outClip;
            }

            outAnim = anim;
            return true;
        }

        bool __cdecl Bridge_SetAnimationClip(u64 packedEntityId, const char* clipPath)
        {
            Scene::AnimatorComponent* anim = nullptr;
            std::shared_ptr<Asset::AnimationClip> clip;
            if (!PrepareAnimatorWithClip(packedEntityId, clipPath, "SetAnimationClip", anim, clip))
            {
                return false;
            }

            anim->PreviousClip.reset();
            anim->CurrentClip = std::move(clip);
            anim->Evaluator.SetAnimation(anim->CurrentClip.get());
            anim->Evaluator.FillGPUBuffer(anim->GpuBoneBuffer);
            anim->NeedsGpuUpload = true;
            anim->Play();

            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            Core::Log::Info("ScriptEngine", "SetAnimationClip: loaded clip '" + anim->CurrentClip->Name +
                "' (" + std::to_string(anim->CurrentClip->GetDurationSeconds()) + "s) on entity " +
                std::to_string(ent.Index));
            return true;
        }

        bool __cdecl Bridge_CrossFadeAnimationClip(u64 packedEntityId, const char* clipPath, f32 blendDurationSeconds)
        {
            Scene::AnimatorComponent* anim = nullptr;
            std::shared_ptr<Asset::AnimationClip> clip;
            if (!PrepareAnimatorWithClip(packedEntityId, clipPath, "CrossFadeAnimationClip", anim, clip))
            {
                return false;
            }

            if (anim->CurrentClip == clip && clip != nullptr)
            {
                return true;
            }

            anim->PreviousClip = anim->CurrentClip;
            anim->CurrentClip = std::move(clip);
            anim->Evaluator.CrossFade(anim->CurrentClip.get(), blendDurationSeconds);
            anim->Evaluator.FillGPUBuffer(anim->GpuBoneBuffer);
            anim->NeedsGpuUpload = true;
            anim->Play();
            return true;
        }

        bool __cdecl Bridge_IsAnimationBlending(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            const auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            return anim ? anim->Evaluator.IsBlending() : false;
        }

        bool __cdecl Bridge_SetAnimationSpeed(u64 packedEntityId, f32 speed)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            if (!anim) return false;
            anim->PlaybackSpeed = speed;
            anim->Evaluator.SetPlaybackSpeed(speed);
            return true;
        }

        bool __cdecl Bridge_IsAnimationPlaying(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            const auto* anim = g_runtimeCtx.ActiveScene->Animators().TryGet(ent);
            return anim ? anim->IsPlaying : false;
        }

        // Phase 24: Input Bridge (SDL3 InputState)
        bool __cdecl Bridge_IsKeyDown(u32 scancode)
        {
            return g_runtimeCtx.Input ? g_runtimeCtx.Input->IsKeyDown(scancode) : false;
        }

        bool __cdecl Bridge_WasKeyPressed(u32 scancode)
        {
            return g_runtimeCtx.Input ? g_runtimeCtx.Input->WasKeyPressedThisFrame(scancode) : false;
        }

        bool __cdecl Bridge_WasKeyReleased(u32 scancode)
        {
            return g_runtimeCtx.Input ? g_runtimeCtx.Input->WasKeyReleasedThisFrame(scancode) : false;
        }

        bool __cdecl Bridge_IsMouseButtonDown(u8 button)
        {
            if (!g_runtimeCtx.Input || button >= static_cast<u8>(Platform::MouseButton::Count)) return false;
            return g_runtimeCtx.Input->IsMouseButtonDown(static_cast<Platform::MouseButton>(button));
        }

        bool __cdecl Bridge_WasMouseButtonPressed(u8 button)
        {
            if (!g_runtimeCtx.Input || button >= static_cast<u8>(Platform::MouseButton::Count)) return false;
            return g_runtimeCtx.Input->WasMouseButtonPressedThisFrame(static_cast<Platform::MouseButton>(button));
        }

        void __cdecl Bridge_GetMousePosition(f32* outX, f32* outY)
        {
            if (outX) *outX = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseX() : 0.0f;
            if (outY) *outY = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseY() : 0.0f;
        }

        void __cdecl Bridge_GetMouseDelta(f32* outDx, f32* outDy)
        {
            if (outDx) *outDx = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseDeltaX() : 0.0f;
            if (outDy) *outDy = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseDeltaY() : 0.0f;
        }

        void __cdecl Bridge_GetMouseWheelDelta(f32* outWheelX, f32* outWheelY)
        {
            if (outWheelX) *outWheelX = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseWheelX() : 0.0f;
            if (outWheelY) *outWheelY = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseWheelY() : 0.0f;
        }

        bool __cdecl Bridge_WasMouseButtonReleased(u8 button)
        {
            if (!g_runtimeCtx.Input || button >= static_cast<u8>(Platform::MouseButton::Count)) return false;
            return g_runtimeCtx.Input->WasMouseButtonReleasedThisFrame(static_cast<Platform::MouseButton>(button));
        }

        void __cdecl Bridge_GetMouseDelta3D(f32* outDx, f32* outDy, f32* outDz)
        {
            if (outDx) *outDx = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseDeltaX() : 0.0f;
            if (outDy) *outDy = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseDeltaY() : 0.0f;
            if (outDz) *outDz = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseDeltaZ() : 0.0f;
        }

        void __cdecl Bridge_GetMousePosition3D(f32* outX, f32* outY, f32* outZ)
        {
            if (outX) *outX = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseX() : 0.0f;
            if (outY) *outY = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseY() : 0.0f;
            if (outZ) *outZ = g_runtimeCtx.Input ? g_runtimeCtx.Input->MouseZ() : 0.0f;
        }

        void __cdecl Bridge_SetMouseCaptured(bool captured)
        {
            if (g_runtimeCtx.Input)
            {
                g_runtimeCtx.Input->SetMouseCaptured(captured);
            }
        }

        bool __cdecl Bridge_IsMouseCaptured()
        {
            return g_runtimeCtx.Input ? g_runtimeCtx.Input->IsMouseCaptured() : false;
        }

        void __cdecl Bridge_SetCursorVisible(bool visible)
        {
            if (g_runtimeCtx.Input)
            {
                g_runtimeCtx.Input->SetCursorVisible(visible);
            }
        }

        bool __cdecl Bridge_IsCursorVisible()
        {
            return g_runtimeCtx.Input ? g_runtimeCtx.Input->IsCursorVisible() : true;
        }

        // Complete C# Entity, Component & Scene Bindings (v3)
        u32 __cdecl Bridge_GetEntityName(u64 packedEntityId, char* outBuffer, u32 bufferSize)
        {
            if (!g_runtimeCtx.ActiveScene)
            {
                if (outBuffer && bufferSize > 0) outBuffer[0] = '\0';
                return 0u;
            }
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            const std::string_view name = g_runtimeCtx.ActiveScene->GetEntityName(ent);
            if (!outBuffer || bufferSize == 0u)
            {
                return static_cast<u32>(name.size());
            }
            const u32 copyLen = static_cast<u32>(std::min<usize>(name.size(), static_cast<usize>(bufferSize - 1u)));
            if (copyLen > 0u)
            {
                std::memcpy(outBuffer, name.data(), copyLen);
            }
            outBuffer[copyLen] = '\0';
            return copyLen;
        }

        bool __cdecl Bridge_SetEntityName(u64 packedEntityId, const char* name)
        {
            if (!g_runtimeCtx.ActiveScene || !name) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;
            g_runtimeCtx.ActiveScene->SetEntityName(ent, name);
            return true;
        }

        u64 __cdecl Bridge_GetEntityParent(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return Scene::EntityId::Invalid().Pack();
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            return g_runtimeCtx.ActiveScene->GetParent(ent).Pack();
        }

        u32 __cdecl Bridge_GetAliveEntityCount()
        {
            return g_runtimeCtx.ActiveScene ? g_runtimeCtx.ActiveScene->GetAliveEntityCount() : 0u;
        }

        bool __cdecl Bridge_HasComponent(u64 packedEntityId, u8 poolKind)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;

            switch (static_cast<Scene::ComponentPoolKind>(poolKind))
            {
            case Scene::ComponentPoolKind::Transform:    return g_runtimeCtx.ActiveScene->Transforms().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::Hierarchy:    return g_runtimeCtx.ActiveScene->Hierarchies().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::MeshRenderer: return g_runtimeCtx.ActiveScene->MeshRenderers().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::Light:        return g_runtimeCtx.ActiveScene->Lights().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::Script:       return g_runtimeCtx.ActiveScene->Scripts().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::RigidBody:    return g_runtimeCtx.ActiveScene->RigidBodies().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::AudioSource:  return g_runtimeCtx.ActiveScene->AudioSources().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::Camera:              return g_runtimeCtx.ActiveScene->Cameras().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::Animator:            return g_runtimeCtx.ActiveScene->Animators().TryGet(ent) != nullptr;
            case Scene::ComponentPoolKind::CharacterController: return g_runtimeCtx.ActiveScene->CharacterControllers().TryGet(ent) != nullptr;
            default:                                            return false;
            }
        }

        bool __cdecl Bridge_AddComponent(u64 packedEntityId, u8 poolKind)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;

            auto ensureComp = [&]<typename T>(Scene::SparseSet<T>& pool) -> bool {
                if (pool.TryGet(ent) == nullptr)
                {
                    pool.EmplaceOrReplace(ent, T{});
                }
                return true;
            };

            switch (static_cast<Scene::ComponentPoolKind>(poolKind))
            {
            case Scene::ComponentPoolKind::Transform:           return ensureComp(g_runtimeCtx.ActiveScene->Transforms());
            case Scene::ComponentPoolKind::Hierarchy:           return ensureComp(g_runtimeCtx.ActiveScene->Hierarchies());
            case Scene::ComponentPoolKind::MeshRenderer:        return ensureComp(g_runtimeCtx.ActiveScene->MeshRenderers());
            case Scene::ComponentPoolKind::Light:               return ensureComp(g_runtimeCtx.ActiveScene->Lights());
            case Scene::ComponentPoolKind::Script:              return ensureComp(g_runtimeCtx.ActiveScene->Scripts());
            case Scene::ComponentPoolKind::RigidBody:           return ensureComp(g_runtimeCtx.ActiveScene->RigidBodies());
            case Scene::ComponentPoolKind::AudioSource:         return ensureComp(g_runtimeCtx.ActiveScene->AudioSources());
            case Scene::ComponentPoolKind::Camera:              return ensureComp(g_runtimeCtx.ActiveScene->Cameras());
            case Scene::ComponentPoolKind::Animator:            return ensureComp(g_runtimeCtx.ActiveScene->Animators());
            case Scene::ComponentPoolKind::CharacterController: return ensureComp(g_runtimeCtx.ActiveScene->CharacterControllers());
            default:                                            return false;
            }
        }

        bool __cdecl Bridge_RemoveComponent(u64 packedEntityId, u8 poolKind)
        {
            if (!g_runtimeCtx.ActiveScene) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;

            switch (static_cast<Scene::ComponentPoolKind>(poolKind))
            {
            case Scene::ComponentPoolKind::MeshRenderer: g_runtimeCtx.ActiveScene->MeshRenderers().Remove(ent); return true;
            case Scene::ComponentPoolKind::Light:        g_runtimeCtx.ActiveScene->Lights().Remove(ent); return true;
            case Scene::ComponentPoolKind::Script:       g_runtimeCtx.ActiveScene->Scripts().Remove(ent); return true;
            case Scene::ComponentPoolKind::RigidBody:
                if (g_runtimeCtx.Physics)
                {
                    g_runtimeCtx.Physics->DestroyBody(ent);
                }
                g_runtimeCtx.ActiveScene->RigidBodies().Remove(ent);
                return true;
            case Scene::ComponentPoolKind::CharacterController:
                if (g_runtimeCtx.Physics)
                {
                    g_runtimeCtx.Physics->DestroyCharacter(ent);
                }
                g_runtimeCtx.ActiveScene->CharacterControllers().Remove(ent);
                return true;
            case Scene::ComponentPoolKind::AudioSource:  g_runtimeCtx.ActiveScene->AudioSources().Remove(ent); return true;
            case Scene::ComponentPoolKind::Camera:       g_runtimeCtx.ActiveScene->Cameras().Remove(ent); return true;
            case Scene::ComponentPoolKind::Animator:     g_runtimeCtx.ActiveScene->Animators().Remove(ent); return true;
            case Scene::ComponentPoolKind::Transform:
            case Scene::ComponentPoolKind::Hierarchy:
            default:
                return false;
            }
        }

        bool __cdecl Bridge_LoadScene(const char* filePath)
        {
            if (!filePath || filePath[0] == '\0') return false;
            if (g_runtimeCtx.SceneHandler)
            {
                return g_runtimeCtx.SceneHandler(filePath, false);
            }
            if (!g_runtimeCtx.ActiveScene) return false;
            return g_runtimeCtx.ActiveScene->LoadFromJsonFile(ResolveAnimationClipPath(filePath));
        }

        bool __cdecl Bridge_SaveScene(const char* filePath)
        {
            if (!g_runtimeCtx.ActiveScene || !filePath || filePath[0] == '\0') return false;
            return g_runtimeCtx.ActiveScene->SaveToJsonFile(filePath);
        }

        void __cdecl Bridge_ClearScene()
        {
            if (g_runtimeCtx.SceneHandler)
            {
                g_runtimeCtx.SceneHandler({}, true);
                return;
            }
            if (g_runtimeCtx.ActiveScene)
            {
                g_runtimeCtx.ActiveScene->Clear();
            }
        }

        // Phase 4: Production Jolt Physics Bridge (v4)
        Physics::ForceMode ToForceMode(u8 mode) noexcept
        {
            switch (mode)
            {
            case 1: return Physics::ForceMode::Impulse;
            case 2: return Physics::ForceMode::VelocityChange;
            case 3: return Physics::ForceMode::Acceleration;
            case 0:
            default: return Physics::ForceMode::Force;
            }
        }

        Physics::QueryFilter ToQueryFilter(const NativeQueryFilter* filter) noexcept
        {
            Physics::QueryFilter out{};
            if (!filter) return out;
            out.LayerMask = filter->LayerMask;
            out.IncludeTriggers = (filter->IncludeTriggers != 0u);
            out.IgnoreEntity = Scene::EntityId::Unpack(filter->IgnoreEntityPacked);
            out.IgnoreStatic = (filter->IgnoreStatic != 0u);
            out.IgnoreKinematic = (filter->IgnoreKinematic != 0u);
            out.IgnoreDynamic = (filter->IgnoreDynamic != 0u);
            return out;
        }

        void ToNativeRaycastHit(const Physics::RaycastHit& src, NativeRaycastHit& dst) noexcept
        {
            dst.HitEntityPacked = src.HitEntity.Pack();
            dst.Point = src.Point;
            dst.Normal = src.Normal;
            dst.Distance = src.Distance;
            dst.Shape = static_cast<u8>(src.Shape);
            dst.Reserved0 = 0;
            dst.Reserved1 = 0;
            dst.Reserved2 = 0;
        }

        void ToNativeOverlapHit(const Physics::OverlapHit& src, NativeOverlapHit& dst) noexcept
        {
            dst.EntityPacked = src.Entity.Pack();
            dst.ContactPoint = src.ContactPoint;
            dst.PenetrationAxis = src.PenetrationAxis;
            dst.PenetrationDepth = src.PenetrationDepth;
            dst.Shape = static_cast<u8>(src.Shape);
            dst.Reserved0 = 0;
            dst.Reserved1 = 0;
            dst.Reserved2 = 0;
        }

        bool __cdecl Bridge_SyncRigidBody(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->SyncEntityBody(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId));
        }

        void __cdecl Bridge_MarkPhysicsTransformDirty(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (auto* rb = g_runtimeCtx.ActiveScene->RigidBodies().TryGet(ent))
            {
                rb->DirtyFlags = static_cast<u8>(rb->DirtyFlags | static_cast<u8>(Scene::RigidBodyDirtyFlags::Transform));
            }
            if (auto* cc = g_runtimeCtx.ActiveScene->CharacterControllers().TryGet(ent))
            {
                cc->DirtyFlags = static_cast<u8>(cc->DirtyFlags | static_cast<u8>(Scene::CharacterControllerDirtyFlags::Transform));
            }
        }

        bool __cdecl Bridge_AddForce(u64 packedEntityId, const Math::Vec3* force, u8 mode)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !force) return false;
            return g_runtimeCtx.Physics->AddForce(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *force, ToForceMode(mode));
        }

        bool __cdecl Bridge_AddForceAtPosition(u64 packedEntityId, const Math::Vec3* force, const Math::Vec3* position, u8 mode)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !force || !position) return false;
            return g_runtimeCtx.Physics->AddForceAtPosition(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *force, *position, ToForceMode(mode));
        }

        bool __cdecl Bridge_AddTorque(u64 packedEntityId, const Math::Vec3* torque, u8 mode)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !torque) return false;
            return g_runtimeCtx.Physics->AddTorque(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *torque, ToForceMode(mode));
        }

        bool __cdecl Bridge_GetLinearVelocity(u64 packedEntityId, Math::Vec3* outVelocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !outVelocity) return false;
            return g_runtimeCtx.Physics->GetLinearVelocity(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *outVelocity);
        }

        bool __cdecl Bridge_SetAngularVelocity(u64 packedEntityId, const Math::Vec3* angularVelocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !angularVelocity) return false;
            return g_runtimeCtx.Physics->SetAngularVelocity(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *angularVelocity);
        }

        bool __cdecl Bridge_GetAngularVelocity(u64 packedEntityId, Math::Vec3* outAngularVelocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !outAngularVelocity) return false;
            return g_runtimeCtx.Physics->GetAngularVelocity(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *outAngularVelocity);
        }

        bool __cdecl Bridge_WakeUpBody(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->WakeUp(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId));
        }

        bool __cdecl Bridge_SleepBody(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->Sleep(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId));
        }

        bool __cdecl Bridge_IsBodySleeping(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->IsSleeping(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId));
        }

        bool __cdecl Bridge_TeleportBody(u64 packedEntityId, const Math::Vec3* position, const Math::Quat* rotation)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !position || !rotation) return false;
            return g_runtimeCtx.Physics->TeleportBody(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *position, *rotation);
        }

        void __cdecl Bridge_SetGravity(const Math::Vec3* gravity)
        {
            if (g_runtimeCtx.Physics && gravity) g_runtimeCtx.Physics->SetGravity(*gravity);
        }

        void __cdecl Bridge_GetGravity(Math::Vec3* outGravity)
        {
            if (g_runtimeCtx.Physics && outGravity) *outGravity = g_runtimeCtx.Physics->GetGravity();
        }

        void __cdecl Bridge_SetLayerCollision(u32 layerA, u32 layerB, bool shouldCollide)
        {
            if (g_runtimeCtx.Physics) g_runtimeCtx.Physics->SetLayerCollision(layerA, layerB, shouldCollide);
        }

        bool __cdecl Bridge_GetLayerCollision(u32 layerA, u32 layerB)
        {
            return g_runtimeCtx.Physics ? g_runtimeCtx.Physics->GetLayerCollision(layerA, layerB) : true;
        }

        bool __cdecl Bridge_RaycastEx(
            const Math::Vec3* origin,
            const Math::Vec3* direction,
            f32 maxDistance,
            const NativeQueryFilter* filter,
            NativeRaycastHit* outHit)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !direction || !outHit) return false;
            Physics::RaycastHit hit{};
            if (!g_runtimeCtx.Physics->Raycast(*g_runtimeCtx.ActiveScene, *origin, *direction, maxDistance, hit, ToQueryFilter(filter)))
            {
                return false;
            }
            ToNativeRaycastHit(hit, *outHit);
            return true;
        }

        u32 __cdecl Bridge_RaycastAllEx(
            const Math::Vec3* origin,
            const Math::Vec3* direction,
            f32 maxDistance,
            const NativeQueryFilter* filter,
            NativeRaycastHit* outHits,
            u32 maxHits)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !direction) return 0u;
            std::vector<Physics::RaycastHit> hits;
            g_runtimeCtx.Physics->RaycastAll(*g_runtimeCtx.ActiveScene, *origin, *direction, maxDistance, hits, ToQueryFilter(filter));
            if (!outHits || maxHits == 0u) return static_cast<u32>(hits.size());
            const u32 copyCount = std::min(maxHits, static_cast<u32>(hits.size()));
            for (u32 i = 0; i < copyCount; ++i)
            {
                ToNativeRaycastHit(hits[i], outHits[i]);
            }
            return copyCount;
        }

        bool __cdecl Bridge_SphereCastEx(
            const Math::Vec3* origin,
            f32 radius,
            const Math::Vec3* direction,
            f32 maxDistance,
            const NativeQueryFilter* filter,
            NativeRaycastHit* outHit)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !direction || !outHit) return false;
            Physics::RaycastHit hit{};
            if (!g_runtimeCtx.Physics->SphereCast(*g_runtimeCtx.ActiveScene, *origin, radius, *direction, maxDistance, hit, ToQueryFilter(filter)))
            {
                return false;
            }
            ToNativeRaycastHit(hit, *outHit);
            return true;
        }

        bool __cdecl Bridge_BoxCastEx(
            const Math::Vec3* origin,
            const Math::Vec3* halfExtents,
            const Math::Quat* orientation,
            const Math::Vec3* direction,
            f32 maxDistance,
            const NativeQueryFilter* filter,
            NativeRaycastHit* outHit)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !halfExtents || !orientation || !direction || !outHit) return false;
            Physics::RaycastHit hit{};
            if (!g_runtimeCtx.Physics->BoxCast(*g_runtimeCtx.ActiveScene, *origin, *halfExtents, *orientation, *direction, maxDistance, hit, ToQueryFilter(filter)))
            {
                return false;
            }
            ToNativeRaycastHit(hit, *outHit);
            return true;
        }

        bool __cdecl Bridge_CapsuleCastEx(
            const Math::Vec3* origin,
            f32 radius,
            f32 halfHeight,
            const Math::Quat* orientation,
            const Math::Vec3* direction,
            f32 maxDistance,
            const NativeQueryFilter* filter,
            NativeRaycastHit* outHit)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !origin || !orientation || !direction || !outHit) return false;
            Physics::RaycastHit hit{};
            if (!g_runtimeCtx.Physics->CapsuleCast(*g_runtimeCtx.ActiveScene, *origin, radius, halfHeight, *orientation, *direction, maxDistance, hit, ToQueryFilter(filter)))
            {
                return false;
            }
            ToNativeRaycastHit(hit, *outHit);
            return true;
        }

        u32 __cdecl Bridge_OverlapSphereEx(
            const Math::Vec3* center,
            f32 radius,
            const NativeQueryFilter* filter,
            NativeOverlapHit* outHits,
            u32 maxHits)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !center) return 0u;
            std::vector<Physics::OverlapHit> hits;
            g_runtimeCtx.Physics->OverlapSphere(*g_runtimeCtx.ActiveScene, *center, radius, hits, ToQueryFilter(filter));
            if (!outHits || maxHits == 0u) return static_cast<u32>(hits.size());
            const u32 copyCount = std::min(maxHits, static_cast<u32>(hits.size()));
            for (u32 i = 0; i < copyCount; ++i)
            {
                ToNativeOverlapHit(hits[i], outHits[i]);
            }
            return copyCount;
        }

        u32 __cdecl Bridge_OverlapBoxEx(
            const Math::Vec3* center,
            const Math::Vec3* halfExtents,
            const Math::Quat* orientation,
            const NativeQueryFilter* filter,
            NativeOverlapHit* outHits,
            u32 maxHits)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !center || !halfExtents || !orientation) return 0u;
            std::vector<Physics::OverlapHit> hits;
            g_runtimeCtx.Physics->OverlapBox(*g_runtimeCtx.ActiveScene, *center, *halfExtents, *orientation, hits, ToQueryFilter(filter));
            if (!outHits || maxHits == 0u) return static_cast<u32>(hits.size());
            const u32 copyCount = std::min(maxHits, static_cast<u32>(hits.size()));
            for (u32 i = 0; i < copyCount; ++i)
            {
                ToNativeOverlapHit(hits[i], outHits[i]);
            }
            return copyCount;
        }

        u32 __cdecl Bridge_OverlapCapsuleEx(
            const Math::Vec3* center,
            f32 radius,
            f32 halfHeight,
            const Math::Quat* orientation,
            const NativeQueryFilter* filter,
            NativeOverlapHit* outHits,
            u32 maxHits)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !center || !orientation) return 0u;
            std::vector<Physics::OverlapHit> hits;
            g_runtimeCtx.Physics->OverlapCapsule(*g_runtimeCtx.ActiveScene, *center, radius, halfHeight, *orientation, hits, ToQueryFilter(filter));
            if (!outHits || maxHits == 0u) return static_cast<u32>(hits.size());
            const u32 copyCount = std::min(maxHits, static_cast<u32>(hits.size()));
            for (u32 i = 0; i < copyCount; ++i)
            {
                ToNativeOverlapHit(hits[i], outHits[i]);
            }
            return copyCount;
        }

        // Phase 5: CharacterController Bridge
        bool __cdecl Bridge_SyncCharacterController(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->SyncCharacterController(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId));
        }

        bool __cdecl Bridge_MoveCharacter(u64 packedEntityId, const Math::Vec3* displacement)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !displacement) return false;
            return g_runtimeCtx.Physics->MoveCharacter(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *displacement);
        }

        bool __cdecl Bridge_JumpCharacter(u64 packedEntityId, f32 jumpSpeed)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->JumpCharacter(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), jumpSpeed);
        }

        bool __cdecl Bridge_SetCharacterVelocity(u64 packedEntityId, const Math::Vec3* velocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !velocity) return false;
            return g_runtimeCtx.Physics->SetCharacterVelocity(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *velocity);
        }

        bool __cdecl Bridge_GetCharacterVelocity(u64 packedEntityId, Math::Vec3* outVelocity)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !outVelocity) return false;
            return g_runtimeCtx.Physics->GetCharacterVelocity(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *outVelocity);
        }

        bool __cdecl Bridge_GetCharacterState(u64 packedEntityId, NativeCharacterState* outState)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !outState) return false;
            Physics::CharacterControllerState st{};
            if (!g_runtimeCtx.Physics->GetCharacterState(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), st))
            {
                return false;
            }
            outState->GroundEntityPacked = st.GroundEntity.Pack();
            outState->Velocity = st.Velocity;
            outState->Speed = Math::Length(st.Velocity);
            outState->GroundNormal = st.GroundNormal;
            outState->GroundState = static_cast<u8>(st.GroundState);
            outState->IsGrounded = st.IsGrounded ? 1u : 0u;
            outState->IsSliding = st.IsSliding ? 1u : 0u;
            outState->Reserved = 0u;
            outState->GroundVelocity = st.GroundVelocity;
            outState->ReservedPad = 0u;
            return true;
        }

        bool __cdecl Bridge_TeleportCharacter(u64 packedEntityId, const Math::Vec3* position)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !position) return false;
            return g_runtimeCtx.Physics->TeleportCharacter(*g_runtimeCtx.ActiveScene, Scene::EntityId::Unpack(packedEntityId), *position);
        }

        Physics::JointDesc FromNativeJointDesc(const NativeJointDesc& d) noexcept
        {
            Physics::JointDesc out{};
            out.Type = static_cast<Physics::JointType>(d.Type);
            out.EntityA = Scene::EntityId::Unpack(d.EntityAPacked);
            out.EntityB = Scene::EntityId::Unpack(d.EntityBPacked);
            out.AnchorA = d.AnchorA;
            out.AnchorB = d.AnchorB;
            out.Axis = d.Axis;
            out.AutoDetectAnchors = (d.AutoDetectAnchors != 0u);
            out.MinDistance = d.MinDistance;
            out.MaxDistance = d.MaxDistance;
            out.SpringFrequencyHz = d.SpringFrequencyHz;
            out.SpringDampingRatio = d.SpringDampingRatio;
            out.MinLimit = d.MinLimit;
            out.MaxLimit = d.MaxLimit;
            out.MaxFriction = d.MaxFriction;
            out.BreakForce = d.BreakForce;
            out.BreakTorque = d.BreakTorque;
            out.Enabled = (d.Enabled != 0u);
            return out;
        }

        void ToNativeJointDesc(const Physics::JointDesc& src, NativeJointDesc& dst) noexcept
        {
            dst.EntityAPacked = src.EntityA.Pack();
            dst.EntityBPacked = src.EntityB.Pack();
            dst.AnchorA = src.AnchorA;
            dst.MinDistance = src.MinDistance;
            dst.AnchorB = src.AnchorB;
            dst.MaxDistance = src.MaxDistance;
            dst.Axis = src.Axis;
            dst.SpringFrequencyHz = src.SpringFrequencyHz;
            dst.SpringDampingRatio = src.SpringDampingRatio;
            dst.MinLimit = src.MinLimit;
            dst.MaxLimit = src.MaxLimit;
            dst.MaxFriction = src.MaxFriction;
            dst.BreakForce = src.BreakForce;
            dst.BreakTorque = src.BreakTorque;
            dst.Type = static_cast<u8>(src.Type);
            dst.AutoDetectAnchors = src.AutoDetectAnchors ? 1u : 0u;
            dst.Enabled = src.Enabled ? 1u : 0u;
            dst.Reserved0 = 0u;
            dst.Reserved1 = 0u;
        }

        u64 __cdecl Bridge_CreateJoint(const NativeJointDesc* desc)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !desc) return 0ull;
            const Physics::JointHandle handle = g_runtimeCtx.Physics->CreateJoint(*g_runtimeCtx.ActiveScene, FromNativeJointDesc(*desc));
            return handle.IsValid() ? handle.Pack() : 0ull;
        }

        bool __cdecl Bridge_UpdateJoint(u64 packedHandle, const NativeJointDesc* desc)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics || !desc) return false;
            return g_runtimeCtx.Physics->UpdateJoint(*g_runtimeCtx.ActiveScene, Physics::JointHandle::Unpack(packedHandle), FromNativeJointDesc(*desc));
        }

        bool __cdecl Bridge_DestroyJoint(u64 packedHandle)
        {
            if (!g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->DestroyJoint(Physics::JointHandle::Unpack(packedHandle));
        }

        bool __cdecl Bridge_IsJointValid(u64 packedHandle)
        {
            if (!g_runtimeCtx.Physics) return false;
            return g_runtimeCtx.Physics->IsJointValid(Physics::JointHandle::Unpack(packedHandle));
        }

        bool __cdecl Bridge_GetJointDesc(u64 packedHandle, NativeJointDesc* outDesc)
        {
            if (!g_runtimeCtx.Physics || !outDesc) return false;
            Physics::JointDesc desc{};
            if (!g_runtimeCtx.Physics->GetJointDesc(Physics::JointHandle::Unpack(packedHandle), desc))
            {
                return false;
            }
            ToNativeJointDesc(desc, *outDesc);
            return true;
        }

        bool __cdecl Bridge_GetJointState(u64 packedHandle, f32* outPositionOrAngle, f32* outForce, f32* outTorque)
        {
            if (!g_runtimeCtx.Physics || !outPositionOrAngle || !outForce || !outTorque) return false;
            return g_runtimeCtx.Physics->GetJointState(
                Physics::JointHandle::Unpack(packedHandle),
                *outPositionOrAngle,
                *outForce,
                *outTorque);
        }

        u32 __cdecl Bridge_GetEntityJointCount(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return 0u;
            const auto span = g_runtimeCtx.ActiveScene->GetEntityJoints(Scene::EntityId::Unpack(packedEntityId));
            return static_cast<u32>(span.size());
        }

        u64 __cdecl Bridge_GetEntityJointHandle(u64 packedEntityId, u32 index)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return 0ull;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            auto span = g_runtimeCtx.ActiveScene->GetEntityJoints(ent);
            if (index >= static_cast<u32>(span.size()))
            {
                return 0ull;
            }
            if (span[index].RuntimeJointHandle == 0ull && !span[index].Broken)
            {
                g_runtimeCtx.Physics->SyncSceneJoints(*g_runtimeCtx.ActiveScene);
                span = g_runtimeCtx.ActiveScene->GetEntityJoints(ent);
                if (index >= static_cast<u32>(span.size()))
                {
                    return 0ull;
                }
            }
            return span[index].RuntimeJointHandle;
        }

        bool __cdecl Bridge_SetEntityCompoundColliders(u64 packedEntityId, const NativeColliderShapeDesc* shapes, u32 count)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;
            if (count > 0u && !shapes) return false;

            auto safeFinite = [](f32 v, f32 fallback) noexcept {
                return std::isfinite(v) ? v : fallback;
            };

            std::vector<Scene::CompoundColliderEntry> entries;
            entries.reserve(count);
            for (u32 i = 0u; i < count; ++i)
            {
                const auto& s = shapes[i];
                if (s.Shape > 2u) return false;
                const Math::Vec3 he{
                    std::max(0.005f, safeFinite(s.HalfExtents.x, 0.5f)),
                    std::max(0.005f, safeFinite(s.HalfExtents.y, 0.5f)),
                    std::max(0.005f, safeFinite(s.HalfExtents.z, 0.5f))
                };
                const Math::Vec3 off{
                    safeFinite(s.LocalOffset.x, 0.0f),
                    safeFinite(s.LocalOffset.y, 0.0f),
                    safeFinite(s.LocalOffset.z, 0.0f)
                };
                const Math::Quat rotRaw{
                    safeFinite(s.LocalRotation.x, 0.0f),
                    safeFinite(s.LocalRotation.y, 0.0f),
                    safeFinite(s.LocalRotation.z, 0.0f),
                    safeFinite(s.LocalRotation.w, 1.0f)
                };
                const f32 qLen = rotRaw.x * rotRaw.x + rotRaw.y * rotRaw.y + rotRaw.z * rotRaw.z + rotRaw.w * rotRaw.w;
                const Math::Quat safeRot = (qLen > 1e-8f) ? Math::NormalizeQuat(rotRaw) : Math::Quat::Identity();

                entries.push_back(Scene::CompoundColliderEntry{
                    .Shape = static_cast<Scene::ColliderShapeType>(s.Shape),
                    .HalfExtents = he,
                    .LocalOffset = off,
                    .LocalRotation = safeRot,
                    .CollisionLayer = s.CollisionLayer,
                    .CollisionMask = s.CollisionMask,
                    .IsTrigger = s.IsTrigger
                });
            }
            g_runtimeCtx.ActiveScene->SetCompoundColliders(ent, entries);
            g_runtimeCtx.Physics->SyncEntityCompoundColliders(*g_runtimeCtx.ActiveScene, ent);
            return g_runtimeCtx.Physics->SyncEntityBody(*g_runtimeCtx.ActiveScene, ent);
        }

        u32 __cdecl Bridge_GetEntityCompoundColliderCount(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene) return 0u;
            return static_cast<u32>(g_runtimeCtx.ActiveScene->GetCompoundColliders(Scene::EntityId::Unpack(packedEntityId)).size());
        }

        bool __cdecl Bridge_GetEntityCompoundCollider(u64 packedEntityId, u32 index, NativeColliderShapeDesc* outShape)
        {
            if (!g_runtimeCtx.ActiveScene || !outShape) return false;
            const auto span = g_runtimeCtx.ActiveScene->GetCompoundColliders(Scene::EntityId::Unpack(packedEntityId));
            if (index >= static_cast<u32>(span.size())) return false;
            const auto& e = span[index];
            outShape->HalfExtents = e.HalfExtents;
            outShape->CollisionLayer = e.CollisionLayer;
            outShape->LocalOffset = e.LocalOffset;
            outShape->CollisionMask = e.CollisionMask;
            outShape->LocalRotation = e.LocalRotation;
            outShape->Shape = static_cast<u8>(e.Shape);
            outShape->IsTrigger = e.IsTrigger;
            outShape->Reserved0 = 0u;
            outShape->Reserved1 = 0u;
            outShape->Reserved2 = 0u;
            return true;
        }

        bool __cdecl Bridge_ClearEntityCompoundColliders(u64 packedEntityId)
        {
            if (!g_runtimeCtx.ActiveScene || !g_runtimeCtx.Physics) return false;
            const Scene::EntityId ent = Scene::EntityId::Unpack(packedEntityId);
            if (!g_runtimeCtx.ActiveScene->IsAlive(ent)) return false;
            g_runtimeCtx.ActiveScene->ClearCompoundColliders(ent);
            g_runtimeCtx.Physics->ClearEntityCompoundColliders(ent);
            return g_runtimeCtx.Physics->SyncEntityBody(*g_runtimeCtx.ActiveScene, ent);
        }
    }

#if MORBI_HAS_DOTNET_HOST
        using FnInitialize = int (__cdecl*)(const NativeEngineFunctionTable* table, int tableSize);
        using FnLoadCollectibleAssembly = int (__cdecl*)(const char* utf8AssemblyPath);
        using FnUnloadCollectibleAssembly = int (__cdecl*)();
        using FnTickScripts = int (__cdecl*)(f32 deltaTime);
        using FnTickFixedScripts = int (__cdecl*)(f32 fixedDeltaTime);
        using FnDispatchPhysicsEvents = int (__cdecl*)(const NativeCollisionEvent* events, int count);
        using FnNotifyEntityDestroyed = int (__cdecl*)(u64 packedEntityId);
        using FnNotifySceneCleared = int (__cdecl*)();
        using FnCompileAndLoadScripts = int (__cdecl*)(
            const char* scriptsDirUtf8,
            const char* outputDllUtf8,
            int loadAfterCompile,
            int releaseOptimize,
            char* errorBufUtf8,
            int errorBufSize,
            int* outFileCount);
        using FnNotifyNetworkSpawn = int (__cdecl*)(
            u32 worldId,
            u64 netIdPacked,
            u64 sceneEntityPacked,
            u64 ownerPlayerId,
            u8 isServer,
            u8 isClient,
            u8 isOwner);
        using FnNotifyNetworkDespawn = int (__cdecl*)(u32 worldId, u64 netIdPacked);
        using FnNotifyNetworkOwnershipChanged = int (__cdecl*)(
            u32 worldId,
            u64 netIdPacked,
            u64 prevOwner,
            u64 newOwner,
            u8 isOwner);
        using FnNotifyNetworkWorldChanged = int (__cdecl*)(u32 prevWorldId, u32 newWorldId, u64 netIdPacked);
        using FnNotifyWorldDestroyed = int (__cdecl*)(u32 worldId);
        using FnDispatchManagedPropertySync = int (__cdecl*)(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            const u8* propsBytes,
            u32 propsLen);
        using FnDispatchManagedRpc = int (__cdecl*)(
            u32 worldId,
            u64 netIdPacked,
            u32 behaviourHash,
            u32 methodHash,
            u64 senderPlayerId,
            u8 isServerSide,
            const u8* argsBytes,
            u32 argsLen);
        using FnSyncManagedNetworkProperties = int (__cdecl*)(u32 worldId);

        // .NET CoreCLR (hostfxr) can only be initialized once per OS process and cannot be unloaded/restarted.
        // Keep the primary hostfxr context and UnmanagedCallersOnly entry points cached process-wide so
        // ScriptEngine::Shutdown() + Initialize() cycles safely rebind to the active CLR without 0x80008081.
        struct ProcessDotNetHost
        {
            std::mutex Mutex;
            bool Ready = false;
            HMODULE HostFxrLib = nullptr;
            hostfxr_handle HostContext = nullptr;
            hostfxr_initialize_for_runtime_config_fn InitForRuntimeConfig = nullptr;
            hostfxr_get_runtime_delegate_fn GetRuntimeDelegate = nullptr;
            hostfxr_close_fn CloseHostFxr = nullptr;
            load_assembly_and_get_function_pointer_fn LoadAssemblyAndGetFnPtr = nullptr;
            std::filesystem::path RuntimeConfigPath;

            FnInitialize ManagedInit = nullptr;
            FnLoadCollectibleAssembly ManagedLoadCollectible = nullptr;
            FnUnloadCollectibleAssembly ManagedUnloadCollectible = nullptr;
            FnTickScripts ManagedTick = nullptr;
            FnTickFixedScripts ManagedFixedTick = nullptr;
            FnDispatchPhysicsEvents ManagedDispatchPhysicsEvents = nullptr;
            FnNotifyEntityDestroyed ManagedNotifyEntityDestroyed = nullptr;
            FnNotifySceneCleared ManagedNotifySceneCleared = nullptr;
            FnCompileAndLoadScripts ManagedCompileAndLoad = nullptr;
            FnNotifyNetworkSpawn ManagedNotifyNetworkSpawn = nullptr;
            FnNotifyNetworkDespawn ManagedNotifyNetworkDespawn = nullptr;
            FnNotifyNetworkOwnershipChanged ManagedNotifyNetworkOwnershipChanged = nullptr;
            FnNotifyNetworkWorldChanged ManagedNotifyNetworkWorldChanged = nullptr;
            FnNotifyWorldDestroyed ManagedNotifyWorldDestroyed = nullptr;
            FnDispatchManagedPropertySync ManagedDispatchPropertySync = nullptr;
            FnDispatchManagedRpc ManagedDispatchRpc = nullptr;
            FnSyncManagedNetworkProperties ManagedSyncNetworkProperties = nullptr;
        };

        ProcessDotNetHost& GetProcessDotNetHost()
        {
            static ProcessDotNetHost s_host;
            return s_host;
        }
#endif

    struct ScriptEngine::Impl
    {
        bool Initialized = false;
        u32 HotReloadGeneration = 0;
        NativeEngineFunctionTable FunctionTable{};

#if MORBI_HAS_DOTNET_HOST
        FnInitialize ManagedInit = nullptr;
        FnLoadCollectibleAssembly ManagedLoadCollectible = nullptr;
        FnUnloadCollectibleAssembly ManagedUnloadCollectible = nullptr;
        FnTickScripts ManagedTick = nullptr;
        FnTickFixedScripts ManagedFixedTick = nullptr;
        FnDispatchPhysicsEvents ManagedDispatchPhysicsEvents = nullptr;
        FnNotifyEntityDestroyed ManagedNotifyEntityDestroyed = nullptr;
        FnNotifySceneCleared ManagedNotifySceneCleared = nullptr;
        FnCompileAndLoadScripts ManagedCompileAndLoad = nullptr;
        FnNotifyNetworkSpawn ManagedNotifyNetworkSpawn = nullptr;
        FnNotifyNetworkDespawn ManagedNotifyNetworkDespawn = nullptr;
        FnNotifyNetworkOwnershipChanged ManagedNotifyNetworkOwnershipChanged = nullptr;
        FnNotifyNetworkWorldChanged ManagedNotifyNetworkWorldChanged = nullptr;
        FnNotifyWorldDestroyed ManagedNotifyWorldDestroyed = nullptr;
        FnDispatchManagedPropertySync ManagedDispatchPropertySync = nullptr;
        FnDispatchManagedRpc ManagedDispatchRpc = nullptr;
        FnSyncManagedNetworkProperties ManagedSyncNetworkProperties = nullptr;
        std::vector<Physics::CollisionPairEvent> CollisionSnapshotScratch;
        std::vector<NativeCollisionEvent> EventScratchBuffer;
#endif
    };

    ScriptEngine::ScriptEngine()
        : m_impl(std::make_unique<Impl>())
    {
        m_impl->FunctionTable.StructSize = sizeof(NativeEngineFunctionTable);
        m_impl->FunctionTable.ApiVersion = 7;
        m_impl->FunctionTable.LogInfo = &Bridge_LogInfo;
        m_impl->FunctionTable.GetComponentArenaView = &Bridge_GetComponentArenaView;
        m_impl->FunctionTable.AddLinearImpulse = &Bridge_AddLinearImpulse;
        m_impl->FunctionTable.SetLinearVelocity = &Bridge_SetLinearVelocity;
        m_impl->FunctionTable.Raycast = &Bridge_Raycast;
        m_impl->FunctionTable.PlaySpatialSound3D = &Bridge_PlaySpatialSound3D;
        m_impl->FunctionTable.SetVSync = &Bridge_SetVSync;
        m_impl->FunctionTable.SetGameUIProperty = &Bridge_SetGameUIProperty;
        m_impl->FunctionTable.TriggerGameUIEvent = &Bridge_TriggerGameUIEvent;

        // Phase 24 additions
        m_impl->FunctionTable.FindEntityByName = &Bridge_FindEntityByName;
        m_impl->FunctionTable.CreateEntity = &Bridge_CreateEntity;
        m_impl->FunctionTable.DestroyEntity = &Bridge_DestroyEntity;
        m_impl->FunctionTable.IsEntityAlive = &Bridge_IsEntityAlive;
        m_impl->FunctionTable.SetEntityParent = &Bridge_SetEntityParent;

        m_impl->FunctionTable.PlayAnimation = &Bridge_PlayAnimation;
        m_impl->FunctionTable.PauseAnimation = &Bridge_PauseAnimation;
        m_impl->FunctionTable.StopAnimation = &Bridge_StopAnimation;
        m_impl->FunctionTable.SetAnimationClip = &Bridge_SetAnimationClip;
        m_impl->FunctionTable.SetAnimationSpeed = &Bridge_SetAnimationSpeed;
        m_impl->FunctionTable.IsAnimationPlaying = &Bridge_IsAnimationPlaying;

        m_impl->FunctionTable.IsKeyDown = &Bridge_IsKeyDown;
        m_impl->FunctionTable.WasKeyPressed = &Bridge_WasKeyPressed;
        m_impl->FunctionTable.WasKeyReleased = &Bridge_WasKeyReleased;
        m_impl->FunctionTable.IsMouseButtonDown = &Bridge_IsMouseButtonDown;
        m_impl->FunctionTable.WasMouseButtonPressed = &Bridge_WasMouseButtonPressed;
        m_impl->FunctionTable.GetMousePosition = &Bridge_GetMousePosition;
        m_impl->FunctionTable.GetMouseDelta = &Bridge_GetMouseDelta;
        m_impl->FunctionTable.GetMouseWheelDelta = &Bridge_GetMouseWheelDelta;
        m_impl->FunctionTable.WasMouseButtonReleased = &Bridge_WasMouseButtonReleased;
        m_impl->FunctionTable.GetMouseDelta3D = &Bridge_GetMouseDelta3D;
        m_impl->FunctionTable.GetMousePosition3D = &Bridge_GetMousePosition3D;
        m_impl->FunctionTable.SetMouseCaptured = &Bridge_SetMouseCaptured;
        m_impl->FunctionTable.IsMouseCaptured = &Bridge_IsMouseCaptured;
        m_impl->FunctionTable.SetCursorVisible = &Bridge_SetCursorVisible;
        m_impl->FunctionTable.IsCursorVisible = &Bridge_IsCursorVisible;

        // Complete C# Entity, Component & Scene Bindings (v3)
        m_impl->FunctionTable.GetEntityName = &Bridge_GetEntityName;
        m_impl->FunctionTable.SetEntityName = &Bridge_SetEntityName;
        m_impl->FunctionTable.GetEntityParent = &Bridge_GetEntityParent;
        m_impl->FunctionTable.GetAliveEntityCount = &Bridge_GetAliveEntityCount;
        m_impl->FunctionTable.HasComponent = &Bridge_HasComponent;
        m_impl->FunctionTable.AddComponent = &Bridge_AddComponent;
        m_impl->FunctionTable.RemoveComponent = &Bridge_RemoveComponent;
        m_impl->FunctionTable.LoadScene = &Bridge_LoadScene;
        m_impl->FunctionTable.SaveScene = &Bridge_SaveScene;
        m_impl->FunctionTable.ClearScene = &Bridge_ClearScene;

        m_impl->FunctionTable.CrossFadeAnimationClip = &Bridge_CrossFadeAnimationClip;
        m_impl->FunctionTable.IsAnimationBlending = &Bridge_IsAnimationBlending;
        m_impl->FunctionTable.ResolveVirtualPath = &Bridge_ResolveVirtualPath;

        // Production Jolt Physics Bridge (v4)
        m_impl->FunctionTable.SyncRigidBody = &Bridge_SyncRigidBody;
        m_impl->FunctionTable.MarkPhysicsTransformDirty = &Bridge_MarkPhysicsTransformDirty;
        m_impl->FunctionTable.AddForce = &Bridge_AddForce;
        m_impl->FunctionTable.AddForceAtPosition = &Bridge_AddForceAtPosition;
        m_impl->FunctionTable.AddTorque = &Bridge_AddTorque;
        m_impl->FunctionTable.GetLinearVelocity = &Bridge_GetLinearVelocity;
        m_impl->FunctionTable.SetAngularVelocity = &Bridge_SetAngularVelocity;
        m_impl->FunctionTable.GetAngularVelocity = &Bridge_GetAngularVelocity;
        m_impl->FunctionTable.WakeUpBody = &Bridge_WakeUpBody;
        m_impl->FunctionTable.SleepBody = &Bridge_SleepBody;
        m_impl->FunctionTable.IsBodySleeping = &Bridge_IsBodySleeping;
        m_impl->FunctionTable.TeleportBody = &Bridge_TeleportBody;
        m_impl->FunctionTable.SetGravity = &Bridge_SetGravity;
        m_impl->FunctionTable.GetGravity = &Bridge_GetGravity;
        m_impl->FunctionTable.SetLayerCollision = &Bridge_SetLayerCollision;
        m_impl->FunctionTable.GetLayerCollision = &Bridge_GetLayerCollision;
        m_impl->FunctionTable.RaycastEx = &Bridge_RaycastEx;
        m_impl->FunctionTable.RaycastAllEx = &Bridge_RaycastAllEx;
        m_impl->FunctionTable.SphereCastEx = &Bridge_SphereCastEx;
        m_impl->FunctionTable.BoxCastEx = &Bridge_BoxCastEx;
        m_impl->FunctionTable.CapsuleCastEx = &Bridge_CapsuleCastEx;
        m_impl->FunctionTable.OverlapSphereEx = &Bridge_OverlapSphereEx;
        m_impl->FunctionTable.OverlapBoxEx = &Bridge_OverlapBoxEx;
        m_impl->FunctionTable.OverlapCapsuleEx = &Bridge_OverlapCapsuleEx;

        // Character Physics Bridge (Phase 5)
        m_impl->FunctionTable.SyncCharacterController = &Bridge_SyncCharacterController;
        m_impl->FunctionTable.MoveCharacter = &Bridge_MoveCharacter;
        m_impl->FunctionTable.JumpCharacter = &Bridge_JumpCharacter;
        m_impl->FunctionTable.SetCharacterVelocity = &Bridge_SetCharacterVelocity;
        m_impl->FunctionTable.GetCharacterVelocity = &Bridge_GetCharacterVelocity;
        m_impl->FunctionTable.GetCharacterState = &Bridge_GetCharacterState;
        m_impl->FunctionTable.TeleportCharacter = &Bridge_TeleportCharacter;

        // Joints & Compound Collider Bridge (ApiVersion 5)
        m_impl->FunctionTable.CreateJoint = &Bridge_CreateJoint;
        m_impl->FunctionTable.UpdateJoint = &Bridge_UpdateJoint;
        m_impl->FunctionTable.DestroyJoint = &Bridge_DestroyJoint;
        m_impl->FunctionTable.IsJointValid = &Bridge_IsJointValid;
        m_impl->FunctionTable.GetJointDesc = &Bridge_GetJointDesc;
        m_impl->FunctionTable.GetJointState = &Bridge_GetJointState;
        m_impl->FunctionTable.GetEntityJointCount = &Bridge_GetEntityJointCount;
        m_impl->FunctionTable.GetEntityJointHandle = &Bridge_GetEntityJointHandle;
        m_impl->FunctionTable.SetEntityCompoundColliders = &Bridge_SetEntityCompoundColliders;
        m_impl->FunctionTable.GetEntityCompoundColliderCount = &Bridge_GetEntityCompoundColliderCount;
        m_impl->FunctionTable.GetEntityCompoundCollider = &Bridge_GetEntityCompoundCollider;
        m_impl->FunctionTable.ClearEntityCompoundColliders = &Bridge_ClearEntityCompoundColliders;

        // Diagnostics & Unhandled Managed Exception Bridge (ApiVersion 6)
        m_impl->FunctionTable.LogFatalException = &Bridge_LogFatalException;
        m_impl->FunctionTable.IsDebugMode = &Bridge_IsDebugMode;

        // Multi-World & Gameplay Networking Bridge (ApiVersion 7 / Phase 8)
        m_impl->FunctionTable.GetActiveWorldId = &Bridge_GetActiveWorldId;
        m_impl->FunctionTable.NetGetObjectState = &Bridge_NetGetObjectState;
        m_impl->FunctionTable.NetResolveEntityNetworkId = &Bridge_NetResolveEntityNetworkId;
        m_impl->FunctionTable.NetSendManagedRpc = &Bridge_NetSendManagedRpc;
        m_impl->FunctionTable.NetSyncManagedProperties = &Bridge_NetSyncManagedProperties;
    }

    ScriptEngine::~ScriptEngine()
    {
        Shutdown();
    }

    void ScriptEngine::BindGameUI(UI::GameUIRenderer* renderer)
    {
        g_runtimeCtx.GameUI = renderer;
    }

    void ScriptEngine::BindInput(Platform::InputState* input)
    {
        g_runtimeCtx.Input = input;
    }

    void ScriptEngine::BindSceneHandler(SceneHandlerCallback callback)
    {
        g_runtimeCtx.SceneHandler = std::move(callback);
    }

    void ScriptEngine::BindVFS(IO::PathService* vfs)
    {
        g_runtimeCtx.Vfs = vfs;
    }

    Core::Result<bool, std::string> ScriptEngine::Initialize(
        const ScriptHostDesc& desc,
        Scene::Scene& activeScene,
        Physics::PhysicsWorld& physicsWorld,
        Audio::AudioWorld& audioWorld)
    {
        IO::PathService* preservedVfs = g_runtimeCtx.Vfs;
        Shutdown();

        g_runtimeCtx.ActiveScene = &activeScene;
        g_runtimeCtx.Physics = &physicsWorld;
        g_runtimeCtx.Audio = &audioWorld;
        g_runtimeCtx.Vfs = preservedVfs;

#if !MORBI_HAS_DOTNET_HOST
        (void)desc;
        return Core::Result<bool, std::string>::Failure("MORBI_HAS_DOTNET_HOST is disabled.");
#else
        const std::filesystem::path runtimeConfigPath = desc.ManagedAssemblyDir / (desc.HostAssemblyName + ".runtimeconfig.json");
        const std::filesystem::path dllPath = desc.ManagedAssemblyDir / (desc.HostAssemblyName + ".dll");

        if (!std::filesystem::exists(runtimeConfigPath))
        {
            return Core::Result<bool, std::string>::Failure("Missing runtimeconfig.json: " + runtimeConfigPath.string());
        }
        if (!std::filesystem::exists(dllPath))
        {
            return Core::Result<bool, std::string>::Failure("Missing managed assembly: " + dllPath.string());
        }

        ProcessDotNetHost& host = GetProcessDotNetHost();
        {
            std::lock_guard<std::mutex> lock(host.Mutex);
            if (!host.Ready)
            {
                wchar_t hostfxrPath[MAX_PATH]{};
                size_t bufferSize = MAX_PATH;
                std::filesystem::path portableDotnetRoot;
                {
                    wchar_t exeBuf[MAX_PATH]{};
                    const DWORD exeLen = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
                    if (exeLen > 0 && exeLen < MAX_PATH)
                    {
                        const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
                        std::error_code ec;
                        const std::filesystem::path cand1 = exeDir / "Tools" / "dotnet";
                        const std::filesystem::path cand2 = exeDir / "dotnet";
                        if (std::filesystem::exists(cand1 / "host" / "fxr", ec))
                        {
                            portableDotnetRoot = cand1;
                        }
                        else if (std::filesystem::exists(cand2 / "host" / "fxr", ec))
                        {
                            portableDotnetRoot = cand2;
                        }
                    }
                }

                bool gotHostfxr = false;
                if (!portableDotnetRoot.empty())
                {
                    const std::wstring wideRoot = portableDotnetRoot.wstring();
                    SetEnvironmentVariableW(L"DOTNET_ROOT", wideRoot.c_str());
                    get_hostfxr_parameters params{};
                    params.size = sizeof(get_hostfxr_parameters);
                    params.dotnet_root = wideRoot.c_str();
                    if (get_hostfxr_path(hostfxrPath, &bufferSize, &params) == 0)
                    {
                        gotHostfxr = true;
                        Core::Log::Info("Script", "Using portable .NET CoreCLR runtime at: " + portableDotnetRoot.string());
                    }
                    else
                    {
                        bufferSize = MAX_PATH;
                    }
                }

                if (!gotHostfxr && get_hostfxr_path(hostfxrPath, &bufferSize, nullptr) != 0)
                {
                    return Core::Result<bool, std::string>::Failure("get_hostfxr_path failed (neither portable Tools/dotnet nor system .NET runtime found).");
                }

                host.HostFxrLib = LoadLibraryW(hostfxrPath);
                if (!host.HostFxrLib)
                {
                    return Core::Result<bool, std::string>::Failure("LoadLibraryW(hostfxr.dll) failed.");
                }

                host.InitForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
                    GetProcAddress(host.HostFxrLib, "hostfxr_initialize_for_runtime_config"));
                host.GetRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
                    GetProcAddress(host.HostFxrLib, "hostfxr_get_runtime_delegate"));
                host.CloseHostFxr = reinterpret_cast<hostfxr_close_fn>(
                    GetProcAddress(host.HostFxrLib, "hostfxr_close"));

                if (!host.InitForRuntimeConfig || !host.GetRuntimeDelegate || !host.CloseHostFxr)
                {
                    return Core::Result<bool, std::string>::Failure("hostfxr exports incomplete.");
                }

                const std::wstring wideConfig = runtimeConfigPath.wstring();
                hostfxr_initialize_parameters initParams{};
                std::wstring wideDotnetRoot;
                if (!portableDotnetRoot.empty())
                {
                    wideDotnetRoot = portableDotnetRoot.wstring();
                    initParams.size = sizeof(hostfxr_initialize_parameters);
                    initParams.host_path = wideConfig.c_str();
                    initParams.dotnet_root = wideDotnetRoot.c_str();
                }
                const int rc = host.InitForRuntimeConfig(
                    wideConfig.c_str(),
                    portableDotnetRoot.empty() ? nullptr : &initParams,
                    &host.HostContext);
                if ((rc != 0 && rc != 1) || host.HostContext == nullptr)
                {
                    return Core::Result<bool, std::string>::Failure("hostfxr_initialize_for_runtime_config failed with code " + std::to_string(rc));
                }

                void* loadAssemblyFn = nullptr;
                if (host.GetRuntimeDelegate(host.HostContext, hdt_load_assembly_and_get_function_pointer, &loadAssemblyFn) != 0 || !loadAssemblyFn)
                {
                    return Core::Result<bool, std::string>::Failure("hdt_load_assembly_and_get_function_pointer failed.");
                }
                host.LoadAssemblyAndGetFnPtr = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyFn);

                const std::wstring wideDll = dllPath.wstring();
                const wchar_t* typeName = L"Morbi.Engine.ManagedHost, Morbi.Engine";

                auto bindUnmanagedEntry = [&](const wchar_t* methodName, void** outFn) -> bool {
                    return host.LoadAssemblyAndGetFnPtr(
                        wideDll.c_str(),
                        typeName,
                        methodName,
                        UNMANAGEDCALLERSONLY_METHOD,
                        nullptr,
                        outFn) == 0 && (*outFn != nullptr);
                };

                if (!bindUnmanagedEntry(L"Initialize", reinterpret_cast<void**>(&host.ManagedInit)) ||
                    !bindUnmanagedEntry(L"LoadCollectibleAssembly", reinterpret_cast<void**>(&host.ManagedLoadCollectible)) ||
                    !bindUnmanagedEntry(L"UnloadCollectibleAssembly", reinterpret_cast<void**>(&host.ManagedUnloadCollectible)) ||
                    !bindUnmanagedEntry(L"TickScripts", reinterpret_cast<void**>(&host.ManagedTick)) ||
                    !bindUnmanagedEntry(L"TickFixedScripts", reinterpret_cast<void**>(&host.ManagedFixedTick)) ||
                    !bindUnmanagedEntry(L"DispatchPhysicsEvents", reinterpret_cast<void**>(&host.ManagedDispatchPhysicsEvents)) ||
                    !bindUnmanagedEntry(L"NotifyEntityDestroyed", reinterpret_cast<void**>(&host.ManagedNotifyEntityDestroyed)) ||
                    !bindUnmanagedEntry(L"NotifySceneCleared", reinterpret_cast<void**>(&host.ManagedNotifySceneCleared)) ||
                    !bindUnmanagedEntry(L"CompileAndLoadScripts", reinterpret_cast<void**>(&host.ManagedCompileAndLoad)) ||
                    !bindUnmanagedEntry(L"NotifyNetworkSpawn", reinterpret_cast<void**>(&host.ManagedNotifyNetworkSpawn)) ||
                    !bindUnmanagedEntry(L"NotifyNetworkDespawn", reinterpret_cast<void**>(&host.ManagedNotifyNetworkDespawn)) ||
                    !bindUnmanagedEntry(L"NotifyNetworkOwnershipChanged", reinterpret_cast<void**>(&host.ManagedNotifyNetworkOwnershipChanged)) ||
                    !bindUnmanagedEntry(L"NotifyNetworkWorldChanged", reinterpret_cast<void**>(&host.ManagedNotifyNetworkWorldChanged)) ||
                    !bindUnmanagedEntry(L"NotifyWorldDestroyed", reinterpret_cast<void**>(&host.ManagedNotifyWorldDestroyed)) ||
                    !bindUnmanagedEntry(L"DispatchManagedPropertySync", reinterpret_cast<void**>(&host.ManagedDispatchPropertySync)) ||
                    !bindUnmanagedEntry(L"DispatchManagedRpc", reinterpret_cast<void**>(&host.ManagedDispatchRpc)) ||
                    !bindUnmanagedEntry(L"SyncManagedNetworkProperties", reinterpret_cast<void**>(&host.ManagedSyncNetworkProperties)))
                {
                    return Core::Result<bool, std::string>::Failure("Failed to bind UnmanagedCallersOnly entry points on Morbi.Engine.ManagedHost.");
                }

                host.RuntimeConfigPath = runtimeConfigPath;
                host.Ready = true;
            }
        }

        m_impl->ManagedInit = host.ManagedInit;
        m_impl->ManagedLoadCollectible = host.ManagedLoadCollectible;
        m_impl->ManagedUnloadCollectible = host.ManagedUnloadCollectible;
        m_impl->ManagedTick = host.ManagedTick;
        m_impl->ManagedFixedTick = host.ManagedFixedTick;
        m_impl->ManagedDispatchPhysicsEvents = host.ManagedDispatchPhysicsEvents;
        m_impl->ManagedNotifyEntityDestroyed = host.ManagedNotifyEntityDestroyed;
        m_impl->ManagedNotifySceneCleared = host.ManagedNotifySceneCleared;
        m_impl->ManagedCompileAndLoad = host.ManagedCompileAndLoad;
        m_impl->ManagedNotifyNetworkSpawn = host.ManagedNotifyNetworkSpawn;
        m_impl->ManagedNotifyNetworkDespawn = host.ManagedNotifyNetworkDespawn;
        m_impl->ManagedNotifyNetworkOwnershipChanged = host.ManagedNotifyNetworkOwnershipChanged;
        m_impl->ManagedNotifyNetworkWorldChanged = host.ManagedNotifyNetworkWorldChanged;
        m_impl->ManagedNotifyWorldDestroyed = host.ManagedNotifyWorldDestroyed;
        m_impl->ManagedDispatchPropertySync = host.ManagedDispatchPropertySync;
        m_impl->ManagedDispatchRpc = host.ManagedDispatchRpc;
        m_impl->ManagedSyncNetworkProperties = host.ManagedSyncNetworkProperties;

        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.Initialize(TableSize=" +
            std::to_string(sizeof(NativeEngineFunctionTable)) + ", ApiVersion=" +
            std::to_string(m_impl->FunctionTable.ApiVersion) + ")");
        const int initRes = m_impl->ManagedInit(&m_impl->FunctionTable, static_cast<int>(sizeof(NativeEngineFunctionTable)));
        Core::Diagnostics::CrashHandler::ReassertFilter();
        if (initRes != 0)
        {
            return Core::Result<bool, std::string>::Failure("Morbi.Engine.ManagedHost.Initialize returned error: " + std::to_string(initRes));
        }

        m_impl->Initialized = true;
        m_impl->HotReloadGeneration = 1;
        Core::Log::Info("Script", "Initialized .NET CoreCLR Host (hostfxr) & Zero-Overhead UnmanagedCallersOnly Function Table.");
        return Core::Result<bool, std::string>::Success(true);
#endif
    }

    void ScriptEngine::Shutdown()
    {
#if MORBI_HAS_DOTNET_HOST
        if (m_impl->Initialized)
        {
            if (m_impl->ManagedNotifySceneCleared)
            {
                m_impl->ManagedNotifySceneCleared();
            }
            if (m_impl->ManagedUnloadCollectible)
            {
                m_impl->ManagedUnloadCollectible();
            }
        }
        m_impl->ManagedInit = nullptr;
        m_impl->ManagedLoadCollectible = nullptr;
        m_impl->ManagedUnloadCollectible = nullptr;
        m_impl->ManagedTick = nullptr;
        m_impl->ManagedFixedTick = nullptr;
        m_impl->ManagedDispatchPhysicsEvents = nullptr;
        m_impl->ManagedNotifyEntityDestroyed = nullptr;
        m_impl->ManagedNotifySceneCleared = nullptr;
        m_impl->ManagedCompileAndLoad = nullptr;
        m_impl->ManagedNotifyNetworkSpawn = nullptr;
        m_impl->ManagedNotifyNetworkDespawn = nullptr;
        m_impl->ManagedNotifyNetworkOwnershipChanged = nullptr;
        m_impl->ManagedNotifyNetworkWorldChanged = nullptr;
        m_impl->ManagedNotifyWorldDestroyed = nullptr;
        m_impl->ManagedDispatchPropertySync = nullptr;
        m_impl->ManagedDispatchRpc = nullptr;
        m_impl->ManagedSyncNetworkProperties = nullptr;
#endif
        m_impl->Initialized = false;
        g_runtimeCtx = {};
    }

    Core::Result<bool, std::string> ScriptEngine::LoadOrHotReloadAssembly(const std::filesystem::path& assemblyPath)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)assemblyPath;
        return Core::Result<bool, std::string>::Failure("MORBI_HAS_DOTNET_HOST is disabled.");
#else
        if (!m_impl->Initialized || !m_impl->ManagedLoadCollectible)
        {
            return Core::Result<bool, std::string>::Failure("ScriptEngine is not initialized.");
        }
        const std::string utf8Path = PathToUtf8(assemblyPath, false);
        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.LoadCollectibleAssembly('" + utf8Path + "')");
        const int res = m_impl->ManagedLoadCollectible(utf8Path.c_str());
        if (res != 0)
        {
            return Core::Result<bool, std::string>::Failure("LoadCollectibleAssembly failed with code: " + std::to_string(res));
        }
        ++m_impl->HotReloadGeneration;
        return Core::Result<bool, std::string>::Success(true);
#endif
    }

    ScriptCompileResult ScriptEngine::CompileProjectScripts(
        const std::filesystem::path& scriptsDir,
        const std::filesystem::path& outputDllPath,
        bool loadAfterCompile,
        bool releaseOptimize)
    {
        ScriptCompileResult result{};
#if !MORBI_HAS_DOTNET_HOST
        (void)scriptsDir;
        (void)outputDllPath;
        (void)loadAfterCompile;
        (void)releaseOptimize;
        result.Diagnostics = "MORBI_HAS_DOTNET_HOST is disabled.";
        return result;
#else
        if (!m_impl->Initialized || !m_impl->ManagedCompileAndLoad)
        {
            result.Diagnostics = "ScriptEngine is not initialized.";
            return result;
        }

        const std::string scriptsUtf8 = PathToUtf8(scriptsDir, true);
        const std::string outputUtf8 = PathToUtf8(outputDllPath, true);
        std::string errorBuf(8192, '\0');
        int fileCount = 0;

        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.CompileAndLoadScripts(dir='" + scriptsUtf8 +
            "', out='" + outputUtf8 + "', loadAfter=" + std::to_string(loadAfterCompile ? 1 : 0) + ")");

        const int rc = m_impl->ManagedCompileAndLoad(
            scriptsUtf8.c_str(),
            outputUtf8.c_str(),
            loadAfterCompile ? 1 : 0,
            releaseOptimize ? 1 : 0,
            errorBuf.data(),
            static_cast<int>(errorBuf.size()),
            &fileCount);

        result.CompiledFileCount = fileCount > 0 ? static_cast<u32>(fileCount) : 0u;
        const size_t nullPos = errorBuf.find('\0');
        if (nullPos != std::string::npos)
        {
            errorBuf.resize(nullPos);
        }
        result.Diagnostics = std::move(errorBuf);

        if (rc == 0 || rc == 1)
        {
            result.Success = true;
            if (loadAfterCompile && rc == 0)
            {
                ++m_impl->HotReloadGeneration;
            }
        }
        return result;
#endif
    }

    bool ScriptEngine::UnloadActiveScriptContext()
    {
#if !MORBI_HAS_DOTNET_HOST
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedUnloadCollectible)
        {
            return false;
        }
        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.UnloadCollectibleAssembly()");
        return m_impl->ManagedUnloadCollectible() == 0;
#endif
    }

    bool ScriptEngine::TickManagedScripts(f32 deltaTime)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)deltaTime;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedTick)
        {
            return false;
        }
        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.TickScripts(dt=" + std::to_string(deltaTime) + ")");
        return m_impl->ManagedTick(deltaTime) == 0;
#endif
    }

    bool ScriptEngine::FixedTickManagedScripts(f32 fixedDeltaTime)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)fixedDeltaTime;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedFixedTick)
        {
            return false;
        }
        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.TickFixedScripts(fixedDt=" + std::to_string(fixedDeltaTime) + ")");
        return m_impl->ManagedFixedTick(fixedDeltaTime) == 0;
#endif
    }

    bool ScriptEngine::DispatchPhysicsEvents()
    {
#if !MORBI_HAS_DOTNET_HOST
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedDispatchPhysicsEvents || !g_runtimeCtx.Physics)
        {
            return false;
        }

        g_runtimeCtx.Physics->CopyRecentCollisions(m_impl->CollisionSnapshotScratch);
        const auto& recent = m_impl->CollisionSnapshotScratch;
        if (recent.empty())
        {
            return true;
        }

        m_impl->EventScratchBuffer.clear();
        m_impl->EventScratchBuffer.reserve(recent.size());
        for (const auto& ev : recent)
        {
            NativeCollisionEvent nev{};
            nev.EntityAPacked = ev.EntityA.Pack();
            nev.EntityBPacked = ev.EntityB.Pack();
            nev.ContactPoint = ev.ContactPoint;
            nev.ContactNormal = ev.ContactNormal;
            nev.RelativeVelocity = ev.RelativeVelocity;
            nev.PenetrationDepth = ev.PenetrationDepth;
            nev.ShapeA = static_cast<u8>(ev.ShapeA);
            nev.ShapeB = static_cast<u8>(ev.ShapeB);
            nev.EventType = static_cast<u8>(ev.EventType);
            nev.IsTrigger = ev.IsTrigger ? 1u : 0u;
            m_impl->EventScratchBuffer.push_back(nev);
        }

        MORBI_BREADCRUMB("C# OnUpdate/OnLoad");
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.DispatchPhysicsEvents(count=" +
            std::to_string(m_impl->EventScratchBuffer.size()) + ")");
        return m_impl->ManagedDispatchPhysicsEvents(
            m_impl->EventScratchBuffer.data(),
            static_cast<int>(m_impl->EventScratchBuffer.size())) == 0;
#endif
    }

    bool ScriptEngine::NotifyEntityDestroyed(Scene::EntityId entity)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)entity;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyEntityDestroyed || !entity.IsValid())
        {
            return false;
        }
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.NotifyEntityDestroyed(id=" + std::to_string(entity.Pack()) + ")");
        return m_impl->ManagedNotifyEntityDestroyed(entity.Pack()) == 0;
#endif
    }

    bool ScriptEngine::NotifySceneCleared()
    {
#if !MORBI_HAS_DOTNET_HOST
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifySceneCleared)
        {
            return false;
        }
        MORBI_TRACE("Script.Bridge", "Calling ManagedHost.NotifySceneCleared()");
        return m_impl->ManagedNotifySceneCleared() == 0;
#endif
    }

    bool ScriptEngine::IsInitialized() const noexcept { return m_impl->Initialized; }
    u32 ScriptEngine::GetHotReloadGeneration() const noexcept { return m_impl->HotReloadGeneration; }

    void ScriptEngine::SetActiveWorldContext(
        u32 worldId,
        Scene::Scene* activeScene,
        Physics::PhysicsWorld* physicsWorld,
        Audio::AudioWorld* audioWorld)
    {
        g_runtimeCtx.ActiveWorldId = (worldId != 0) ? worldId : 1u;
        g_runtimeCtx.ActiveScene = activeScene;
        g_runtimeCtx.Physics = physicsWorld;
        g_runtimeCtx.Audio = audioWorld;
    }

    u32 ScriptEngine::GetActiveWorldId() const noexcept
    {
        return g_runtimeCtx.ActiveWorldId;
    }

    void ScriptEngine::BindNetworkBridge(NetworkBridgeCallbacks callbacks)
    {
        g_runtimeCtx.NetworkCallbacks = std::move(callbacks);
    }

    bool ScriptEngine::NotifyNetworkSpawn(
        u32 worldId,
        u64 netIdPacked,
        u64 sceneEntityPacked,
        u64 ownerPlayerId,
        bool isServer,
        bool isClient,
        bool isOwner)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId; (void)netIdPacked; (void)sceneEntityPacked; (void)ownerPlayerId;
        (void)isServer; (void)isClient; (void)isOwner;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyNetworkSpawn)
        {
            return false;
        }
        return m_impl->ManagedNotifyNetworkSpawn(
            worldId,
            netIdPacked,
            sceneEntityPacked,
            ownerPlayerId,
            isServer ? 1u : 0u,
            isClient ? 1u : 0u,
            isOwner ? 1u : 0u) == 0;
#endif
    }

    bool ScriptEngine::NotifyNetworkDespawn(u32 worldId, u64 netIdPacked)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId; (void)netIdPacked;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyNetworkDespawn)
        {
            return false;
        }
        return m_impl->ManagedNotifyNetworkDespawn(worldId, netIdPacked) == 0;
#endif
    }

    bool ScriptEngine::NotifyNetworkOwnershipChanged(
        u32 worldId,
        u64 netIdPacked,
        u64 previousOwnerPlayerId,
        u64 newOwnerPlayerId,
        bool isOwner)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId; (void)netIdPacked; (void)previousOwnerPlayerId; (void)newOwnerPlayerId; (void)isOwner;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyNetworkOwnershipChanged)
        {
            return false;
        }
        return m_impl->ManagedNotifyNetworkOwnershipChanged(
            worldId,
            netIdPacked,
            previousOwnerPlayerId,
            newOwnerPlayerId,
            isOwner ? 1u : 0u) == 0;
#endif
    }

    bool ScriptEngine::NotifyNetworkWorldChanged(u32 previousWorldId, u32 newWorldId, u64 netIdPacked)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)previousWorldId; (void)newWorldId; (void)netIdPacked;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyNetworkWorldChanged)
        {
            return false;
        }
        return m_impl->ManagedNotifyNetworkWorldChanged(previousWorldId, newWorldId, netIdPacked) == 0;
#endif
    }

    bool ScriptEngine::NotifyWorldDestroyed(u32 worldId)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedNotifyWorldDestroyed)
        {
            return false;
        }
        return m_impl->ManagedNotifyWorldDestroyed(worldId) == 0;
#endif
    }

    bool ScriptEngine::DispatchManagedPropertySync(
        u32 worldId,
        u64 netIdPacked,
        u32 behaviourHash,
        std::span<const u8> propsBytes)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId; (void)netIdPacked; (void)behaviourHash; (void)propsBytes;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedDispatchPropertySync || propsBytes.empty())
        {
            return false;
        }
        return m_impl->ManagedDispatchPropertySync(
            worldId,
            netIdPacked,
            behaviourHash,
            propsBytes.data(),
            static_cast<u32>(propsBytes.size())) == 0;
#endif
    }

    bool ScriptEngine::DispatchManagedRpc(
        u32 worldId,
        u64 netIdPacked,
        u32 behaviourHash,
        u32 methodHash,
        u64 senderPlayerId,
        bool isServerSide,
        std::span<const u8> argsBytes)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId; (void)netIdPacked; (void)behaviourHash; (void)methodHash;
        (void)senderPlayerId; (void)isServerSide; (void)argsBytes;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedDispatchRpc)
        {
            return false;
        }
        return m_impl->ManagedDispatchRpc(
            worldId,
            netIdPacked,
            behaviourHash,
            methodHash,
            senderPlayerId,
            isServerSide ? 1u : 0u,
            argsBytes.empty() ? nullptr : argsBytes.data(),
            static_cast<u32>(argsBytes.size())) == 0;
#endif
    }

    bool ScriptEngine::SyncManagedNetworkProperties(u32 worldId)
    {
#if !MORBI_HAS_DOTNET_HOST
        (void)worldId;
        return false;
#else
        if (!m_impl->Initialized || !m_impl->ManagedSyncNetworkProperties)
        {
            return false;
        }
        return m_impl->ManagedSyncNetworkProperties(worldId) == 0;
#endif
    }
}
