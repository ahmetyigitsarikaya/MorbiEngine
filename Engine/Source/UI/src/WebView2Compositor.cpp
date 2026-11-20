#include "Morbi/UI/WebView2Compositor.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Diagnostics/DebugConfig.h"
#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/Math/Math.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <commdlg.h>
    #include <dcomp.h>
    #include <wrl/client.h>

namespace
{
    struct EventRegistrationToken
    {
        std::int64_t value;
    };

    struct ICoreWebView2Environment;
    struct ICoreWebView2Controller;
    struct ICoreWebView2;
    struct ICoreWebView2WebMessageReceivedEventArgs;
    struct ICoreWebView2WebMessageReceivedEventHandler;

    struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* environment) = 0;
    };

    struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) = 0;
    };

    struct ICoreWebView2Environment : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parentWindow, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream* content, int statusCode, LPCWSTR reasonPhrase, LPCWSTR headers, void** response) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR* versionInfo) = 0;
    };

    struct ICoreWebView2 : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_Settings(void** settings) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* uri) = 0;
        virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
        virtual HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR htmlContent) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_NavigationStarting(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_ContentLoading(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_ContentLoading(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_HistoryChanged(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_HistoryChanged(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_PermissionRequested(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_ProcessFailed(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR javaScript, void* handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR id) = 0;
        virtual HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR javaScript, void* handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE CapturePreview(int imageFormat, IStream* imageStream, void* handler) = 0;
        virtual HRESULT STDMETHODCALLTYPE Reload() = 0;
        virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR webMessageAsJson) = 0;
        virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR webMessageAsString) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(ICoreWebView2WebMessageReceivedEventHandler* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(EventRegistrationToken token) = 0;
    };

    struct ICoreWebView2WebMessageReceivedEventArgs : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* source) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR* webMessageAsJson) = 0;
        virtual HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR* webMessageAsString) = 0;
    };

    struct ICoreWebView2WebMessageReceivedEventHandler : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) = 0;
    };

    struct COREWEBVIEW2_COLOR
    {
        BYTE A;
        BYTE R;
        BYTE G;
        BYTE B;
    };

    struct ICoreWebView2Controller2 : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_DefaultBackgroundColor(COREWEBVIEW2_COLOR* backgroundColor) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_DefaultBackgroundColor(COREWEBVIEW2_COLOR backgroundColor) = 0;
    };

    struct ICoreWebView2Settings : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL* isScriptEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL isScriptEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL* isWebMessageEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL isWebMessageEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL* areDefaultScriptDialogsEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL areDefaultScriptDialogsEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL* isStatusBarEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL* areDevToolsEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL* enabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL enabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_AreHostObjectsAllowed(BOOL* allowed) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL allowed) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsZoomControlEnabled(BOOL* enabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL enabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsBuiltInErrorPageEnabled(BOOL* enabled) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsBuiltInErrorPageEnabled(BOOL enabled) = 0;
    };

    struct ICoreWebView2NavigationCompletedEventArgs : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL* isSuccess) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_WebErrorStatus(int* webErrorStatus) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_NavigationId(UINT64* navigationId) = 0;
    };

    struct ICoreWebView2NavigationCompletedEventHandler : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) = 0;
    };

    static const IID IID_ICoreWebView2Controller2 = { 0xc979903e, 0xd4ca, 0x4228, { 0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab } };

    struct ICoreWebView2Controller : IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL* isVisible) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Bounds(RECT* bounds) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ZoomFactor(double* zoomFactor) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor) = 0;
        virtual HRESULT STDMETHODCALLTYPE MoveFocus(int reason) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_GotFocus(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_LostFocus(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(void* eventHandler, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND* parentWindow) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND parentWindow) = 0;
        virtual HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() = 0;
        virtual HRESULT STDMETHODCALLTYPE Close() = 0;
        virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2** coreWebView2) = 0;
    };

    using CreateCoreWebView2EnvironmentWithOptionsFn = HRESULT (STDAPICALLTYPE*)(PCWSTR, PCWSTR, void*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    using GetAvailableCoreWebView2BrowserVersionStringFn = HRESULT (STDAPICALLTYPE*)(PCWSTR, LPWSTR*);

    std::string WideToUtf8(std::wstring_view wide)
    {
        if (wide.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::string EnsureValidUtf8(std::string_view input)
    {
        if (input.empty()) return {};
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0) > 0)
        {
            return std::string(input);
        }

        // Fallback: Windows ANSI (e.g. CP1254 Turkish path from std::filesystem::path::string()) -> UTF-8
        const int wideLen = MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
        if (wideLen > 0)
        {
            std::wstring wide(static_cast<size_t>(wideLen), L'\0');
            MultiByteToWideChar(CP_ACP, 0, input.data(), static_cast<int>(input.size()), wide.data(), wideLen);
            return WideToUtf8(wide);
        }

        std::string sanitized;
        sanitized.reserve(input.size());
        for (const unsigned char ch : input)
        {
            sanitized.push_back((ch < 0x80u) ? static_cast<char>(ch) : '?');
        }
        return sanitized;
    }

    std::wstring Utf8ToWide(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
        return result;
    }

    Morbi::Math::Vec3 ToEulerDegrees(const Morbi::Math::Quat& q) noexcept
    {
        const float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
        const float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
        const float roll = std::atan2(sinr_cosp, cosr_cosp);

        const float sinp = 2.0f * (q.w * q.y - q.z * q.x);
        float pitch = 0.0f;
        if (std::abs(sinp) >= 1.0f)
            pitch = std::copysign(3.14159265f / 2.0f, sinp);
        else
            pitch = std::asin(sinp);

        const float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
        const float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
        const float yaw = std::atan2(siny_cosp, cosy_cosp);

        return { roll * Morbi::Math::kRadToDeg, pitch * Morbi::Math::kRadToDeg, yaw * Morbi::Math::kRadToDeg };
    }
}
#endif

namespace Morbi::UI
{
    using namespace Morbi::Math;

    struct WebView2Compositor::Impl
    {
        WebView2CompositorDesc Desc{};
        BinaryIPCBridge* IpcBridge = nullptr;
        ViewportRect ActiveViewport{ 284, 72, 686, 344 };
        bool MenuOpen = false;
        ViewportRect MenuRect{};
        std::vector<ViewportRect> MenuRects{};

        RHI::TextureHandle SharedViewportTex = RHI::TextureHandle::Invalid();
        RHI::BindlessResourceIndex SharedViewportBindless{};

#if defined(_WIN32)
        HWND ParentHwnd = nullptr;   // WebView2 host child window
        HWND MainHwnd = nullptr;     // SDL window that owns the Vulkan swapchain
        HWND ViewportChildHwnd = nullptr; // Embedded 3D viewport native window

        // Cuts the 3D viewport rectangle out of the WebView2 host window so the native
        // Vulkan scene shows through inside the editor layout.
        // If a dropdown menu or dialog is open that overlaps the viewport, the hole is
        // dynamically carved around the menu and the native viewport window is clipped,
        // ensuring the menu is rendered 100% on top without being covered by the 3D scene.
        void ApplyViewportHole()
        {
            if (ParentHwnd == nullptr) return;
            RECT rc{};
            GetClientRect(ParentHwnd, &rc);
            if (rc.right <= 0 || rc.bottom <= 0) return;

            const int x0 = static_cast<int>(ActiveViewport.X);
            const int y0 = static_cast<int>(ActiveViewport.Y);
            const int w = static_cast<int>(ActiveViewport.Width);
            const int h = static_cast<int>(ActiveViewport.Height);
            if (w <= 0 || h <= 0) return;

            HRGN full = CreateRectRgn(0, 0, rc.right, rc.bottom);
            HRGN hole = CreateRectRgn(x0, y0, x0 + w, y0 + h);
            if (full == nullptr || hole == nullptr)
            {
                if (full != nullptr) DeleteObject(full);
                if (hole != nullptr) DeleteObject(hole);
                return;
            }

            if (MenuOpen && !MenuRects.empty())
            {
                HRGN allMenusRgn = CreateRectRgn(0, 0, 0, 0);
                HRGN vpClipRgn = CreateRectRgn(0, 0, 0, 0);

                for (const auto& mr : MenuRects)
                {
                    const int mx0 = static_cast<int>(mr.X);
                    const int my0 = static_cast<int>(mr.Y);
                    const int mw = static_cast<int>(mr.Width);
                    const int mh = static_cast<int>(mr.Height);
                    if (mw <= 0 || mh <= 0) continue;

                    // 1. Accumulate all menus/toasts into allMenusRgn
                    HRGN menuRgn = CreateRectRgn(mx0, my0, mx0 + mw, my0 + mh);
                    if (menuRgn != nullptr)
                    {
                        if (allMenusRgn != nullptr)
                        {
                            CombineRgn(allMenusRgn, allMenusRgn, menuRgn, RGN_OR);
                        }
                        DeleteObject(menuRgn);
                    }

                    // 2. Accumulate intersection with 3D viewport into vpClipRgn
                    const int localX = mx0 - x0;
                    const int localY = my0 - y0;
                    if (localX < w && localY < h && (localX + mw) > 0 && (localY + mh) > 0)
                    {
                        const int clipLeft = (std::max)(0, localX);
                        const int clipTop = (std::max)(0, localY);
                        const int clipRight = (std::min)(w, localX + mw);
                        const int clipBottom = (std::min)(h, localY + mh);

                        HRGN vpMenu = CreateRectRgn(clipLeft, clipTop, clipRight, clipBottom);
                        if (vpMenu != nullptr)
                        {
                            if (vpClipRgn != nullptr)
                            {
                                CombineRgn(vpClipRgn, vpClipRgn, vpMenu, RGN_OR);
                            }
                            DeleteObject(vpMenu);
                        }
                    }
                }

                // Exclude all open menus/toasts from the hole so ParentHwnd retains the menu region!
                if (allMenusRgn != nullptr)
                {
                    CombineRgn(hole, hole, allMenusRgn, RGN_DIFF);
                    DeleteObject(allMenusRgn);
                }

                // Clip ViewportChildHwnd so native 3D Vulkan cannot paint inside any open menu/toast
                if (ViewportChildHwnd != nullptr)
                {
                    HRGN vpFull = CreateRectRgn(0, 0, w, h);
                    if (vpFull != nullptr)
                    {
                        if (vpClipRgn != nullptr)
                        {
                            CombineRgn(vpFull, vpFull, vpClipRgn, RGN_DIFF);
                        }
                        if (!SetWindowRgn(ViewportChildHwnd, vpFull, TRUE))
                        {
                            DeleteObject(vpFull);
                        }
                    }
                    else
                    {
                        SetWindowRgn(ViewportChildHwnd, nullptr, TRUE);
                    }
                }
                if (vpClipRgn != nullptr)
                {
                    DeleteObject(vpClipRgn);
                }

                // 3. Bring ParentHwnd (WebView2) to the top of the Z-order so HTML menu/toast renders on top
                SetWindowPos(ParentHwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            else
            {
                if (ViewportChildHwnd != nullptr)
                {
                    SetWindowRgn(ViewportChildHwnd, nullptr, TRUE);
                    SetWindowPos(ViewportChildHwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }

            CombineRgn(full, full, hole, RGN_DIFF);
            DeleteObject(hole);
            if (!SetWindowRgn(ParentHwnd, full, TRUE))
            {
                DeleteObject(full);
            }

            // Explicitly ensure all Chromium child windows have NULL region
            EnumChildWindows(ParentHwnd, [](HWND child, LPARAM) -> BOOL {
                SetWindowRgn(child, nullptr, TRUE);
                return TRUE;
            }, 0);
        }
        RECT LastControllerBounds{};
        Microsoft::WRL::ComPtr<IDCompositionDevice> DCompDevice;
        Microsoft::WRL::ComPtr<IDCompositionTarget> DCompTarget;
        Microsoft::WRL::ComPtr<IDCompositionVisual> RootVisual;
        Microsoft::WRL::ComPtr<IDCompositionVisual> ViewportVisual;
        Microsoft::WRL::ComPtr<IDCompositionVisual> WebViewOverlayVisual;

        HMODULE WebView2LoaderModule = nullptr;
        ICoreWebView2Environment* Environment = nullptr;
        ICoreWebView2Controller* Controller = nullptr;
        ICoreWebView2* WebView = nullptr;
        EventRegistrationToken MessageToken{};
        bool MessageTokenRegistered = false;
        bool WebViewReady = false;
#endif

        std::mutex LogQueueMutex;
        std::vector<std::string> PendingLogsJson;

        std::mutex IncomingMessagesMutex;
        std::vector<std::string> PendingIncomingJson;

        EditorPlayMode CurrentPlayMode = EditorPlayMode::Edit;
        EditorPlayMode RequestedPlayMode = EditorPlayMode::Edit;
        bool PlayModeTransitionPending = false;

        bool SimulationPaused = false;
        bool StepRequested = false;

        std::vector<AssetInstantiationRequest> PendingAssetInstantiations;
        std::vector<EntityCommandRequest> PendingEntityCommands;
        std::mutex ModelImportMutex;
        std::vector<std::string> PendingImportModelPaths;
        bool TriggerNativeOpenModelDialog = false;
        bool TriggerNativeOpenSceneDialog = false;
        bool TriggerNativeSaveSceneDialog = false;
        bool NewScenePending = false;
        bool CompileScriptsPending = false;
        bool BuildGamePending = false;
        bool LoadScenePending = false;
        std::string PendingLoadScenePath;
        bool SaveScenePending = false;
        std::string PendingSaveScenePath;
        std::string ActiveGizmoTool = "translate";
        std::string ActiveViewportTab = "Scene";

        PhysicsTelemetrySnapshot PhysicsTelemetry{};
        std::vector<Scene::EntityId> PendingPhysicsSyncEntities{};
        bool PhysicsDebugDrawEnabled = false;

        std::string CachedSceneSyncJson;
        std::string CachedAssetSyncJson;

        WebView2CompositorStats Stats{};
        bool Initialized = false;
    };

#if defined(_WIN32)
    class WebMessageReceivedHandler final : public ICoreWebView2WebMessageReceivedEventHandler
    {
    public:
        explicit WebMessageReceivedHandler(WebView2Compositor::Impl& owner) : m_owner(owner) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { if (!object) return E_POINTER; *object = this; AddRef(); return S_OK; }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
        ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --m_refCount; if (count == 0) delete this; return count; }

        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) override
        {
            if (args != nullptr)
            {
                LPWSTR msg = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg != nullptr)
                {
                    const std::string utf8 = WideToUtf8(msg);
                    CoTaskMemFree(msg);
                    if (utf8.rfind("JS_", 0) == 0)
                    {
                        Core::Log::Info("UI.WebView2.Diagnostics", utf8);
                        if (utf8.find("JS_REACT_MOUNTED") != std::string::npos)
                        {
                            if (!m_owner.CachedSceneSyncJson.empty() && m_owner.WebView != nullptr)
                            {
                                const std::wstring wScene = Utf8ToWide(m_owner.CachedSceneSyncJson);
                                m_owner.WebView->PostWebMessageAsJson(wScene.c_str());
                            }
                            if (!m_owner.CachedAssetSyncJson.empty() && m_owner.WebView != nullptr)
                            {
                                const std::wstring wAssets = Utf8ToWide(m_owner.CachedAssetSyncJson);
                                m_owner.WebView->PostWebMessageAsJson(wAssets.c_str());
                            }
                        }
                    }
                    else if (!utf8.empty() && utf8[0] == '{')
                    {
                        std::lock_guard<std::mutex> lock(m_owner.IncomingMessagesMutex);
                        m_owner.PendingIncomingJson.push_back(utf8);
                    }
                    else if (m_owner.IpcBridge != nullptr)
                    {
                        m_owner.IpcBridge->EnqueueIncomingBinaryFrame(
                            std::span<const u8>(reinterpret_cast<const u8*>(utf8.data()), utf8.size()));
                    }
                }
            }
            return S_OK;
        }
    private:
        WebView2Compositor::Impl& m_owner;
        ULONG m_refCount = 1;
    };

    class NavigationCompletedHandler final : public ICoreWebView2NavigationCompletedEventHandler
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { if (!object) return E_POINTER; *object = this; AddRef(); return S_OK; }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
        ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --m_refCount; if (count == 0) delete this; return count; }

        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) override
        {
            if (args != nullptr)
            {
                BOOL isSuccess = FALSE;
                args->get_IsSuccess(&isSuccess);
                int status = 0;
                args->get_WebErrorStatus(&status);
                if (isSuccess)
                {
                    Core::Log::Info("UI.WebView2", "NavigationCompleted: Page loaded successfully!");
                }
                else
                {
                    Core::Log::Error("UI.WebView2", "NavigationCompleted: Page load FAILED with error status: " + std::to_string(status));
                }
            }
            return S_OK;
        }
    private:
        ULONG m_refCount = 1;
    };

    class ControllerCompletedHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
    {
    public:
        ControllerCompletedHandler(WebView2Compositor::Impl& owner, std::wstring url)
            : m_owner(owner), m_url(std::move(url)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { if (!object) return E_POINTER; *object = this; AddRef(); return S_OK; }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
        ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --m_refCount; if (count == 0) delete this; return count; }

        HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) override
        {
            if (FAILED(errorCode) || controller == nullptr)
            {
                Core::Log::Warn("UI.WebView2", "CreateCoreWebView2Controller failed: 0x" + std::to_string(errorCode));
                return S_OK;
            }
            m_owner.Controller = controller;
            m_owner.Controller->AddRef();

            // Set 100% transparent background (Alpha=0) so underlying 3D swapchain shines through viewport cutout
            ICoreWebView2Controller2* ctrl2 = nullptr;
            if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, reinterpret_cast<void**>(&ctrl2))) && ctrl2 != nullptr)
            {
                COREWEBVIEW2_COLOR transparentBg{ 0, 0, 0, 0 };
                ctrl2->put_DefaultBackgroundColor(transparentBg);
                ctrl2->Release();
            }

            m_owner.Controller->put_IsVisible(TRUE);

            if (m_owner.ParentHwnd != nullptr)
            {
                RECT rc{};
                GetClientRect(m_owner.ParentHwnd, &rc);
                m_owner.Controller->put_Bounds(rc);
                m_owner.Controller->NotifyParentWindowPositionChanged();
                m_owner.ApplyViewportHole();
            }

            if (SUCCEEDED(m_owner.Controller->get_CoreWebView2(&m_owner.WebView)) && m_owner.WebView != nullptr)
            {
                // Enable settings
                ICoreWebView2Settings* settings = nullptr;
                if (SUCCEEDED(m_owner.WebView->get_Settings(reinterpret_cast<void**>(&settings))) && settings != nullptr)
                {
                    settings->put_IsScriptEnabled(TRUE);
                    settings->put_IsWebMessageEnabled(TRUE);
                    settings->put_AreDevToolsEnabled(TRUE);
                    settings->put_AreDefaultContextMenusEnabled(TRUE);
                    settings->Release();
                }

                auto* msgHandler = new WebMessageReceivedHandler(m_owner);
                if (SUCCEEDED(m_owner.WebView->add_WebMessageReceived(msgHandler, &m_owner.MessageToken)))
                {
                    m_owner.MessageTokenRegistered = true;
                }
                msgHandler->Release();

                // Diagnostic error hook script
                m_owner.WebView->AddScriptToExecuteOnDocumentCreated(
                    L"window.addEventListener('error', function(e) { window.chrome.webview.postMessage('JS_ERR: ' + e.message + ' (' + e.filename + ':' + e.lineno + ')'); });"
                    L"window.addEventListener('unhandledrejection', function(e) { window.chrome.webview.postMessage('JS_REJECT: ' + e.reason); });"
                    L"window.addEventListener('DOMContentLoaded', function() { window.chrome.webview.postMessage('JS_DOM_READY: ' + (document.getElementById('root') ? 'root_ok' : 'no_root')); });"
                    L"console.error = function(...args) { window.chrome.webview.postMessage('JS_CONSOLE_ERR: ' + args.join(' ')); };",
                    nullptr);

                auto* navHandler = new NavigationCompletedHandler();
                EventRegistrationToken navToken{};
                m_owner.WebView->add_NavigationCompleted(navHandler, &navToken);
                navHandler->Release();

                m_owner.WebView->Navigate(m_url.c_str());
                m_owner.WebViewReady = true;
                Core::Log::Info("UI.WebView2", "WebView2 Controller active & navigated to: " + WideToUtf8(m_url));
            }
            return S_OK;
        }
    private:
        WebView2Compositor::Impl& m_owner;
        std::wstring m_url;
        ULONG m_refCount = 1;
    };

    class EnvironmentCompletedHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
    {
    public:
        EnvironmentCompletedHandler(WebView2Compositor::Impl& owner, HWND parentHwnd, std::wstring url)
            : m_owner(owner), m_parentHwnd(parentHwnd), m_url(std::move(url)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { if (!object) return E_POINTER; *object = this; AddRef(); return S_OK; }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refCount; }
        ULONG STDMETHODCALLTYPE Release() override { const ULONG count = --m_refCount; if (count == 0) delete this; return count; }

        HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* environment) override
        {
            if (FAILED(errorCode) || environment == nullptr)
            {
                Core::Log::Warn("UI.WebView2", "CreateCoreWebView2Environment failed: 0x" + std::to_string(errorCode));
                return S_OK;
            }
            m_owner.Environment = environment;
            m_owner.Environment->AddRef();
            auto* ctrlHandler = new ControllerCompletedHandler(m_owner, m_url);
            m_owner.Environment->CreateCoreWebView2Controller(m_parentHwnd, ctrlHandler);
            ctrlHandler->Release();
            return S_OK;
        }
    private:
        WebView2Compositor::Impl& m_owner;
        HWND m_parentHwnd = nullptr;
        std::wstring m_url;
        ULONG m_refCount = 1;
    };
#endif

    WebView2Compositor::WebView2Compositor()
        : m_impl(std::make_unique<Impl>())
    {
    }

    WebView2Compositor::~WebView2Compositor()
    {
#if defined(_WIN32)
        if (m_impl && m_impl->WebView2LoaderModule != nullptr)
        {
            FreeLibrary(m_impl->WebView2LoaderModule);
            m_impl->WebView2LoaderModule = nullptr;
        }
#endif
    }

    Core::Result<bool, std::string> WebView2Compositor::Initialize(
        const WebView2CompositorDesc& desc,
        const Platform::NativeWindowHandle& windowHandle,
        RHI::IDevice& rhiDevice,
        BinaryIPCBridge& ipcBridge)
    {
        m_impl->Desc = desc;
        m_impl->IpcBridge = &ipcBridge;

        // 1. DirectComposition Visual Device (Optional HWND composition)
#if defined(_WIN32)
        if (!desc.HeadlessMode && desc.Mode == UIHostSurfaceMode::EditorAndHub && windowHandle.Window != nullptr)
        {
            const HRESULT dcompHr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&m_impl->DCompDevice));
            if (SUCCEEDED(dcompHr) && m_impl->DCompDevice != nullptr)
            {
                const HRESULT v1 = m_impl->DCompDevice->CreateVisual(&m_impl->RootVisual);
                const HRESULT v2 = m_impl->DCompDevice->CreateVisual(&m_impl->ViewportVisual);
                const HRESULT v3 = m_impl->DCompDevice->CreateVisual(&m_impl->WebViewOverlayVisual);
                if (SUCCEEDED(v1) && SUCCEEDED(v2) && SUCCEEDED(v3) &&
                    m_impl->RootVisual && m_impl->ViewportVisual && m_impl->WebViewOverlayVisual)
                {
                    m_impl->RootVisual->AddVisual(m_impl->ViewportVisual.Get(), FALSE, nullptr);
                    m_impl->RootVisual->AddVisual(m_impl->WebViewOverlayVisual.Get(), TRUE, m_impl->ViewportVisual.Get());
                    m_impl->Stats.DirectCompositionActive = true;
                    MORBI_TRACE("UI.WebView2.DComp", "DComposition bound: Device=" +
                        std::to_string(reinterpret_cast<uintptr_t>(m_impl->DCompDevice.Get())) +
                        ", RootVisual=" + std::to_string(reinterpret_cast<uintptr_t>(m_impl->RootVisual.Get())) +
                        ", ViewportVisual=" + std::to_string(reinterpret_cast<uintptr_t>(m_impl->ViewportVisual.Get())) +
                        ", OverlayVisual=" + std::to_string(reinterpret_cast<uintptr_t>(m_impl->WebViewOverlayVisual.Get())) +
                        ", MainHWND=" + std::to_string(reinterpret_cast<uintptr_t>(windowHandle.Window)));
                }
                else
                {
                    Core::Log::Warn("UI.WebView2.DComp", "DComposition CreateVisual failed.");
                }
            }
            else
            {
                MORBI_TRACE("UI.WebView2.DComp", "DCompositionCreateDevice returned HRESULT=" + std::to_string(dcompHr));
            }
        }
#else
        (void)windowHandle;
#endif

        // 2. Create Shared 3D Viewport GPU Texture (Bindless SRV + RenderTarget)
        MORBI_BREADCRUMB("WebView2 SharedTexture Resize");
        if (m_impl->ActiveViewport.Width == 0 || m_impl->ActiveViewport.Height == 0 ||
            m_impl->ActiveViewport.Width > 16384u || m_impl->ActiveViewport.Height > 16384u)
        {
            Core::Log::Error("UI.WebView2", "Invalid initial SharedViewport dimensions: " +
                std::to_string(m_impl->ActiveViewport.Width) + "x" + std::to_string(m_impl->ActiveViewport.Height));
        }

        const RHI::TextureDesc sharedViewportDesc{
            .Width = m_impl->ActiveViewport.Width,
            .Height = m_impl->ActiveViewport.Height,
            .PixelFormat = rhiDevice.GetSwapchainFormat(),
            .RenderTarget = true,
            .Storage = false,
            .DebugName = "WebView2_Shared3DViewportTexture"
        };

        m_impl->SharedViewportTex = rhiDevice.CreateTexture(sharedViewportDesc, nullptr);
        m_impl->SharedViewportBindless = rhiDevice.GetBindlessIndex(m_impl->SharedViewportTex);

        m_impl->Stats.SharedViewportTextureActive = m_impl->SharedViewportTex.IsValid();
        m_impl->Stats.SharedTextureBindlessIndex = m_impl->SharedViewportBindless.Index;
        m_impl->Stats.SharedTextureGpuHandle =
            (static_cast<u64>(m_impl->SharedViewportBindless.Index) << 32) | m_impl->SharedViewportTex.Index;

        if (!m_impl->SharedViewportTex.IsValid() || !m_impl->SharedViewportBindless.IsValid())
        {
            Core::Log::Warn("UI.WebView2", "SharedViewportTexture validation failed! TexValid=" +
                std::string(m_impl->SharedViewportTex.IsValid() ? "YES" : "NO") +
                ", BindlessValid=" + std::string(m_impl->SharedViewportBindless.IsValid() ? "YES" : "NO"));
        }
        else
        {
            MORBI_TRACE("UI.WebView2", "SharedViewportTexture Created: Size=" +
                std::to_string(m_impl->ActiveViewport.Width) + "x" + std::to_string(m_impl->ActiveViewport.Height) +
                ", TexHandle=(Idx=" + std::to_string(m_impl->SharedViewportTex.Index) +
                ", Gen=" + std::to_string(m_impl->SharedViewportTex.Generation) + ")" +
                ", BindlessSlot=" + std::to_string(m_impl->SharedViewportBindless.Index) +
                ", GpuHandle=" + std::to_string(m_impl->Stats.SharedTextureGpuHandle));
        }

        // 3. Push initial Shared Texture Handle & Viewport dimensions into Binary IPC Ring Buffer
        m_impl->IpcBridge->PushViewportSharedTexture(
            m_impl->ActiveViewport.Width,
            m_impl->ActiveViewport.Height,
            1.0f,
            m_impl->Stats.SharedTextureGpuHandle);

        m_impl->Initialized = true;

        std::filesystem::path entryHtml = desc.FrontendRootDir / "dist" / "index.html";
        bool isProductionBundle = true;
        bool isEmbeddedInExe = false;

#if defined(_WIN32)
        const std::filesystem::path userDataDir = std::filesystem::temp_directory_path() / "MorbiEngine" / "WebView2";
        const std::filesystem::path embeddedDir = userDataDir / "embedded_frontend";

        auto extractRcData = [](const wchar_t* resourceName, const std::filesystem::path& targetFile) -> bool {
            HMODULE exeModule = GetModuleHandleW(nullptr);
            HRSRC resInfo = FindResourceW(exeModule, resourceName, RT_RCDATA);
            if (!resInfo) return false;
            const DWORD resSize = SizeofResource(exeModule, resInfo);
            HGLOBAL resData = LoadResource(exeModule, resInfo);
            if (!resData || resSize == 0) return false;
            const void* ptr = LockResource(resData);
            if (!ptr) return false;

            std::error_code ec;
            std::filesystem::create_directories(targetFile.parent_path(), ec);
            if (std::filesystem::exists(targetFile, ec) && std::filesystem::file_size(targetFile, ec) == resSize)
            {
                return true;
            }
            std::ofstream out(targetFile, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(static_cast<const char*>(ptr), static_cast<std::streamsize>(resSize));
            return out.good();
        };

        if (extractRcData(L"MORBI_FRONTEND_HTML", embeddedDir / "index.html"))
        {
            extractRcData(L"MORBI_FRONTEND_MINIMAP", embeddedDir / "minimap.jpg");
            extractRcData(L"MORBI_FRONTEND_VIEWPORT_BG", embeddedDir / "viewport_bg.jpg");
            entryHtml = embeddedDir / "index.html";
            isProductionBundle = true;
            isEmbeddedInExe = true;
        }
#endif

        if (!isEmbeddedInExe && !std::filesystem::exists(entryHtml))
        {
            entryHtml = desc.FrontendRootDir / "index.html";
            isProductionBundle = false;
        }

        Core::Log::Info("UI.WebView2", "Initialized WebView2 Visual Compositor & Shared 3D Viewport Texture "
            "[Size=" + std::to_string(m_impl->ActiveViewport.Width) + "x" + std::to_string(m_impl->ActiveViewport.Height) +
            ", BindlessSlot=" + std::to_string(m_impl->SharedViewportBindless.Index) +
            ", DComposition=" + (m_impl->Stats.DirectCompositionActive ? "ON" : "Headless/SharedTex") +
            ", Bundle=" + (isEmbeddedInExe ? "EmbeddedInExe(RCDATA)" : (isProductionBundle ? "Production(dist/index.html)" : "Development")) +
            ", Frontend='" + entryHtml.string() + "']");

        // 4. Initialize Live WebView2 Host on Window HWND (when not headless and in Editor mode)
#if defined(_WIN32)
        if (!desc.HeadlessMode && desc.Mode == UIHostSurfaceMode::EditorAndHub && windowHandle.Window != nullptr)
        {
            HWND hwnd = static_cast<HWND>(windowHandle.Window);
            m_impl->MainHwnd = hwnd;

            // Dedicated child host for WebView2 so we can punch a hole for the native 3D viewport
            static const wchar_t* kHostClass = L"MorbiWebViewHost";
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kHostClass;
            wc.hbrBackground = nullptr;
            RegisterClassW(&wc);

            RECT mainRc{};
            GetClientRect(hwnd, &mainRc);
            HWND hostHwnd = CreateWindowExW(0, kHostClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                0, 0, mainRc.right, mainRc.bottom, hwnd, nullptr, wc.hInstance, nullptr);
            if (hostHwnd != nullptr)
            {
                SetWindowPos(hostHwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            m_impl->ParentHwnd = hostHwnd != nullptr ? hostHwnd : hwnd;
            hwnd = m_impl->ParentHwnd;

            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

            // Probe WebView2Loader.dll
            m_impl->WebView2LoaderModule = LoadLibraryW(L"WebView2Loader.dll");
            if (m_impl->WebView2LoaderModule == nullptr)
            {
                wchar_t exePath[MAX_PATH]{};
                if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
                {
                    const auto dir = std::filesystem::path(exePath).parent_path();
                    m_impl->WebView2LoaderModule = LoadLibraryW((dir / "WebView2Loader.dll").c_str());
                }
            }

            if (m_impl->WebView2LoaderModule != nullptr)
            {
                auto* getVer = reinterpret_cast<GetAvailableCoreWebView2BrowserVersionStringFn>(
                    GetProcAddress(m_impl->WebView2LoaderModule, "GetAvailableCoreWebView2BrowserVersionString"));
                auto* createEnv = reinterpret_cast<CreateCoreWebView2EnvironmentWithOptionsFn>(
                    GetProcAddress(m_impl->WebView2LoaderModule, "CreateCoreWebView2EnvironmentWithOptions"));

                LPWSTR ver = nullptr;
                if (getVer != nullptr && SUCCEEDED(getVer(nullptr, &ver)) && ver != nullptr)
                {
                    Core::Log::Info("UI.WebView2", "Detected Edge WebView2 Runtime: " + WideToUtf8(ver));
                    CoTaskMemFree(ver);
                }

                if (createEnv != nullptr)
                {
                    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                        L"--allow-file-access-from-files --disable-web-security --allow-file-access");

                    std::wstring navUrl = L"file:///" + entryHtml.lexically_normal().generic_wstring();
                    const std::filesystem::path userData = std::filesystem::temp_directory_path() / "MorbiEngine" / "WebView2";
                    std::filesystem::create_directories(userData);

                    auto* envHandler = new EnvironmentCompletedHandler(*m_impl, hwnd, navUrl);
                    const HRESULT hr = createEnv(nullptr, userData.c_str(), nullptr, envHandler);
                    envHandler->Release();

                    if (SUCCEEDED(hr))
                    {
                        Core::Log::Info("UI.WebView2", "Initializing WebView2 Controller for HWND=" +
                            std::to_string(reinterpret_cast<uintptr_t>(hwnd)));

                        // Bounded message pump to allow WebView2 controller to attach smoothly
                        const auto t0 = std::chrono::steady_clock::now();
                        while (!m_impl->WebViewReady &&
                               std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < 1200)
                        {
                            MSG msg;
                            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                            {
                                TranslateMessage(&msg);
                                DispatchMessageW(&msg);
                            }
                            Sleep(10);
                        }
                    }
                }
            }
            else
            {
                Core::Log::Warn("UI.WebView2", "WebView2Loader.dll not found beside executable or in engine bin folder.");
            }
        }
#endif

        return Core::Result<bool, std::string>::Success(true);
    }

    void WebView2Compositor::Shutdown(RHI::IDevice& rhiDevice)
    {
        if (!m_impl || !m_impl->Initialized)
        {
            return;
        }

        if (m_impl->SharedViewportTex.IsValid())
        {
            rhiDevice.DestroyTexture(m_impl->SharedViewportTex);
            m_impl->SharedViewportTex = RHI::TextureHandle::Invalid();
        }

#if defined(_WIN32)
        if (m_impl->WebView != nullptr)
        {
            if (m_impl->MessageTokenRegistered)
            {
                m_impl->WebView->remove_WebMessageReceived(m_impl->MessageToken);
                m_impl->MessageTokenRegistered = false;
            }
            m_impl->WebView->Release();
            m_impl->WebView = nullptr;
        }

        if (m_impl->Controller != nullptr)
        {
            m_impl->Controller->Close();
            m_impl->Controller->Release();
            m_impl->Controller = nullptr;
        }

        if (m_impl->Environment != nullptr)
        {
            m_impl->Environment->Release();
            m_impl->Environment = nullptr;
        }

        m_impl->WebViewOverlayVisual.Reset();
        m_impl->ViewportVisual.Reset();
        m_impl->RootVisual.Reset();
        m_impl->DCompTarget.Reset();
        m_impl->DCompDevice.Reset();

        if (m_impl->ParentHwnd != nullptr && m_impl->ParentHwnd != m_impl->MainHwnd)
        {
            DestroyWindow(m_impl->ParentHwnd);
        }
        m_impl->ParentHwnd = nullptr;
        m_impl->ViewportChildHwnd = nullptr;
        m_impl->MenuRects.clear();
        m_impl->MenuOpen = false;

        if (m_impl->WebView2LoaderModule != nullptr)
        {
            FreeLibrary(m_impl->WebView2LoaderModule);
            m_impl->WebView2LoaderModule = nullptr;
        }
#endif

        m_impl->Initialized = false;
    }

    void WebView2Compositor::SetViewportChildWindow(void* hwnd)
    {
#if defined(_WIN32)
        if (m_impl)
        {
            m_impl->ViewportChildHwnd = static_cast<HWND>(hwnd);
            MORBI_TRACE("UI.WebView2", "SetViewportChildWindow HWND=" +
                std::to_string(reinterpret_cast<uintptr_t>(m_impl->ViewportChildHwnd)) +
                " (IsWindow=" + (IsWindow(m_impl->ViewportChildHwnd) ? "TRUE" : "FALSE") + ")");
            m_impl->ApplyViewportHole();
        }
#else
        (void)hwnd;
#endif
    }

    void WebView2Compositor::OnWindowResize(u32 width, u32 height)
    {
        if (!m_impl || width == 0 || height == 0)
        {
            return;
        }

#if defined(_WIN32)
        if (m_impl->ParentHwnd != nullptr && m_impl->ParentHwnd != m_impl->MainHwnd)
        {
            SetWindowPos(m_impl->ParentHwnd, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_impl->Controller != nullptr)
        {
            RECT rc{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            m_impl->Controller->put_Bounds(rc);
            m_impl->Controller->NotifyParentWindowPositionChanged();
        }
        m_impl->ApplyViewportHole();
#endif
    }

    void WebView2Compositor::ResizeViewport(RHI::IDevice& rhiDevice, const ViewportRect& newViewportRect, f32 dpiScale)
    {
        if (!m_impl || !m_impl->Initialized)
        {
            return;
        }

        if (newViewportRect.Width == 0 || newViewportRect.Height == 0 ||
            newViewportRect.Width > 16384u || newViewportRect.Height > 16384u)
        {
            MORBI_TRACE("UI.WebView2", "ResizeViewport ignored invalid dimensions: " +
                std::to_string(newViewportRect.Width) + "x" + std::to_string(newViewportRect.Height));
            return;
        }

        MORBI_BREADCRUMB("WebView2 SharedTexture Resize");
        MORBI_TRACE("UI.WebView2", "ResizeViewport: Old=(" +
            std::to_string(m_impl->ActiveViewport.Width) + "x" + std::to_string(m_impl->ActiveViewport.Height) +
            ") -> New=(" + std::to_string(newViewportRect.Width) + "x" + std::to_string(newViewportRect.Height) +
            "), DPI=" + std::to_string(dpiScale) +
            ", DCompDevice=" + std::to_string(reinterpret_cast<uintptr_t>(m_impl->DCompDevice.Get())) +
            ", ViewportChildHwnd=" + std::to_string(reinterpret_cast<uintptr_t>(m_impl->ViewportChildHwnd)));

        m_impl->ActiveViewport = newViewportRect;

        if (m_impl->SharedViewportTex.IsValid())
        {
            rhiDevice.DestroyTexture(m_impl->SharedViewportTex);
        }

        const RHI::TextureDesc sharedViewportDesc{
            .Width = m_impl->ActiveViewport.Width,
            .Height = m_impl->ActiveViewport.Height,
            .PixelFormat = rhiDevice.GetSwapchainFormat(),
            .RenderTarget = true,
            .Storage = false,
            .DebugName = "WebView2_Shared3DViewportTexture"
        };

        m_impl->SharedViewportTex = rhiDevice.CreateTexture(sharedViewportDesc, nullptr);
        m_impl->SharedViewportBindless = rhiDevice.GetBindlessIndex(m_impl->SharedViewportTex);
        m_impl->Stats.SharedViewportTextureActive = m_impl->SharedViewportTex.IsValid();
        m_impl->Stats.SharedTextureBindlessIndex = m_impl->SharedViewportBindless.Index;
        m_impl->Stats.SharedTextureGpuHandle =
            (static_cast<u64>(m_impl->SharedViewportBindless.Index) << 32) | m_impl->SharedViewportTex.Index;

        if (!m_impl->SharedViewportTex.IsValid() || !m_impl->SharedViewportBindless.IsValid())
        {
            Core::Log::Error("UI.WebView2", "ResizeViewport failed to allocate valid SharedTexture/BindlessSlot!");
        }
        else
        {
            MORBI_TRACE("UI.WebView2", "ResizeViewport SharedTexture Ready: Size=" +
                std::to_string(m_impl->ActiveViewport.Width) + "x" + std::to_string(m_impl->ActiveViewport.Height) +
                ", TexIdx=" + std::to_string(m_impl->SharedViewportTex.Index) +
                ", BindlessSlot=" + std::to_string(m_impl->SharedViewportBindless.Index) +
                ", GpuHandle=" + std::to_string(m_impl->Stats.SharedTextureGpuHandle));
        }

        if (m_impl->IpcBridge != nullptr)
        {
            m_impl->IpcBridge->PushViewportSharedTexture(
                m_impl->ActiveViewport.Width,
                m_impl->ActiveViewport.Height,
                dpiScale,
                m_impl->Stats.SharedTextureGpuHandle);
        }
    }

    void WebView2Compositor::BeginFrameSync(
        Scene::Scene& activeScene,
        Scene::EntityId trackedEntity,
        const BinaryTelemetryPayload& telemetry)
    {
        if (!m_impl || !m_impl->Initialized || m_impl->IpcBridge == nullptr)
        {
            return;
        }

#if defined(_WIN32)
        // Pump Win32 messages so WebView2 receives input, updates layout, and executes callbacks
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (m_impl->Controller != nullptr && m_impl->ParentHwnd != nullptr)
        {
            RECT rc{};
            if (GetClientRect(m_impl->ParentHwnd, &rc))
            {
                if (rc.left != m_impl->LastControllerBounds.left || rc.top != m_impl->LastControllerBounds.top ||
                    rc.right != m_impl->LastControllerBounds.right || rc.bottom != m_impl->LastControllerBounds.bottom)
                {
                    m_impl->LastControllerBounds = rc;
                    m_impl->Controller->put_Bounds(rc);
                }
            }
        }
#endif

        // 1. Process any incoming JSON messages from React/TS
        std::vector<std::string> incomingMessages;
        {
            std::lock_guard<std::mutex> lock(m_impl->IncomingMessagesMutex);
            incomingMessages.swap(m_impl->PendingIncomingJson);
        }

        for (const auto& rawJson : incomingMessages)
        {
            (void)ProcessFrontendJsonMessage(rawJson, activeScene);
        }

        // 2. Poll any incoming binary packets from React/TS (e.g., Transform gizmo drag via Float32Array)
        m_impl->IpcBridge->PollIncomingPackets(nullptr);
        m_impl->IpcBridge->ApplyIncomingTransformMutations(activeScene);

        // 3. Stream updated tracked entity TransformComponent & engine telemetry to JS Binary Ring Buffer
        m_impl->IpcBridge->ClearOutboundBatch();
        if (const auto* tr = activeScene.Transforms().TryGet(trackedEntity))
        {
            m_impl->IpcBridge->PushTransformUpdate(trackedEntity, *tr);

            // Stream live transform to React Inspector as JSON
            const Vec3 eulerDeg = ToEulerDegrees(tr->Rotation);
            nlohmann::json trObj = {
                { "type", "transform_sync" },
                { "entityId", trackedEntity.Index },
                { "position", { tr->Position.x, tr->Position.y, tr->Position.z } },
                { "rotation", { eulerDeg.x, eulerDeg.y, eulerDeg.z } },
                { "scale", { tr->Scale.x, tr->Scale.y, tr->Scale.z } }
            };
            PostJsonToFrontend(trObj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
        m_impl->IpcBridge->PushTelemetry(telemetry);
        m_impl->Stats.BinaryIPCPacketsFlushed = m_impl->IpcBridge->GetStats().PacketsSentToJS;

        // Stream live telemetry to React TopBar / StatusBar as JSON
        const PhysicsTelemetrySnapshot& pt = m_impl->PhysicsTelemetry;
        nlohmann::json telemObj = {
            { "type", "telemetry_sync" },
            { "fps", telemetry.Fps },
            { "frameTimeMs", telemetry.FrameTimeMs },
            { "aliveEntities", telemetry.AliveEntities },
            { "physicsActiveBodies", telemetry.PhysicsActiveBodies },
            { "physicsSleepingBodies", pt.SleepingBodies },
            { "physicsDynamicBodies", pt.DynamicBodies },
            { "physicsKinematicBodies", pt.KinematicBodies },
            { "physicsStaticBodies", pt.StaticBodies },
            { "physicsTotalBodies", pt.TotalBodies },
            { "physicsJoints", pt.ActiveJoints },
            { "physicsCharacters", pt.Characters },
            { "physicsCollisions", pt.CollisionPairs },
            { "physicsTriggers", pt.TriggerPairs },
            { "physicsSubsteps", pt.Substeps },
            { "physicsQueries", pt.Queries },
            { "physicsSimMs", pt.SimulationTimeMs },
            { "physicsDebugDraw", m_impl->PhysicsDebugDrawEnabled },
            { "hotReloadGeneration", telemetry.HotReloadGeneration },
            { "renderGraphPasses", telemetry.RenderGraphPasses },
            { "aliasedTexturesReused", telemetry.AliasedTexturesReused },
            { "audioVoicesPlayed", telemetry.AudioVoicesPlayed }
        };
        PostJsonToFrontend(telemObj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));

        // 4. Flush queued log messages to React Console
        std::vector<std::string> logsToSend;
        {
            std::lock_guard<std::mutex> lock(m_impl->LogQueueMutex);
            logsToSend.swap(m_impl->PendingLogsJson);
        }
        for (const auto& logEntry : logsToSend)
        {
            PostJsonToFrontend(logEntry);
        }
    }

    void WebView2Compositor::PostJsonToFrontend(std::string_view jsonPayload)
    {
#if defined(_WIN32)
        if (m_impl && m_impl->WebView != nullptr && m_impl->WebViewReady)
        {
            const std::wstring wide = Utf8ToWide(jsonPayload);
            m_impl->WebView->PostWebMessageAsJson(wide.c_str());
        }
#else
        (void)jsonPayload;
#endif
    }

    void WebView2Compositor::SyncSceneData(const Scene::Scene& scene, std::string_view sceneName)
    {
        if (!m_impl) return;

        nlohmann::json j;
        j["type"] = "scene_sync";
        j["sceneName"] = sceneName;
        j["entities"] = nlohmann::json::array();

        const auto& trEntities = scene.Transforms().RawEntities();
        const auto& trComps = scene.Transforms().RawComponents();

        for (usize i = 0; i < trEntities.size(); ++i)
        {
            const Scene::EntityId eid = trEntities[i];
            const auto& tr = trComps[i];

            std::string entityName = "Entity_" + std::to_string(eid.Index);
            std::string iconType = "cube";
            std::string tag = "Default";

            const Vec3 eulerDeg = ToEulerDegrees(tr.Rotation);
            nlohmann::json compData;
            compData["transform"] = {
                { "position", { tr.Position.x, tr.Position.y, tr.Position.z } },
                { "rotation", { eulerDeg.x, eulerDeg.y, eulerDeg.z } },
                { "scale", { tr.Scale.x, tr.Scale.y, tr.Scale.z } }
            };
            compData["tag"] = tag;
            compData["layer"] = "Default";

            const std::string_view sceneEntName = scene.GetEntityName(eid);
            if (!sceneEntName.empty())
            {
                entityName = std::string(sceneEntName);
            }

            if (const auto* cam = scene.Cameras().TryGet(eid))
            {
                iconType = "camera";
                tag = "MainCamera";
                compData["camera"] = {
                    { "fov", cam->FovDegrees },
                    { "near", cam->NearPlane },
                    { "far", cam->FarPlane },
                    { "aspect", cam->AspectRatio },
                    { "isActive", cam->IsActive != 0 }
                };
            }

            if (const auto* mr = scene.MeshRenderers().TryGet(eid))
            {
                const bool isCube = (mr->MeshAssetId == Scene::kProceduralCubeMeshId);
                const bool isCameraMesh = (mr->MeshAssetId == Scene::kProceduralCameraMeshId);
                if (isCameraMesh)
                {
                    iconType = "camera";
                }
                else if (isCube)
                {
                    iconType = "cube";
                }
                else
                {
                    iconType = "avatar";
                }
                std::string meshName = isCube ? "Procedural Unit Cube" :
                                      (isCameraMesh ? "Procedural Camera Actor" : "3D Mesh");
                if (mr->MeshAssetId == 459202948264581002ull) meshName = "Remy.fbx";
                compData["meshRenderer"] = {
                    { "meshAssetId", mr->MeshAssetId },
                    { "submeshIndex", mr->SubmeshIndex },
                    { "flags", mr->Flags },
                    { "name", meshName }
                };
            }

            if (const auto* anim = scene.Animators().TryGet(eid))
            {
                compData["animator"] = {
                    { "clipName", (anim->CurrentClip && !anim->CurrentClip->Name.empty()) ? anim->CurrentClip->Name : "Idle.fbx" },
                    { "isPlaying", anim->IsPlaying },
                    { "playbackSpeed", anim->PlaybackSpeed },
                    { "currentState", anim->IsPlaying ? "Playing" : "Paused" }
                };
            }

            if (const auto* lgt = scene.Lights().TryGet(eid))
            {
                const std::string lgtType = (lgt->Type == Scene::LightType::Directional) ? "Directional" :
                                            (lgt->Type == Scene::LightType::Point ? "Point" : "Spot");
                iconType = "light";
                tag = "Light";
                compData["light"] = {
                    { "type", lgtType },
                    { "intensity", lgt->Intensity },
                    { "range", lgt->Range },
                    { "color", { lgt->Color.x, lgt->Color.y, lgt->Color.z } }
                };
            }

            if (const auto* sc = scene.Scripts().TryGet(eid))
            {
                if (sc->Enabled)
                {
                    compData["script"] = {
                        { "className", "PlayerController" },
                        { "namespace", "Morbi.Engine" }
                    };
                }
            }

            if (const auto* rb = scene.RigidBodies().TryGet(eid))
            {
                const char* motionStr = (rb->MotionType == Scene::RigidBodyMotionType::Static) ? "Static" :
                                        (rb->MotionType == Scene::RigidBodyMotionType::Kinematic ? "Kinematic" : "Dynamic");
                const char* shapeStr = (rb->Shape == Scene::ColliderShapeType::Sphere) ? "Sphere" :
                                       (rb->Shape == Scene::ColliderShapeType::Capsule ? "Capsule" : "Box");
                const Morbi::Math::Vec3 colRotDeg = ToEulerDegrees(rb->ColliderRotation);
                compData["rigidbody"] = {
                    { "motionType", motionStr },
                    { "shape", shapeStr },
                    { "mass", rb->Mass },
                    { "density", rb->Density },
                    { "useDensity", rb->UseDensity != 0 },
                    { "friction", rb->Friction },
                    { "restitution", rb->Restitution },
                    { "linearDamping", rb->LinearDamping },
                    { "angularDamping", rb->AngularDamping },
                    { "useGravity", rb->UseGravity != 0 },
                    { "gravityFactor", rb->GravityFactor },
                    { "useCCD", rb->UseCCD != 0 },
                    { "isTrigger", rb->IsTrigger != 0 },
                    { "isAwake", rb->IsAwake != 0 },
                    { "collisionLayer", rb->CollisionLayer },
                    { "collisionMask", rb->CollisionMask },
                    { "lockedAxes", rb->LockedAxes },
                    { "halfExtents", { rb->HalfExtents.x, rb->HalfExtents.y, rb->HalfExtents.z } },
                    { "colliderOffset", { rb->ColliderOffset.x, rb->ColliderOffset.y, rb->ColliderOffset.z } },
                    { "colliderRotationEuler", { colRotDeg.x, colRotDeg.y, colRotDeg.z } }
                };
            }

            if (const auto ccList = scene.GetCompoundColliders(eid); !ccList.empty())
            {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& entry : ccList)
                {
                    const char* subSh = (entry.Shape == Scene::ColliderShapeType::Sphere) ? "Sphere" :
                                        (entry.Shape == Scene::ColliderShapeType::Capsule ? "Capsule" : "Box");
                    const Morbi::Math::Vec3 subDeg = ToEulerDegrees(entry.LocalRotation);
                    arr.push_back({
                        { "shape", subSh },
                        { "halfExtents", { entry.HalfExtents.x, entry.HalfExtents.y, entry.HalfExtents.z } },
                        { "localOffset", { entry.LocalOffset.x, entry.LocalOffset.y, entry.LocalOffset.z } },
                        { "localRotationEuler", { subDeg.x, subDeg.y, subDeg.z } },
                        { "collisionLayer", entry.CollisionLayer },
                        { "collisionMask", entry.CollisionMask },
                        { "isTrigger", entry.IsTrigger != 0 }
                    });
                }
                compData["compoundColliders"] = std::move(arr);
            }

            if (const auto jList = scene.GetEntityJoints(eid); !jList.empty())
            {
                nlohmann::json jArr = nlohmann::json::array();
                for (const auto& jt : jList)
                {
                    const char* jtStr = "Fixed";
                    switch (jt.Type)
                    {
                    case Scene::PhysicsJointType::Distance: jtStr = "Distance"; break;
                    case Scene::PhysicsJointType::Hinge:    jtStr = "Hinge"; break;
                    case Scene::PhysicsJointType::Slider:   jtStr = "Slider"; break;
                    case Scene::PhysicsJointType::Fixed:
                    default:                                jtStr = "Fixed"; break;
                    }
                    const bool hasConn = jt.ConnectedEntity.IsValid() && scene.IsAlive(jt.ConnectedEntity);
                    jArr.push_back({
                        { "type", jtStr },
                        { "connectedEntityId", hasConn ? static_cast<i64>(jt.ConnectedEntity.Index) : -1LL },
                        { "connectedEntityName", hasConn ? std::string(scene.GetEntityName(jt.ConnectedEntity)) : std::string("World") },
                        { "anchorA", { jt.AnchorA.x, jt.AnchorA.y, jt.AnchorA.z } },
                        { "anchorB", { jt.AnchorB.x, jt.AnchorB.y, jt.AnchorB.z } },
                        { "axis", { jt.Axis.x, jt.Axis.y, jt.Axis.z } },
                        { "autoDetectAnchors", jt.AutoDetectAnchors },
                        { "minDistance", jt.MinDistance },
                        { "maxDistance", jt.MaxDistance },
                        { "springFrequencyHz", jt.SpringFrequencyHz },
                        { "springDampingRatio", jt.SpringDampingRatio },
                        { "minLimit", jt.MinLimit },
                        { "maxLimit", jt.MaxLimit },
                        { "maxFriction", jt.MaxFriction },
                        { "breakForce", jt.BreakForce },
                        { "breakTorque", jt.BreakTorque },
                        { "enabled", jt.Enabled },
                        { "broken", jt.Broken }
                    });
                }
                compData["physicsJoints"] = std::move(jArr);
            }

            if (const auto* cc = scene.CharacterControllers().TryGet(eid))
            {
                compData["characterController"] = {
                    { "radius", cc->Radius },
                    { "height", cc->Height },
                    { "mass", cc->Mass },
                    { "maxPushStrength", cc->MaxPushStrength },
                    { "stepHeight", cc->StepHeight },
                    { "maxSlopeAngleDegrees", cc->MaxSlopeAngleDegrees },
                    { "gravityFactor", cc->GravityFactor },
                    { "airControl", cc->AirControl },
                    { "isGrounded", cc->IsGrounded != 0 },
                    { "isSliding", cc->IsSliding != 0 }
                };
            }

            if (const auto* audio = scene.AudioSources().TryGet(eid))
            {
                compData["audioSource"] = {
                    { "volume", audio->Volume },
                    { "pitch", audio->Pitch },
                    { "spatial3D", audio->Spatial3D != 0 },
                    { "looping", audio->Looping != 0 }
                };
            }

            if (entityName.find("Camera") != std::string::npos || entityName.find("camera") != std::string::npos)
            {
                iconType = "camera";
                tag = "MainCamera";
            }
            else if (entityName.find("Player") != std::string::npos || entityName.find("player") != std::string::npos ||
                     entityName.find("Remy") != std::string::npos || entityName.find("remy") != std::string::npos)
            {
                iconType = "avatar";
                tag = "PlayerModel";
            }
            else if (entityName.find("Light") != std::string::npos || entityName.find("light") != std::string::npos)
            {
                iconType = "light";
                tag = "Lighting";
            }
            else if (entityName.find("Cube") != std::string::npos || entityName.find("cube") != std::string::npos)
            {
                iconType = "cube";
                tag = "Object";
            }
            else if (eid.Index == 0)
            {
                iconType = "pivot";
                tag = "SceneRoot";
            }
            else if (eid.Index == 1)
            {
                iconType = "cube";
                tag = "Environment";
            }
            else if (eid.Index == 2)
            {
                iconType = "pivot";
                tag = "Satellite";
            }

            nlohmann::json entityObj;
            entityObj["id"] = "entity_" + std::to_string(eid.Index);
            entityObj["entityId"] = eid.Index;
            entityObj["name"] = entityName;
            entityObj["type"] = "entity";
            entityObj["iconType"] = iconType;
            entityObj["componentData"] = compData;

            j["entities"].push_back(entityObj);
        }

        m_impl->CachedSceneSyncJson = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PostJsonToFrontend(m_impl->CachedSceneSyncJson);
    }

    void WebView2Compositor::SyncAssetData(const std::filesystem::path& assetsDir)
    {
        if (!m_impl) return;

        nlohmann::json j;
        j["type"] = "assets_sync";
        j["folders"] = { "Models", "Textures", "Scenes", "Scripts" };
        j["assets"] = nlohmann::json::array();

        if (std::filesystem::exists(assetsDir))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir))
            {
                if (entry.is_regular_file())
                {
                    const auto ext = entry.path().extension().string();
                    std::string type = "file";
                    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") type = "model";
                    else if (ext == ".png" || ext == ".jpg" || ext == ".tga") type = "texture";
                    else if (ext == ".json" || ext == ".mscene") type = "scene";
                    else if (ext == ".cs") type = "script";

                    const auto rel = std::filesystem::relative(entry.path(), assetsDir);
                    nlohmann::json item;
#if defined(_WIN32)
                    item["id"] = WideToUtf8(rel.generic_wstring());
                    item["name"] = WideToUtf8(entry.path().filename().wstring());
                    item["type"] = type;
                    item["path"] = "assets://" + WideToUtf8(rel.generic_wstring());
#else
                    item["id"] = rel.generic_string();
                    item["name"] = entry.path().filename().string();
                    item["type"] = type;
                    item["path"] = "assets://" + rel.generic_string();
#endif
                    item["sizeBytes"] = entry.file_size();
                    j["assets"].push_back(item);
                }
            }
        }

        m_impl->CachedAssetSyncJson = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        PostJsonToFrontend(m_impl->CachedAssetSyncJson);
    }

    void WebView2Compositor::PushLogMessage(std::string_view level, std::string_view category, std::string_view message, std::string_view timestamp)
    {
        if (!m_impl) return;

        try
        {
            nlohmann::json j;
            j["type"] = "log_entry";
#if defined(_WIN32)
            j["level"] = EnsureValidUtf8(level);
            j["category"] = EnsureValidUtf8(category);
            j["message"] = EnsureValidUtf8(message);
            j["timestamp"] = EnsureValidUtf8(timestamp);
#else
            j["level"] = level;
            j["category"] = category;
            j["message"] = message;
            j["timestamp"] = timestamp;
#endif

            std::string serialized = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            std::lock_guard<std::mutex> lock(m_impl->LogQueueMutex);
            if (m_impl->PendingLogsJson.size() > 200)
            {
                m_impl->PendingLogsJson.erase(m_impl->PendingLogsJson.begin());
            }
            m_impl->PendingLogsJson.push_back(std::move(serialized));
        }
        catch (...)
        {
        }
    }

    EditorPlayMode WebView2Compositor::GetPlayMode() const noexcept
    {
        return m_impl ? m_impl->CurrentPlayMode : EditorPlayMode::Edit;
    }

    void WebView2Compositor::SetPlayMode(EditorPlayMode mode) noexcept
    {
        if (!m_impl) return;
        m_impl->CurrentPlayMode = mode;
        m_impl->SimulationPaused = (mode == EditorPlayMode::Paused);

        const char* modeStr = (mode == EditorPlayMode::Playing) ? "playing" :
                              ((mode == EditorPlayMode::Paused) ? "paused" : "edit");
        nlohmann::json modeObj = {
            { "type", "play_mode_sync" },
            { "playMode", modeStr }
        };
        PostJsonToFrontend(modeObj.dump());
    }

    bool WebView2Compositor::ConsumePlayModeTransition(EditorPlayMode& outRequestedMode) noexcept
    {
        if (!m_impl || !m_impl->PlayModeTransitionPending)
        {
            return false;
        }
        m_impl->PlayModeTransitionPending = false;
        outRequestedMode = m_impl->RequestedPlayMode;
        return true;
    }

    bool WebView2Compositor::IsSimulationPaused() const noexcept
    {
        return m_impl ? m_impl->SimulationPaused : false;
    }

    void WebView2Compositor::SetSimulationPaused(bool paused) noexcept
    {
        if (m_impl) m_impl->SimulationPaused = paused;
    }

    bool WebView2Compositor::ConsumeStepRequested() noexcept
    {
        if (!m_impl) return false;
        const bool req = m_impl->StepRequested;
        m_impl->StepRequested = false;
        return req;
    }

    bool WebView2Compositor::ConsumeAssetInstantiation(AssetInstantiationRequest& outReq) noexcept
    {
        if (!m_impl || m_impl->PendingAssetInstantiations.empty()) return false;
        outReq = std::move(m_impl->PendingAssetInstantiations.front());
        m_impl->PendingAssetInstantiations.erase(m_impl->PendingAssetInstantiations.begin());
        return true;
    }

    bool WebView2Compositor::ConsumeEntityCommand(EntityCommandRequest& outReq) noexcept
    {
        if (!m_impl || m_impl->PendingEntityCommands.empty()) return false;
        outReq = std::move(m_impl->PendingEntityCommands.front());
        m_impl->PendingEntityCommands.erase(m_impl->PendingEntityCommands.begin());
        return true;
    }

#if defined(_WIN32)
    namespace
    {
        bool ShowNativeFileDialog(
            HWND ownerHwnd,
            const wchar_t* filter,
            const wchar_t* title,
            const wchar_t* defaultExt,
            DWORD flags,
            bool isSaveDialog,
            std::string& outUtf8Path) noexcept
        {
            wchar_t szFile[2048] = { 0 };
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ownerHwnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = filter;
            ofn.nFilterIndex = 1;
            ofn.lpstrTitle = title;
            ofn.lpstrDefExt = defaultExt;
            ofn.Flags = flags | OFN_EXPLORER | OFN_ENABLESIZING;

            std::error_code cwdEc;
            const auto prevCwd = std::filesystem::current_path(cwdEc);
            const BOOL accepted = isSaveDialog ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
            if (!cwdEc && !prevCwd.empty())
            {
                std::filesystem::current_path(prevCwd, cwdEc);
            }
            if (!accepted)
            {
                return false;
            }

            const auto u8 = std::filesystem::path(szFile).generic_u8string();
            outUtf8Path.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
            return !outUtf8Path.empty();
        }
    }
#endif

    bool WebView2Compositor::ConsumeNewSceneRequest() noexcept
    {
        if (!m_impl || !m_impl->NewScenePending) return false;
        m_impl->NewScenePending = false;
        return true;
    }

    bool WebView2Compositor::ConsumeCompileScriptsRequest() noexcept
    {
        if (!m_impl || !m_impl->CompileScriptsPending) return false;
        m_impl->CompileScriptsPending = false;
        return true;
    }

    bool WebView2Compositor::ConsumeBuildGameRequest() noexcept
    {
        if (!m_impl || !m_impl->BuildGamePending) return false;
        m_impl->BuildGamePending = false;
        return true;
    }

    bool WebView2Compositor::ConsumeLoadSceneRequest(std::string& outPath) noexcept
    {
        if (!m_impl) return false;

        if (m_impl->TriggerNativeOpenSceneDialog)
        {
            m_impl->TriggerNativeOpenSceneDialog = false;
            TriggerOpenSceneDialog();
        }

        if (!m_impl->LoadScenePending) return false;
        m_impl->LoadScenePending = false;
        outPath = std::move(m_impl->PendingLoadScenePath);
        m_impl->PendingLoadScenePath.clear();
        return !outPath.empty();
    }

    bool WebView2Compositor::ConsumeSaveSceneRequest(std::string& outPath) noexcept
    {
        if (!m_impl) return false;

        if (m_impl->TriggerNativeSaveSceneDialog)
        {
            m_impl->TriggerNativeSaveSceneDialog = false;
            TriggerSaveSceneAsDialog();
        }

        if (!m_impl->SaveScenePending) return false;
        m_impl->SaveScenePending = false;
        outPath = std::move(m_impl->PendingSaveScenePath);
        m_impl->PendingSaveScenePath.clear();
        return true;
    }

    void WebView2Compositor::TriggerOpenSceneDialog() noexcept
    {
#if defined(_WIN32)
        if (!m_impl) return;
        std::string chosenPath;
        const HWND owner = m_impl->MainHwnd ? m_impl->MainHwnd : m_impl->ParentHwnd;
        if (ShowNativeFileDialog(
                owner,
                L"Morbi Scene (*.mscene.json;*.json)\0*.mscene.json;*.json\0All Files (*.*)\0*.*\0",
                L"Open MorbiEngine Scene",
                L"json",
                OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR,
                false,
                chosenPath))
        {
            m_impl->PendingLoadScenePath = std::move(chosenPath);
            m_impl->LoadScenePending = true;
            Core::Log::Info("Editor", "Native File Dialog selected scene: " + m_impl->PendingLoadScenePath);
        }
#endif
    }

    void WebView2Compositor::TriggerSaveSceneAsDialog() noexcept
    {
#if defined(_WIN32)
        if (!m_impl) return;
        std::string chosenPath;
        const HWND owner = m_impl->MainHwnd ? m_impl->MainHwnd : m_impl->ParentHwnd;
        if (ShowNativeFileDialog(
                owner,
                L"Morbi Scene (*.mscene.json)\0*.mscene.json\0JSON Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0",
                L"Save MorbiEngine Scene As",
                L"json",
                OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR,
                true,
                chosenPath))
        {
            constexpr std::string_view kSceneExt = ".mscene.json";
            constexpr std::string_view kJsonExt = ".json";
            if (!chosenPath.ends_with(kSceneExt))
            {
                if (chosenPath.ends_with(kJsonExt))
                {
                    chosenPath.erase(chosenPath.size() - kJsonExt.size());
                }
                chosenPath += kSceneExt;
            }
            m_impl->PendingSaveScenePath = std::move(chosenPath);
            m_impl->SaveScenePending = true;
            Core::Log::Info("Editor", "Native Save Dialog target scene: " + m_impl->PendingSaveScenePath);
        }
#endif
    }

    bool WebView2Compositor::ConsumeImportModelRequest(std::string& outPath) noexcept
    {
        if (!m_impl) return false;

        if (m_impl->TriggerNativeOpenModelDialog)
        {
            m_impl->TriggerNativeOpenModelDialog = false;
            TriggerImportModelDialog();
        }

        std::scoped_lock lock(m_impl->ModelImportMutex);
        if (!m_impl->PendingImportModelPaths.empty())
        {
            outPath = std::move(m_impl->PendingImportModelPaths.front());
            m_impl->PendingImportModelPaths.erase(m_impl->PendingImportModelPaths.begin());
            return true;
        }
        return false;
    }

    void WebView2Compositor::QueueExternalModelImport(std::string_view path)
    {
        if (!m_impl || path.empty()) return;
        std::scoped_lock lock(m_impl->ModelImportMutex);
        m_impl->PendingImportModelPaths.emplace_back(path);
        Core::Log::Info("Editor", "Queued external model import: " + std::string(path));
    }

    void WebView2Compositor::TriggerImportModelDialog() noexcept
    {
#if defined(_WIN32)
        if (!m_impl) return;
        std::string chosenPath;
        const HWND owner = m_impl->MainHwnd ? m_impl->MainHwnd : m_impl->ParentHwnd;
        if (ShowNativeFileDialog(
                owner,
                L"All 3D Models (*.fbx;*.obj;*.gltf;*.glb;*.dae;*.stl;*.blend;*.3ds;*.ply)\0*.fbx;*.obj;*.gltf;*.glb;*.dae;*.stl;*.blend;*.3ds;*.ply\0Autodesk FBX (*.fbx)\0*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF / GLB (*.gltf;*.glb)\0*.gltf;*.glb\0Collada DAE (*.dae)\0*.dae\0Stereolithography STL (*.stl)\0*.stl\0All Files (*.*)\0*.*\0",
                L"Select 3D Model to Import into MorbiEngine",
                nullptr,
                OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR,
                false,
                chosenPath))
        {
            QueueExternalModelImport(chosenPath);
            Core::Log::Info("Editor", "Native File Dialog selected model: " + chosenPath);
        }
#endif
    }

    std::string WebView2Compositor::GetActiveGizmoTool() const noexcept
    {
        return m_impl ? m_impl->ActiveGizmoTool : "translate";
    }

    std::string WebView2Compositor::GetActiveViewportTab() const noexcept
    {
        return m_impl ? m_impl->ActiveViewportTab : "Scene";
    }

    void WebView2Compositor::SetPhysicsTelemetry(const PhysicsTelemetrySnapshot& stats) noexcept
    {
        if (m_impl)
        {
            m_impl->PhysicsTelemetry = stats;
        }
    }

    bool WebView2Compositor::ConsumePhysicsSyncEntity(Scene::EntityId& outEntity) noexcept
    {
        if (!m_impl || m_impl->PendingPhysicsSyncEntities.empty())
        {
            return false;
        }
        outEntity = m_impl->PendingPhysicsSyncEntities.front();
        m_impl->PendingPhysicsSyncEntities.erase(m_impl->PendingPhysicsSyncEntities.begin());
        return true;
    }

    const std::string& WebView2Compositor::GetCachedSceneSyncJson() const noexcept
    {
        static const std::string kEmpty;
        return m_impl ? m_impl->CachedSceneSyncJson : kEmpty;
    }

    bool WebView2Compositor::ProcessFrontendJsonMessage(std::string_view jsonPayload, Scene::Scene& activeScene)
    {
        if (!m_impl || jsonPayload.empty())
        {
            return false;
        }
        try
        {
            const auto j = nlohmann::json::parse(jsonPayload);
            const std::string type = j.value("type", "");

            if (type == "update_transform")
            {
                const auto entityIdx = static_cast<u32>(j.value("entityId", 0ull));
                for (const auto& ent : activeScene.Transforms().RawEntities())
                {
                    if (ent.Index != entityIdx) continue;
                    if (auto* tr = activeScene.Transforms().TryGet(ent))
                    {
                        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 3)
                        {
                            tr->Position.x = j["position"][0].get<f32>();
                            tr->Position.y = j["position"][1].get<f32>();
                            tr->Position.z = j["position"][2].get<f32>();
                        }
                        if (j.contains("rotation") && j["rotation"].is_array() && j["rotation"].size() >= 3)
                        {
                            tr->Rotation = FromEulerDegrees({
                                j["rotation"][0].get<f32>(),
                                j["rotation"][1].get<f32>(),
                                j["rotation"][2].get<f32>()
                            });
                        }
                        if (j.contains("scale") && j["scale"].is_array() && j["scale"].size() >= 3)
                        {
                            tr->Scale.x = j["scale"][0].get<f32>();
                            tr->Scale.y = j["scale"][1].get<f32>();
                            tr->Scale.z = j["scale"][2].get<f32>();
                        }
                        if (auto* rb = activeScene.RigidBodies().TryGet(ent))
                        {
                            rb->DirtyFlags |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Transform);
                        }
                        if (m_impl->IpcBridge)
                        {
                            m_impl->IpcBridge->PushTransformUpdate(ent, *tr);
                        }
                    }
                    break;
                }
            }
            else if (type == "select_entity")
            {
                const auto entityIdx = static_cast<u32>(j.value("entityId", 0ull));
                for (const auto& ent : activeScene.Transforms().RawEntities())
                {
                    if (ent.Index == entityIdx && m_impl->IpcBridge)
                    {
                        m_impl->IpcBridge->SetSelectedEntity(ent);
                        m_impl->IpcBridge->PushEntitySelected(ent);
                        break;
                    }
                }
            }
            else if (type == "play")
            {
                m_impl->RequestedPlayMode = EditorPlayMode::Playing;
                m_impl->PlayModeTransitionPending = true;
#if defined(_WIN32)
                if (m_impl->ParentHwnd != nullptr) SetFocus(m_impl->ParentHwnd);
#endif
                Core::Log::Info("Editor", "Received 'play' command from transport UI");
            }
            else if (type == "pause")
            {
                m_impl->RequestedPlayMode = EditorPlayMode::Paused;
                m_impl->PlayModeTransitionPending = true;
                Core::Log::Info("Editor", "Received 'pause' command from transport UI");
            }
            else if (type == "stop")
            {
                m_impl->RequestedPlayMode = EditorPlayMode::Edit;
                m_impl->PlayModeTransitionPending = true;
                Core::Log::Info("Editor", "Received 'stop' command from transport UI");
            }
            else if (type == "play_pause")
            {
                const bool paused = j.value("paused", false);
                m_impl->RequestedPlayMode = paused ? EditorPlayMode::Paused : EditorPlayMode::Playing;
                m_impl->PlayModeTransitionPending = true;
#if defined(_WIN32)
                if (!paused && m_impl->ParentHwnd != nullptr) SetFocus(m_impl->ParentHwnd);
#endif
                Core::Log::Info("Editor", m_impl->RequestedPlayMode == EditorPlayMode::Paused ? "Simulation Paused via Editor Transport" : "Simulation Resumed via Editor Transport");
            }
            else if (type == "step")
            {
                m_impl->StepRequested = true;
                Core::Log::Info("Editor", "Simulation Step Requested");
            }
            else if (type == "request_sync")
            {
                if (!m_impl->CachedSceneSyncJson.empty()) PostJsonToFrontend(m_impl->CachedSceneSyncJson);
                if (!m_impl->CachedAssetSyncJson.empty()) PostJsonToFrontend(m_impl->CachedAssetSyncJson);
                const char* modeStr = (m_impl->CurrentPlayMode == EditorPlayMode::Playing) ? "playing" :
                                      ((m_impl->CurrentPlayMode == EditorPlayMode::Paused) ? "paused" : "edit");
                nlohmann::json modeObj = { { "type", "play_mode_sync" }, { "playMode", modeStr } };
                PostJsonToFrontend(modeObj.dump());
            }
            else if (type == "viewport_rect")
            {
                m_impl->ActiveViewport.X = j.value("x", m_impl->ActiveViewport.X);
                m_impl->ActiveViewport.Y = j.value("y", m_impl->ActiveViewport.Y);
                m_impl->ActiveViewport.Width = j.value("width", m_impl->ActiveViewport.Width);
                m_impl->ActiveViewport.Height = j.value("height", m_impl->ActiveViewport.Height);
#if defined(_WIN32)
                m_impl->ApplyViewportHole();
#endif
            }
            else if (type == "menu_state")
            {
                m_impl->MenuOpen = j.value("open", false);
                m_impl->MenuRect.X = j.value("x", 0);
                m_impl->MenuRect.Y = j.value("y", 0);
                m_impl->MenuRect.Width = j.value("width", 0u);
                m_impl->MenuRect.Height = j.value("height", 0u);
                m_impl->MenuRects.clear();
                if (j.contains("rects") && j["rects"].is_array())
                {
                    for (const auto& rItem : j["rects"])
                    {
                        if (!rItem.is_object()) continue;
                        ViewportRect r{};
                        r.X = rItem.value("x", 0);
                        r.Y = rItem.value("y", 0);
                        r.Width = rItem.value("width", 0u);
                        r.Height = rItem.value("height", 0u);
                        if (r.Width > 0 && r.Height > 0) m_impl->MenuRects.push_back(r);
                    }
                }
                if (m_impl->MenuRects.empty() && m_impl->MenuOpen && m_impl->MenuRect.Width > 0 && m_impl->MenuRect.Height > 0)
                {
                    m_impl->MenuRects.push_back(m_impl->MenuRect);
                }
#if defined(_WIN32)
                m_impl->ApplyViewportHole();
#endif
            }
            else if (type == "instantiate_asset")
            {
                AssetInstantiationRequest req{};
                req.AssetPath = j.value("assetPath", "");
                req.AssetName = j.value("assetName", "");
                req.NormX = j.value("normX", 0.5f);
                req.NormY = j.value("normY", 0.5f);
                m_impl->PendingAssetInstantiations.push_back(std::move(req));
            }
            else if (type == "new_scene") { m_impl->NewScenePending = true; }
            else if (type == "compile_scripts") { m_impl->CompileScriptsPending = true; }
            else if (type == "build_game") { m_impl->BuildGamePending = true; }
            else if (type == "load_scene")
            {
                const std::string scenePath = j.value("path", "");
                if (!scenePath.empty())
                {
                    m_impl->LoadScenePending = true;
                    m_impl->PendingLoadScenePath = scenePath;
                }
            }
            else if (type == "open_scene_dialog") { m_impl->TriggerNativeOpenSceneDialog = true; }
            else if (type == "save_scene")
            {
                m_impl->SaveScenePending = true;
                m_impl->PendingSaveScenePath = j.value("path", "");
            }
            else if (type == "save_scene_dialog") { m_impl->TriggerNativeSaveSceneDialog = true; }
            else if (type == "gizmo_tool") { m_impl->ActiveGizmoTool = j.value("tool", "translate"); }
            else if (type == "viewport_tab") { m_impl->ActiveViewportTab = j.value("tab", "Scene"); }
            else if (type == "set_physics_debug_draw") { m_impl->PhysicsDebugDrawEnabled = j.value("enabled", false); }
            else if (type == "update_physics_component")
            {
                const auto entityIdx = static_cast<u32>(j.value("entityId", 0ull));
                const std::string kind = j.value("componentKind", "rigidbody");
                for (const auto& ent : activeScene.Transforms().RawEntities())
                {
                    if (ent.Index != entityIdx) continue;
                    if (kind == "rigidbody")
                    {
                        if (auto* rb = activeScene.RigidBodies().TryGet(ent))
                        {
                            u8 dirty = 0u;
                            if (j.contains("motionType") && j["motionType"].is_string())
                            {
                                const std::string mt = j["motionType"].get<std::string>();
                                if (mt == "Static") rb->MotionType = Scene::RigidBodyMotionType::Static;
                                else if (mt == "Kinematic") rb->MotionType = Scene::RigidBodyMotionType::Kinematic;
                                else rb->MotionType = Scene::RigidBodyMotionType::Dynamic;
                                dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::MotionType);
                            }
                            if (j.contains("shape") && j["shape"].is_string())
                            {
                                const std::string sh = j["shape"].get<std::string>();
                                if (sh == "Sphere") rb->Shape = Scene::ColliderShapeType::Sphere;
                                else if (sh == "Capsule") rb->Shape = Scene::ColliderShapeType::Capsule;
                                else rb->Shape = Scene::ColliderShapeType::Box;
                                dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape);
                            }
                            if (j.contains("mass")) { rb->Mass = std::max(0.001f, j["mass"].get<f32>()); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("density")) { rb->Density = std::max(0.01f, j["density"].get<f32>()); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("useDensity")) { rb->UseDensity = j["useDensity"].get<bool>() ? 1u : 0u; dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("friction")) { rb->Friction = std::max(0.0f, j["friction"].get<f32>()); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("restitution")) { rb->Restitution = std::clamp(j["restitution"].get<f32>(), 0.0f, 1.0f); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("linearDamping")) { rb->LinearDamping = std::max(0.0f, j["linearDamping"].get<f32>()); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("angularDamping")) { rb->AngularDamping = std::max(0.0f, j["angularDamping"].get<f32>()); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("useGravity")) { rb->UseGravity = j["useGravity"].get<bool>() ? 1u : 0u; dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("gravityFactor")) { rb->GravityFactor = j["gravityFactor"].get<f32>(); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("useCCD")) { rb->UseCCD = j["useCCD"].get<bool>() ? 1u : 0u; dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("isTrigger")) { rb->IsTrigger = j["isTrigger"].get<bool>() ? 1u : 0u; dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("collisionLayer")) { rb->CollisionLayer = j["collisionLayer"].get<u32>(); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("collisionMask")) { rb->CollisionMask = j["collisionMask"].get<u32>(); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Properties); }
                            if (j.contains("lockedAxes")) { rb->LockedAxes = static_cast<u8>(j["lockedAxes"].get<u32>() & 0x3Fu); dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::MotionType); }
                            if (j.contains("halfExtents") && j["halfExtents"].is_array() && j["halfExtents"].size() >= 3)
                            {
                                rb->HalfExtents = {
                                    std::max(0.005f, j["halfExtents"][0].get<f32>()),
                                    std::max(0.005f, j["halfExtents"][1].get<f32>()),
                                    std::max(0.005f, j["halfExtents"][2].get<f32>())
                                };
                                dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape);
                            }
                            if (j.contains("colliderOffset") && j["colliderOffset"].is_array() && j["colliderOffset"].size() >= 3)
                            {
                                rb->ColliderOffset = {
                                    j["colliderOffset"][0].get<f32>(),
                                    j["colliderOffset"][1].get<f32>(),
                                    j["colliderOffset"][2].get<f32>()
                                };
                                dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape);
                            }
                            if (j.contains("colliderRotationEuler") && j["colliderRotationEuler"].is_array() && j["colliderRotationEuler"].size() >= 3)
                            {
                                rb->ColliderRotation = Math::FromEulerDegrees({
                                    j["colliderRotationEuler"][0].get<f32>(),
                                    j["colliderRotationEuler"][1].get<f32>(),
                                    j["colliderRotationEuler"][2].get<f32>()
                                });
                                dirty |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape);
                            }
                            rb->DirtyFlags = static_cast<u8>(rb->DirtyFlags | dirty);
                            m_impl->PendingPhysicsSyncEntities.push_back(ent);
                        }
                    }
                    else if (kind == "compoundColliders")
                    {
                        const std::string action = j.value("action", "set");
                        auto parseShapeEntry = [](const nlohmann::json& sj) {
                            Scene::CompoundColliderEntry e{};
                            const std::string sh = sj.value("shape", "Box");
                            if (sh == "Sphere") e.Shape = Scene::ColliderShapeType::Sphere;
                            else if (sh == "Capsule") e.Shape = Scene::ColliderShapeType::Capsule;
                            else e.Shape = Scene::ColliderShapeType::Box;

                            if (sj.contains("halfExtents") && sj["halfExtents"].is_array() && sj["halfExtents"].size() >= 3)
                            {
                                e.HalfExtents = {
                                    std::max(0.005f, sj["halfExtents"][0].get<f32>()),
                                    std::max(0.005f, sj["halfExtents"][1].get<f32>()),
                                    std::max(0.005f, sj["halfExtents"][2].get<f32>())
                                };
                            }
                            if (sj.contains("localOffset") && sj["localOffset"].is_array() && sj["localOffset"].size() >= 3)
                            {
                                e.LocalOffset = {
                                    sj["localOffset"][0].get<f32>(),
                                    sj["localOffset"][1].get<f32>(),
                                    sj["localOffset"][2].get<f32>()
                                };
                            }
                            if (sj.contains("localRotationEuler") && sj["localRotationEuler"].is_array() && sj["localRotationEuler"].size() >= 3)
                            {
                                e.LocalRotation = Math::FromEulerDegrees({
                                    sj["localRotationEuler"][0].get<f32>(),
                                    sj["localRotationEuler"][1].get<f32>(),
                                    sj["localRotationEuler"][2].get<f32>()
                                });
                            }
                            e.CollisionLayer = sj.value("collisionLayer", 1u);
                            e.CollisionMask = sj.value("collisionMask", 0xFFFFFFFFu);
                            e.IsTrigger = sj.value("isTrigger", false) ? 1u : 0u;
                            return e;
                        };

                        if (action == "add") activeScene.AddCompoundCollider(ent, parseShapeEntry(j));
                        else if (action == "remove") activeScene.RemoveCompoundColliderAt(ent, j.value("index", 0u));
                        else if (action == "clear") activeScene.ClearCompoundColliders(ent);
                        else if (action == "update")
                        {
                            const u32 idx = j.value("index", 0u);
                            const auto cur = activeScene.GetCompoundColliders(ent);
                            if (idx < static_cast<u32>(cur.size()))
                            {
                                std::vector<Scene::CompoundColliderEntry> next(cur.begin(), cur.end());
                                next[idx] = parseShapeEntry(j);
                                activeScene.SetCompoundColliders(ent, next);
                            }
                        }
                        else if (action == "set" && j.contains("shapes") && j["shapes"].is_array())
                        {
                            std::vector<Scene::CompoundColliderEntry> list;
                            for (const auto& item : j["shapes"])
                            {
                                if (item.is_object()) list.push_back(parseShapeEntry(item));
                            }
                            activeScene.SetCompoundColliders(ent, list);
                        }
                        if (auto* rb = activeScene.RigidBodies().TryGet(ent))
                        {
                            rb->DirtyFlags = static_cast<u8>(rb->DirtyFlags | static_cast<u8>(Scene::RigidBodyDirtyFlags::Shape));
                        }
                        m_impl->PendingPhysicsSyncEntities.push_back(ent);
                    }
                    else if (kind == "physicsJoints")
                    {
                        const std::string action = j.value("action", "update");
                        auto parseJointEntry = [&](const nlohmann::json& jj) {
                            Scene::PhysicsJointEntry e{};
                            const std::string rawType = jj.value("type", std::string(""));
                            const std::string jtStr = jj.contains("jointType")
                                ? jj.value("jointType", std::string("Hinge"))
                                : ((!rawType.empty() && rawType != "update_physics_component") ? rawType : std::string("Hinge"));
                            if (jtStr == "Fixed") e.Type = Scene::PhysicsJointType::Fixed;
                            else if (jtStr == "Distance") e.Type = Scene::PhysicsJointType::Distance;
                            else if (jtStr == "Slider") e.Type = Scene::PhysicsJointType::Slider;
                            else e.Type = Scene::PhysicsJointType::Hinge;

                            const i64 connIdx = jj.value("connectedEntityId", -1LL);
                            e.ConnectedEntity = Scene::EntityId::Invalid();
                            if (connIdx >= 0)
                            {
                                for (const auto& cand : activeScene.Transforms().RawEntities())
                                {
                                    if (static_cast<i64>(cand.Index) == connIdx && cand != ent)
                                    {
                                        e.ConnectedEntity = cand;
                                        break;
                                    }
                                }
                            }
                            if (jj.contains("anchorA") && jj["anchorA"].is_array() && jj["anchorA"].size() >= 3)
                            {
                                e.AnchorA = { jj["anchorA"][0].get<f32>(), jj["anchorA"][1].get<f32>(), jj["anchorA"][2].get<f32>() };
                            }
                            if (jj.contains("anchorB") && jj["anchorB"].is_array() && jj["anchorB"].size() >= 3)
                            {
                                e.AnchorB = { jj["anchorB"][0].get<f32>(), jj["anchorB"][1].get<f32>(), jj["anchorB"][2].get<f32>() };
                            }
                            if (jj.contains("axis") && jj["axis"].is_array() && jj["axis"].size() >= 3)
                            {
                                e.Axis = { jj["axis"][0].get<f32>(), jj["axis"][1].get<f32>(), jj["axis"][2].get<f32>() };
                            }
                            e.AutoDetectAnchors = jj.value("autoDetectAnchors", true);
                            e.MinDistance = jj.value("minDistance", -1.0f);
                            e.MaxDistance = jj.value("maxDistance", -1.0f);
                            e.SpringFrequencyHz = jj.value("springFrequencyHz", 0.0f);
                            e.SpringDampingRatio = jj.value("springDampingRatio", 0.0f);
                            e.MinLimit = jj.value("minLimit", -Math::kPi);
                            e.MaxLimit = jj.value("maxLimit", Math::kPi);
                            e.MaxFriction = jj.value("maxFriction", 0.0f);
                            e.BreakForce = jj.value("breakForce", 0.0f);
                            e.BreakTorque = jj.value("breakTorque", 0.0f);
                            e.Enabled = jj.value("enabled", true);
                            e.Broken = false;
                            return e;
                        };

                        if (action == "add") activeScene.AddEntityJoint(ent, parseJointEntry(j));
                        else if (action == "update")
                        {
                            const u32 idx = j.value("index", 0u);
                            Scene::PhysicsJointEntry updated = parseJointEntry(j);
                            const auto existing = activeScene.GetEntityJoints(ent);
                            if (idx < static_cast<u32>(existing.size()))
                            {
                                updated.RuntimeJointHandle = existing[idx].RuntimeJointHandle;
                            }
                            activeScene.UpdateEntityJointAt(ent, idx, updated);
                        }
                        else if (action == "remove") activeScene.RemoveEntityJointAt(ent, j.value("index", 0u));
                        else if (action == "clear") activeScene.ClearEntityJoints(ent);
                        m_impl->PendingPhysicsSyncEntities.push_back(ent);
                    }
                    else if (kind == "characterController")
                    {
                        if (auto* cc = activeScene.CharacterControllers().TryGet(ent))
                        {
                            u8 dirty = 0u;
                            if (j.contains("radius")) { cc->Radius = std::max(0.05f, j["radius"].get<f32>()); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Shape); }
                            if (j.contains("height")) { cc->Height = std::max(cc->Radius * 2.0f + 0.02f, j["height"].get<f32>()); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Shape); }
                            if (j.contains("mass")) { cc->Mass = std::max(0.1f, j["mass"].get<f32>()); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Properties); }
                            if (j.contains("maxPushStrength")) { cc->MaxPushStrength = std::max(0.0f, j["maxPushStrength"].get<f32>()); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Properties); }
                            if (j.contains("stepHeight")) { cc->StepHeight = std::max(0.0f, j["stepHeight"].get<f32>()); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Properties); }
                            if (j.contains("maxSlopeAngleDegrees")) { cc->MaxSlopeAngleDegrees = std::clamp(j["maxSlopeAngleDegrees"].get<f32>(), 1.0f, 89.0f); dirty |= static_cast<u8>(Scene::CharacterControllerDirtyFlags::Properties); }
                            if (j.contains("gravityFactor")) { cc->GravityFactor = j["gravityFactor"].get<f32>(); }
                            if (j.contains("airControl")) { cc->AirControl = std::clamp(j["airControl"].get<f32>(), 0.0f, 1.0f); }
                            cc->DirtyFlags = static_cast<u8>(cc->DirtyFlags | dirty);
                            m_impl->PendingPhysicsSyncEntities.push_back(ent);
                        }
                    }
                    break;
                }
            }
            else if (type == "create_entity")
            {
                EntityCommandRequest cmdReq{};
                cmdReq.Action = EntityActionKind::Create;
                cmdReq.Name = j.value("name", "NewEntity");
                cmdReq.TargetType = j.value("entityType", "empty");
                m_impl->PendingEntityCommands.push_back(std::move(cmdReq));
            }
            else if (type == "delete_entity")
            {
                EntityCommandRequest cmdReq{};
                cmdReq.Action = EntityActionKind::Delete;
                cmdReq.EntityId = static_cast<u32>(j.value("entityId", 0ull));
                m_impl->PendingEntityCommands.push_back(std::move(cmdReq));
            }
            else if (type == "rename_entity")
            {
                EntityCommandRequest cmdReq{};
                cmdReq.Action = EntityActionKind::Rename;
                cmdReq.EntityId = static_cast<u32>(j.value("entityId", 0ull));
                cmdReq.Name = j.value("name", "");
                m_impl->PendingEntityCommands.push_back(std::move(cmdReq));
            }
            else if (type == "add_component")
            {
                EntityCommandRequest cmdReq{};
                cmdReq.Action = EntityActionKind::AddComponent;
                cmdReq.EntityId = static_cast<u32>(j.value("entityId", 0ull));
                cmdReq.TargetType = j.value("componentType", "");
                m_impl->PendingEntityCommands.push_back(std::move(cmdReq));
            }
            else if (type == "duplicate_entity")
            {
                EntityCommandRequest cmdReq{};
                cmdReq.Action = EntityActionKind::Duplicate;
                cmdReq.EntityId = static_cast<u32>(j.value("entityId", 0ull));
                m_impl->PendingEntityCommands.push_back(std::move(cmdReq));
            }
            else if (type == "import_model")
            {
                const std::string filePath = j.value("filePath", "");
                if (!filePath.empty())
                {
                    std::scoped_lock lock(m_impl->ModelImportMutex);
                    m_impl->PendingImportModelPaths.push_back(filePath);
                }
            }
            else if (type == "open_import_dialog") { m_impl->TriggerNativeOpenModelDialog = true; }
            else if (type == "console_command")
            {
                const std::string rawCmd = j.value("command", "");
                if (!rawCmd.empty() && m_impl->IpcBridge)
                {
                    BinaryPacketHeader hdr{};
                    hdr.Opcode = static_cast<u16>(BinaryIPCOpcode::ExecuteCommand);
                    hdr.PayloadSizeBytes = static_cast<u32>(rawCmd.size());
                    std::vector<u8> pkt(sizeof(BinaryPacketHeader) + rawCmd.size());
                    std::memcpy(pkt.data(), &hdr, sizeof(BinaryPacketHeader));
                    std::memcpy(pkt.data() + sizeof(BinaryPacketHeader), rawCmd.data(), rawCmd.size());
                    m_impl->IpcBridge->EnqueueIncomingBinaryFrame(pkt);
                }
            }
            return true;
        }
        catch (const std::exception& ex)
        {
            Core::Log::Warn("UI.WebView2", std::string("JSON parse error from frontend: ") + ex.what());
            return false;
        }
    }

    bool WebView2Compositor::IsPhysicsDebugDrawEnabled() const noexcept
    {
        return m_impl ? m_impl->PhysicsDebugDrawEnabled : false;
    }

    void WebView2Compositor::SetPhysicsDebugDrawEnabled(bool enabled) noexcept
    {
        if (m_impl)
        {
            m_impl->PhysicsDebugDrawEnabled = enabled;
        }
    }

    void WebView2Compositor::EndFrameComposite()
    {
        if (!m_impl || !m_impl->Initialized)
        {
            return;
        }

#if defined(_WIN32)
        if (m_impl->DCompDevice != nullptr)
        {
            const HRESULT commitHr = m_impl->DCompDevice->Commit();
            if (FAILED(commitHr))
            {
                Core::Log::Warn("UI.WebView2.DComp", "DCompDevice->Commit() failed: HRESULT=" + std::to_string(commitHr));
            }
        }
#endif
        ++m_impl->Stats.ComposedFrames;
    }

    RHI::TextureHandle WebView2Compositor::GetSharedViewportTexture() const noexcept
    {
        return m_impl ? m_impl->SharedViewportTex : RHI::TextureHandle::Invalid();
    }

    RHI::BindlessResourceIndex WebView2Compositor::GetSharedViewportBindlessIndex() const noexcept
    {
        return m_impl ? m_impl->SharedViewportBindless : RHI::BindlessResourceIndex{};
    }

    ViewportRect WebView2Compositor::GetViewportRect() const noexcept
    {
        return m_impl ? m_impl->ActiveViewport : ViewportRect{};
    }

    WebView2CompositorStats WebView2Compositor::GetStats() const noexcept
    {
        return m_impl ? m_impl->Stats : WebView2CompositorStats{};
    }
}
