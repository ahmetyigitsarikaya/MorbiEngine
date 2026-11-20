#include "Morbi/Shader/ShaderCompiler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

#if !defined(_WIN32)
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "DxcRuntimeBridge.hpp"
#include "DxilReflector.hpp"
#include "SpirvReflector.hpp"
#include "Morbi/Core/Log.hpp"

namespace Morbi::Shader
{
    namespace
    {
        constexpr u64 kFnvOffsetBasis = 14695981039346656037ULL;
        constexpr u64 kFnvPrime = 1099511628211ULL;
        constexpr u32 kMaxIncludeLoadsPerFile = 32u;

        void HashBytes(u64& seed, const void* data, usize size) noexcept
        {
            const auto* bytes = static_cast<const u8*>(data);
            for (usize i = 0; i < size; ++i)
            {
                seed ^= static_cast<u64>(bytes[i]);
                seed *= kFnvPrime;
            }
        }

        template <typename T>
        void HashValue(u64& seed, const T& value) noexcept
        {
            HashBytes(seed, &value, sizeof(T));
        }

        void HashString(u64& seed, std::string_view text) noexcept
        {
            const u64 len = static_cast<u64>(text.size());
            HashValue(seed, len);
            if (!text.empty())
            {
                HashBytes(seed, text.data(), text.size());
            }
        }

        std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }
#if defined(_WIN32)
            const int needed = MultiByteToWideChar(
                CP_UTF8,
                0,
                text.data(),
                static_cast<int>(text.size()),
                nullptr,
                0);
            if (needed <= 0)
            {
                return std::wstring(text.begin(), text.end());
            }
            std::wstring out(static_cast<usize>(needed), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                0,
                text.data(),
                static_cast<int>(text.size()),
                out.data(),
                needed);
            return out;
#else
            std::wstring out;
            out.reserve(text.size());
            usize i = 0;
            while (i < text.size())
            {
                const u8 c = static_cast<u8>(text[i]);
                uint32_t cp = 0;
                usize seqLen = 1;
                if ((c & 0x80u) == 0)
                {
                    cp = c;
                }
                else if ((c & 0xE0u) == 0xC0u && i + 1 < text.size())
                {
                    cp = ((c & 0x1Fu) << 6) | (static_cast<u8>(text[i + 1]) & 0x3Fu);
                    seqLen = 2;
                }
                else if ((c & 0xF0u) == 0xE0u && i + 2 < text.size())
                {
                    cp = ((c & 0x0Fu) << 12) |
                         ((static_cast<u8>(text[i + 1]) & 0x3Fu) << 6) |
                         (static_cast<u8>(text[i + 2]) & 0x3Fu);
                    seqLen = 3;
                }
                else if ((c & 0xF8u) == 0xF0u && i + 3 < text.size())
                {
                    cp = ((c & 0x07u) << 18) |
                         ((static_cast<u8>(text[i + 1]) & 0x3Fu) << 12) |
                         ((static_cast<u8>(text[i + 2]) & 0x3Fu) << 6) |
                         (static_cast<u8>(text[i + 3]) & 0x3Fu);
                    seqLen = 4;
                }
                else
                {
                    cp = 0xFFFDu;
                }
                out.push_back(static_cast<wchar_t>(cp));
                i += seqLen;
            }
            return out;
#endif
        }

        std::string WideToUtf8(const wchar_t* wideStr)
        {
            if (wideStr == nullptr || wideStr[0] == L'\0')
            {
                return {};
            }
#if defined(_WIN32)
            const int needed = WideCharToMultiByte(
                CP_UTF8,
                0,
                wideStr,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (needed <= 1)
            {
                return {};
            }
            std::string out(static_cast<usize>(needed - 1), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wideStr,
                -1,
                out.data(),
                needed,
                nullptr,
                nullptr);
            return out;
#else
            std::string out;
            for (const wchar_t* p = wideStr; *p != L'\0'; ++p)
            {
                const uint32_t cp = static_cast<uint32_t>(*p);
                if (cp < 0x80u)
                {
                    out.push_back(static_cast<char>(cp));
                }
                else if (cp < 0x800u)
                {
                    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else if (cp < 0x10000u)
                {
                    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            return out;
#endif
        }

        std::filesystem::path CanonicalizePathBestEffort(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
            if (!ec && !canonical.empty())
            {
                return canonical.lexically_normal();
            }
            return path.lexically_normal();
        }

        std::string NormalizePathString(const std::filesystem::path& path)
        {
            std::string normalized = CanonicalizePathBestEffort(path).generic_string();
#if defined(_WIN32)
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
#endif
            return normalized;
        }

        bool RunProcessCapture(
            const std::filesystem::path& exePath,
            const std::vector<std::string>& args,
            int& outExitCode,
            std::string& outLog)
        {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE readPipe = nullptr;
            HANDLE writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
            {
                outLog = "Failed to create stdout/stderr pipe for process.";
                return false;
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = writePipe;
            si.hStdError = writePipe;

            std::wstringstream cmdStream;
            cmdStream << L"\"" << exePath.wstring() << L"\"";
            for (const auto& arg : args)
            {
                cmdStream << L" \"" << Utf8ToWide(arg) << L"\"";
            }
            std::wstring cmdLine = cmdStream.str();

            PROCESS_INFORMATION pi{};
            const std::filesystem::path workDir = exePath.parent_path();
            const BOOL created = CreateProcessW(
                exePath.c_str(),
                cmdLine.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                workDir.empty() ? nullptr : workDir.c_str(),
                &si,
                &pi);

            CloseHandle(writePipe);

            if (!created)
            {
                CloseHandle(readPipe);
                outLog = "Failed to launch process: " + exePath.string();
                return false;
            }

            char buffer[1024];
            DWORD bytesRead = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                outLog.append(buffer, bytesRead);
            }
            CloseHandle(readPipe);

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            outExitCode = static_cast<int>(exitCode);

            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
#else
            int pipeFds[2]{ -1, -1 };
            if (pipe(pipeFds) != 0)
            {
                outLog = "Failed to create stdout/stderr pipe for process.";
                return false;
            }

            const pid_t pid = fork();
            if (pid < 0)
            {
                close(pipeFds[0]);
                close(pipeFds[1]);
                outLog = "Failed to fork child process.";
                return false;
            }

            if (pid == 0)
            {
                close(pipeFds[0]);
                dup2(pipeFds[1], STDOUT_FILENO);
                dup2(pipeFds[1], STDERR_FILENO);
                close(pipeFds[1]);

                std::vector<std::string> storage;
                storage.reserve(args.size() + 1);
                storage.push_back(exePath.string());
                for (const auto& a : args)
                {
                    storage.push_back(a);
                }

                std::vector<char*> argvPtrs;
                argvPtrs.reserve(storage.size() + 1);
                for (auto& s : storage)
                {
                    argvPtrs.push_back(s.data());
                }
                argvPtrs.push_back(nullptr);

                execvp(exePath.c_str(), argvPtrs.data());
                _exit(127);
            }

            close(pipeFds[1]);
            char buffer[1024];
            ssize_t bytesRead = 0;
            while ((bytesRead = read(pipeFds[0], buffer, sizeof(buffer))) > 0)
            {
                outLog.append(buffer, static_cast<usize>(bytesRead));
            }
            close(pipeFds[0]);

            int status = 0;
            if (waitpid(pid, &status, 0) < 0)
            {
                outExitCode = -1;
                return false;
            }
            if (WIFEXITED(status))
            {
                outExitCode = WEXITSTATUS(status);
            }
            else
            {
                outExitCode = -1;
            }
            return true;
#endif
        }

        class MorbiDxcIncludeHandler final : public IDxcIncludeHandler
        {
        public:
            MorbiDxcIncludeHandler(
                IDxcUtils* utils,
                std::filesystem::path primarySourceDir,
                std::vector<std::filesystem::path> includeDirs)
                : m_utils(utils)
                , m_primarySourceDir(std::move(primarySourceDir))
                , m_includeDirs(std::move(includeDirs))
            {
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
            {
                if (!ppvObject)
                {
                    return E_POINTER;
                }
                if (riid == Detail::GetDxcInterfaceId<IUnknown>() ||
                    riid == Detail::GetDxcInterfaceId<IDxcIncludeHandler>())
                {
                    *ppvObject = static_cast<IDxcIncludeHandler*>(this);
                    AddRef();
                    return S_OK;
                }
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            Detail::DxcRefCountType STDMETHODCALLTYPE AddRef() override
            {
                return m_refCount.fetch_add(1, std::memory_order_relaxed) + 1;
            }

            Detail::DxcRefCountType STDMETHODCALLTYPE Release() override
            {
                const Detail::DxcRefCountType remaining = m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (remaining == 0)
                {
                    delete this;
                }
                return remaining;
            }

            HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override
            {
                if (!ppIncludeSource)
                {
                    return E_POINTER;
                }
                *ppIncludeSource = nullptr;

                if (!pFilename || !m_utils)
                {
                    return E_FAIL;
                }

                const std::string requestedUtf8 = WideToUtf8(pFilename);
                std::filesystem::path requestedPath(requestedUtf8);
                requestedPath = requestedPath.lexically_normal();

                std::filesystem::path resolvedPath;
                if (!ResolveIncludePath(requestedPath, resolvedPath))
                {
                    m_missingIncludes.push_back(requestedUtf8);
                    return E_FAIL;
                }

                const std::string canonicalKey = NormalizePathString(resolvedPath);
                const u32 loadCount = ++m_loadCounts[canonicalKey];
                if (loadCount > kMaxIncludeLoadsPerFile)
                {
                    if (m_cycleError.empty())
                    {
                        m_cycleError = "Circular or excessive #include recursion detected for: " +
                            CanonicalizePathBestEffort(resolvedPath).generic_string();
                    }
                    return E_FAIL;
                }

                auto cacheIt = m_cachedFiles.find(canonicalKey);
                if (cacheIt == m_cachedFiles.end())
                {
                    std::ifstream in(resolvedPath, std::ios::binary | std::ios::ate);
                    if (!in)
                    {
                        m_missingIncludes.push_back(requestedUtf8);
                        return E_FAIL;
                    }

                    const std::streamsize size = in.tellg();
                    if (size < 0)
                    {
                        return E_FAIL;
                    }
                    in.seekg(0, std::ios::beg);

                    std::string content(static_cast<usize>(size), '\0');
                    if (size > 0 && !in.read(content.data(), size))
                    {
                        return E_FAIL;
                    }

                    u64 hash = kFnvOffsetBasis;
                    if (!content.empty())
                    {
                        HashBytes(hash, content.data(), content.size());
                    }

                    const auto [insertedIt, inserted] = m_cachedFiles.emplace(canonicalKey, std::move(content));
                    (void)inserted;
                    cacheIt = insertedIt;

                    m_dependencies.push_back(ShaderIncludeDependency{
                        .CanonicalPath = CanonicalizePathBestEffort(resolvedPath).generic_string(),
                        .ContentHash = hash,
                        .ByteSize = static_cast<u64>(cacheIt->second.size())
                    });
                }

                Detail::DxcComPtr<IDxcBlobEncoding> encodingBlob;
                const auto& bytes = cacheIt->second;
                const HRESULT hr = m_utils->CreateBlob(
                    bytes.empty() ? "" : bytes.data(),
                    static_cast<UINT32>(bytes.size()),
                    DXC_CP_UTF8,
                    encodingBlob.GetAddressOf());
                if (FAILED(hr) || !encodingBlob)
                {
                    return E_FAIL;
                }

                *ppIncludeSource = encodingBlob.Detach();
                return S_OK;
            }

            [[nodiscard]] const std::vector<ShaderIncludeDependency>& GetDependencies() const noexcept
            {
                return m_dependencies;
            }

            [[nodiscard]] const std::string& GetCycleError() const noexcept
            {
                return m_cycleError;
            }

        private:
            bool ResolveIncludePath(const std::filesystem::path& candidate, std::filesystem::path& outResolved) const
            {
                std::error_code ec;
                auto tryFile = [&](const std::filesystem::path& p) -> bool {
                    if (p.empty())
                    {
                        return false;
                    }
                    const std::filesystem::path normalized = p.lexically_normal();
                    if (std::filesystem::exists(normalized, ec) && !ec && std::filesystem::is_regular_file(normalized, ec))
                    {
                        outResolved = CanonicalizePathBestEffort(normalized);
                        return true;
                    }
                    return false;
                };

                if (candidate.is_absolute())
                {
                    return tryFile(candidate);
                }

                if (!m_primarySourceDir.empty() && tryFile(m_primarySourceDir / candidate))
                {
                    return true;
                }

                if (tryFile(candidate))
                {
                    return true;
                }

                for (const auto& incDir : m_includeDirs)
                {
                    if (tryFile(incDir / candidate))
                    {
                        return true;
                    }
                }

                return false;
            }

            std::atomic<Detail::DxcRefCountType> m_refCount{ 1 };
            IDxcUtils* m_utils = nullptr;
            std::filesystem::path m_primarySourceDir;
            std::vector<std::filesystem::path> m_includeDirs;
            std::unordered_map<std::string, std::string> m_cachedFiles;
            std::unordered_map<std::string, u32> m_loadCounts;
            std::vector<ShaderIncludeDependency> m_dependencies;
            std::vector<std::string> m_missingIncludes;
            std::string m_cycleError;
        };

        std::string ReadDxcUtf8Blob(IDxcBlob* blob)
        {
            if (!blob || !blob->GetBufferPointer() || blob->GetBufferSize() == 0)
            {
                return {};
            }
            const char* ptr = static_cast<const char*>(blob->GetBufferPointer());
            usize len = static_cast<usize>(blob->GetBufferSize());
            while (len > 0 && ptr[len - 1] == '\0')
            {
                --len;
            }
            return std::string(ptr, len);
        }
    }

    class DxcModuleContext final
    {
    public:
        struct WorkerContext
        {
            Detail::DxcComPtr<IDxcUtils> Utils;
            Detail::DxcComPtr<IDxcCompiler3> Compiler;
        };

        DxcModuleContext() = default;

        ~DxcModuleContext()
        {
            // Release all COM interfaces BEFORE unloading dxcompiler shared library!
            {
                std::scoped_lock lock(m_poolMutex);
                m_workerPool.clear();
            }

            m_createInstance = nullptr;
            if (m_libraryHandle != nullptr)
            {
#if defined(_WIN32)
                FreeLibrary(static_cast<HMODULE>(m_libraryHandle));
#else
                dlclose(m_libraryHandle);
#endif
                m_libraryHandle = nullptr;
            }
        }

        DxcModuleContext(const DxcModuleContext&) = delete;
        DxcModuleContext& operator=(const DxcModuleContext&) = delete;

        bool Initialize(const std::filesystem::path& libraryPath)
        {
            std::error_code ec;
            const std::filesystem::path absPath = libraryPath.is_absolute()
                ? libraryPath
                : std::filesystem::absolute(libraryPath, ec);
            const std::filesystem::path& loadTarget = absPath.empty() ? libraryPath : absPath;

#if defined(_WIN32)
            HMODULE mod = LoadLibraryExW(
                loadTarget.c_str(),
                nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!mod)
            {
                mod = LoadLibraryW(loadTarget.c_str());
            }
            if (!mod)
            {
                return false;
            }
            m_libraryHandle = mod;
            m_createInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(mod, "DxcCreateInstance"));
#else
            void* mod = dlopen(loadTarget.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!mod && !libraryPath.empty() && libraryPath.filename() == libraryPath)
            {
                mod = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            }
            if (!mod)
            {
                return false;
            }
            m_libraryHandle = mod;
            m_createInstance = reinterpret_cast<DxcCreateInstanceProc>(dlsym(mod, "DxcCreateInstance"));
#endif
            if (!m_createInstance)
            {
                return false;
            }

            WorkerContext probe;
            if (!CreateWorker(probe))
            {
                return false;
            }

            QueryCompilerVersion(probe.Compiler.Get());

            {
                std::scoped_lock lock(m_poolMutex);
                m_workerPool.push_back(std::move(probe));
            }
            return true;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_libraryHandle != nullptr && m_createInstance != nullptr;
        }

        [[nodiscard]] const ShaderCompilerVersion& GetVersion() const noexcept
        {
            return m_version;
        }

        bool AcquireWorker(WorkerContext& outWorker)
        {
            {
                std::scoped_lock lock(m_poolMutex);
                if (!m_workerPool.empty())
                {
                    outWorker = std::move(m_workerPool.back());
                    m_workerPool.pop_back();
                    return true;
                }
            }
            return CreateWorker(outWorker);
        }

        void ReleaseWorker(WorkerContext&& worker)
        {
            if (worker.Utils && worker.Compiler)
            {
                std::scoped_lock lock(m_poolMutex);
                m_workerPool.push_back(std::move(worker));
            }
        }

        bool CreateContainerReflection(Detail::DxcComPtr<IDxcContainerReflection>& outReflection) const
        {
            if (!m_createInstance)
            {
                return false;
            }
            const HRESULT hr = m_createInstance(
                CLSID_DxcContainerReflection,
                Detail::GetDxcInterfaceId<IDxcContainerReflection>(),
                outReflection.PutVoid());
            return SUCCEEDED(hr) && static_cast<bool>(outReflection);
        }

    private:
        bool CreateWorker(WorkerContext& outWorker) const
        {
            if (!m_createInstance)
            {
                return false;
            }
            const HRESULT utilsHr = m_createInstance(
                CLSID_DxcUtils,
                Detail::GetDxcInterfaceId<IDxcUtils>(),
                outWorker.Utils.PutVoid());
            if (FAILED(utilsHr) || !outWorker.Utils)
            {
                outWorker = {};
                return false;
            }

            const HRESULT compHr = m_createInstance(
                CLSID_DxcCompiler,
                Detail::GetDxcInterfaceId<IDxcCompiler3>(),
                outWorker.Compiler.PutVoid());
            if (FAILED(compHr) || !outWorker.Compiler)
            {
                outWorker = {};
                return false;
            }
            return true;
        }

        void QueryCompilerVersion(IDxcCompiler3* compiler)
        {
            if (!compiler)
            {
                return;
            }

            Detail::DxcComPtr<IDxcVersionInfo> verInfo;
            if (SUCCEEDED(compiler->QueryInterface(Detail::GetDxcInterfaceId<IDxcVersionInfo>(), verInfo.PutVoid())) && verInfo)
            {
                UINT32 major = 0;
                UINT32 minor = 0;
                if (SUCCEEDED(verInfo->GetVersion(&major, &minor)))
                {
                    m_version.Major = major;
                    m_version.Minor = minor;
                }
            }

            Detail::DxcComPtr<IDxcVersionInfo2> verInfo2;
            if (SUCCEEDED(compiler->QueryInterface(Detail::GetDxcInterfaceId<IDxcVersionInfo2>(), verInfo2.PutVoid())) && verInfo2)
            {
                UINT32 commitCount = 0;
                char* commitHash = nullptr;
                if (SUCCEEDED(verInfo2->GetCommitInfo(&commitCount, &commitHash)))
                {
                    m_version.CommitCount = commitCount;
                    if (commitHash)
                    {
                        m_version.CommitHash = commitHash;
                        Detail::FreeDxcTaskString(commitHash);
                    }
                }
            }

            std::string customVer;
            Detail::DxcComPtr<IDxcVersionInfo3> verInfo3;
            if (SUCCEEDED(compiler->QueryInterface(Detail::GetDxcInterfaceId<IDxcVersionInfo3>(), verInfo3.PutVoid())) && verInfo3)
            {
                char* customStr = nullptr;
                if (SUCCEEDED(verInfo3->GetCustomVersionString(&customStr)))
                {
                    if (customStr)
                    {
                        customVer = customStr;
                        Detail::FreeDxcTaskString(customStr);
                    }
                }
            }

            std::ostringstream disp;
            disp << "DXC " << m_version.Major << "." << m_version.Minor;
            if (m_version.CommitCount > 0 || !m_version.CommitHash.empty())
            {
                disp << " (" << m_version.CommitCount;
                if (!m_version.CommitHash.empty())
                {
                    disp << "-" << m_version.CommitHash.substr(0, 8);
                }
                disp << ")";
            }
            if (!customVer.empty())
            {
                disp << " [" << customVer << "]";
            }
            m_version.DisplayString = disp.str();

            u64 vHash = kFnvOffsetBasis;
            HashValue(vHash, m_version.Major);
            HashValue(vHash, m_version.Minor);
            HashValue(vHash, m_version.CommitCount);
            HashString(vHash, m_version.CommitHash);
            HashString(vHash, m_version.DisplayString);
            m_version.VersionHash = vHash;
        }

        void* m_libraryHandle = nullptr;
        DxcCreateInstanceProc m_createInstance = nullptr;
        ShaderCompilerVersion m_version{};
        std::mutex m_poolMutex;
        std::vector<WorkerContext> m_workerPool;
    };

    namespace
    {
        struct ScopedWorkerGuard
        {
            DxcModuleContext& Module;
            DxcModuleContext::WorkerContext Worker;

            ~ScopedWorkerGuard()
            {
                Module.ReleaseWorker(std::move(Worker));
            }
        };
    }

    ShaderCompiler::ShaderCompiler(std::filesystem::path dxcToolDirectory)
        : m_dxcDirectory(std::move(dxcToolDirectory))
    {
#if defined(_WIN32)
        constexpr const char* kDxcLibName = "dxcompiler.dll";
        constexpr const char* kDxcExeName = "dxc.exe";
        constexpr const char* kSpirvValName = "spirv-val.exe";
#else
        constexpr const char* kDxcLibName = "libdxcompiler.so";
        constexpr const char* kDxcExeName = "dxc";
        constexpr const char* kSpirvValName = "spirv-val";
#endif

        auto tryInitFromDir = [this, kDxcLibName, kDxcExeName, kSpirvValName](const std::filesystem::path& dir) -> bool {
            if (dir.empty())
            {
                return false;
            }
            std::error_code ec;
            const std::filesystem::path libCandidate = dir / kDxcLibName;
            if (!std::filesystem::exists(libCandidate, ec) || ec)
            {
                return false;
            }

            auto moduleCtx = std::make_shared<DxcModuleContext>();
            if (!moduleCtx->Initialize(libCandidate))
            {
                return false;
            }

            m_dxcDirectory = CanonicalizePathBestEffort(dir);
            m_dxcLibPath = CanonicalizePathBestEffort(libCandidate);
            m_dxcModule = std::move(moduleCtx);

            const std::filesystem::path exeCandidate = dir / kDxcExeName;
            if (std::filesystem::exists(exeCandidate, ec) && !ec)
            {
                m_dxcExePath = CanonicalizePathBestEffort(exeCandidate);
            }

            const std::filesystem::path valCandidate = dir / kSpirvValName;
            if (std::filesystem::exists(valCandidate, ec) && !ec)
            {
                m_spirvValPath = CanonicalizePathBestEffort(valCandidate);
            }
            return true;
        };

        auto tryFindSpirvValInDir = [this, kSpirvValName](const std::filesystem::path& dir) {
            if (!m_spirvValPath.empty() || dir.empty())
            {
                return;
            }
            std::error_code ec;
            const std::filesystem::path valCandidate = dir / kSpirvValName;
            if (std::filesystem::exists(valCandidate, ec) && !ec)
            {
                m_spirvValPath = CanonicalizePathBestEffort(valCandidate);
            }
        };

        if (!m_dxcDirectory.empty())
        {
            (void)tryInitFromDir(m_dxcDirectory);
            return;
        }

#if defined(_WIN32)
        wchar_t exeBuf[MAX_PATH]{};
        const DWORD exeLen = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
        if (exeLen > 0 && exeLen < MAX_PATH)
        {
            const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
            if (tryInitFromDir(exeDir / "Tools" / "dxc") || tryInitFromDir(exeDir))
            {
                tryFindSpirvValInDir(exeDir / "Tools" / "dxc");
                tryFindSpirvValInDir(exeDir);
#ifdef MORBI_DEFAULT_DXC_DIR
                tryFindSpirvValInDir(std::filesystem::path(MORBI_DEFAULT_DXC_DIR));
#endif
                return;
            }
        }
#else
        char exeBuf[4096]{};
        const ssize_t exeLen = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
        if (exeLen > 0)
        {
            exeBuf[exeLen] = '\0';
            const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
            if (tryInitFromDir(exeDir / "Tools" / "dxc") || tryInitFromDir(exeDir))
            {
                return;
            }
        }
#endif

#ifdef MORBI_DEFAULT_DXC_DIR
        if (tryInitFromDir(std::filesystem::path(MORBI_DEFAULT_DXC_DIR)))
        {
            return;
        }
#endif

#if !defined(_WIN32)
        // Last-resort system loader search for libdxcompiler.so on Linux
        auto systemCtx = std::make_shared<DxcModuleContext>();
        if (systemCtx->Initialize(std::filesystem::path(kDxcLibName)))
        {
            m_dxcLibPath = kDxcLibName;
            m_dxcModule = std::move(systemCtx);
        }
#endif
    }

    ShaderCompiler::~ShaderCompiler() = default;

    bool ShaderCompiler::IsDXCAvailable() const noexcept
    {
        return m_dxcModule && m_dxcModule->IsValid();
    }

    bool ShaderCompiler::IsSpirVValidatorAvailable() const noexcept
    {
        std::error_code ec;
        return !m_spirvValPath.empty() && std::filesystem::exists(m_spirvValPath, ec) && !ec;
    }

    const ShaderCompilerVersion& ShaderCompiler::GetCompilerVersion() const noexcept
    {
        static const ShaderCompilerVersion kEmptyVersion{};
        return m_dxcModule ? m_dxcModule->GetVersion() : kEmptyVersion;
    }

    const char* ShaderCompiler::GetTargetProfileName(ShaderStage stage, ShaderModel model) noexcept
    {
        switch (stage)
        {
        case ShaderStage::Vertex:
            switch (model)
            {
            case ShaderModel::SM6_0: return "vs_6_0";
            case ShaderModel::SM6_1: return "vs_6_1";
            case ShaderModel::SM6_2: return "vs_6_2";
            case ShaderModel::SM6_3: return "vs_6_3";
            case ShaderModel::SM6_4: return "vs_6_4";
            case ShaderModel::SM6_5: return "vs_6_5";
            case ShaderModel::SM6_6: return "vs_6_6";
            }
            return "vs_6_0";

        case ShaderStage::Pixel:
            switch (model)
            {
            case ShaderModel::SM6_0: return "ps_6_0";
            case ShaderModel::SM6_1: return "ps_6_1";
            case ShaderModel::SM6_2: return "ps_6_2";
            case ShaderModel::SM6_3: return "ps_6_3";
            case ShaderModel::SM6_4: return "ps_6_4";
            case ShaderModel::SM6_5: return "ps_6_5";
            case ShaderModel::SM6_6: return "ps_6_6";
            }
            return "ps_6_0";

        case ShaderStage::Compute:
            switch (model)
            {
            case ShaderModel::SM6_0: return "cs_6_0";
            case ShaderModel::SM6_1: return "cs_6_1";
            case ShaderModel::SM6_2: return "cs_6_2";
            case ShaderModel::SM6_3: return "cs_6_3";
            case ShaderModel::SM6_4: return "cs_6_4";
            case ShaderModel::SM6_5: return "cs_6_5";
            case ShaderModel::SM6_6: return "cs_6_6";
            }
            return "cs_6_0";
        }
        return "vs_6_0";
    }

    u64 ShaderCompiler::ComputeRequestHash(
        const ShaderCompileRequest& request,
        const ShaderCompilerVersion* compilerVersion,
        std::span<const ShaderIncludeDependency> includeDependencies) noexcept
    {
        u64 hash = kFnvOffsetBasis;

        HashString(hash, CanonicalizePathBestEffort(std::filesystem::path(request.VirtualFilePath)).generic_string());
        HashString(hash, request.SourceCode);
        HashString(hash, request.EntryPoint);
        HashValue(hash, static_cast<u8>(request.Stage));
        HashValue(hash, static_cast<u8>(request.TargetFormat));
        HashValue(hash, static_cast<u8>(request.Model));
        HashValue(hash, static_cast<u8>(request.EnableOptimization ? 1u : 0u));
        HashValue(hash, static_cast<u8>(request.GenerateDebugInfo ? 1u : 0u));
        HashValue(hash, static_cast<u8>(request.ValidateSpirV ? 1u : 0u));

        const u64 defCount = static_cast<u64>(request.Defines.size());
        HashValue(hash, defCount);
        for (const auto& def : request.Defines)
        {
            HashString(hash, def.Name);
            HashString(hash, def.Value);
        }

        const u64 incDirCount = static_cast<u64>(request.IncludeDirs.size());
        HashValue(hash, incDirCount);
        for (const auto& incDir : request.IncludeDirs)
        {
            HashString(hash, NormalizePathString(incDir));
        }

        const u64 extraArgCount = static_cast<u64>(request.ExtraArguments.size());
        HashValue(hash, extraArgCount);
        for (const auto& arg : request.ExtraArguments)
        {
            HashString(hash, arg);
        }

        if (compilerVersion != nullptr)
        {
            HashValue(hash, compilerVersion->Major);
            HashValue(hash, compilerVersion->Minor);
            HashValue(hash, compilerVersion->CommitCount);
            HashString(hash, compilerVersion->CommitHash);
            HashString(hash, compilerVersion->DisplayString);
        }

        if (!includeDependencies.empty())
        {
            std::vector<ShaderIncludeDependency> sortedDeps(includeDependencies.begin(), includeDependencies.end());
            std::sort(sortedDeps.begin(), sortedDeps.end(), [](const auto& a, const auto& b) {
                return a.CanonicalPath < b.CanonicalPath;
            });

            const u64 depCount = static_cast<u64>(sortedDeps.size());
            HashValue(hash, depCount);
            for (const auto& dep : sortedDeps)
            {
                HashString(hash, dep.CanonicalPath);
                HashValue(hash, dep.ContentHash);
                HashValue(hash, dep.ByteSize);
            }
        }

        return hash;
    }

    bool ShaderCompiler::ComputeFileContentHash(
        const std::filesystem::path& filePath,
        u64& outHash,
        u64& outByteSize) noexcept
    {
        std::error_code ec;
        if (!std::filesystem::exists(filePath, ec) || ec || !std::filesystem::is_regular_file(filePath, ec))
        {
            return false;
        }

        std::ifstream in(filePath, std::ios::binary | std::ios::ate);
        if (!in)
        {
            return false;
        }

        const std::streamsize size = in.tellg();
        if (size < 0)
        {
            return false;
        }
        in.seekg(0, std::ios::beg);

        u64 hash = kFnvOffsetBasis;
        if (size > 0)
        {
            std::array<u8, 8192> chunk{};
            std::streamsize remaining = size;
            while (remaining > 0)
            {
                const std::streamsize toRead = std::min<std::streamsize>(remaining, static_cast<std::streamsize>(chunk.size()));
                if (!in.read(reinterpret_cast<char*>(chunk.data()), toRead))
                {
                    return false;
                }
                HashBytes(hash, chunk.data(), static_cast<usize>(toRead));
                remaining -= toRead;
            }
        }

        outHash = hash;
        outByteSize = static_cast<u64>(size);
        return true;
    }

    bool ShaderCompiler::ValidateSpirVBytecode(
        std::span<const u8> spirvBytecode,
        std::string& outDiagnostics) const
    {
        if (spirvBytecode.size() < 20 || (spirvBytecode.size() % 4) != 0)
        {
            outDiagnostics = "Invalid SPIR-V bytecode size (" + std::to_string(spirvBytecode.size()) + " bytes).";
            return false;
        }

        u32 magic = 0;
        std::memcpy(&magic, spirvBytecode.data(), sizeof(u32));
        if (magic != 0x07230203u)
        {
            outDiagnostics = "Invalid SPIR-V magic word (expected 0x07230203).";
            return false;
        }

        if (!IsSpirVValidatorAvailable())
        {
            outDiagnostics = "spirv-val executable not found.";
            return false;
        }

        static std::atomic<u64> s_valCounter{ 0 };
        const u64 valId = s_valCounter.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "MorbiSpirvVal";
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);

        const std::filesystem::path tempSpv = tempDir / ("validate_" + std::to_string(valId) + ".spv");
        struct TempCleanup
        {
            const std::filesystem::path& Path;
            ~TempCleanup()
            {
                std::error_code cleanEc;
                std::filesystem::remove(Path, cleanEc);
            }
        } cleanup{ tempSpv };

        {
            std::ofstream out(tempSpv, std::ios::binary);
            if (!out)
            {
                outDiagnostics = "Failed to write temporary SPIR-V file for validation.";
                return false;
            }
            out.write(reinterpret_cast<const char*>(spirvBytecode.data()), static_cast<std::streamsize>(spirvBytecode.size()));
            if (!out.good())
            {
                outDiagnostics = "Failed to flush temporary SPIR-V file for validation.";
                return false;
            }
        }

        int exitCode = -1;
        std::string toolLog;
        const std::vector<std::string> args = {
            "--target-env",
            "vulkan1.3",
            tempSpv.string()
        };

        if (!RunProcessCapture(m_spirvValPath, args, exitCode, toolLog))
        {
            outDiagnostics = toolLog.empty() ? "Failed to invoke spirv-val." : toolLog;
            return false;
        }

        if (exitCode != 0)
        {
            outDiagnostics = toolLog.empty()
                ? ("spirv-val failed with exit code " + std::to_string(exitCode))
                : toolLog;
            return false;
        }

        outDiagnostics = std::move(toolLog);
        return true;
    }

    Core::Result<ShaderReflectionData, std::string> ShaderCompiler::ReflectBytecode(
        ShaderTargetFormat targetFormat,
        std::span<const u8> bytecode,
        std::optional<ShaderStage> expectedStage,
        std::string_view expectedEntryPoint) const
    {
        switch (targetFormat)
        {
        case ShaderTargetFormat::SPIRV:
            return Detail::ReflectSpirvBinary(bytecode, expectedStage, expectedEntryPoint);

        case ShaderTargetFormat::DXIL:
        {
            if (!IsDXCAvailable())
            {
                const std::string targetDesc = m_dxcDirectory.empty()
                    ? std::string("<default search paths>")
                    : m_dxcDirectory.string();
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    "DXIL reflection failed: in-process DXC library not available (searched: " + targetDesc + ").");
            }

            DxcModuleContext::WorkerContext worker;
            if (!m_dxcModule->AcquireWorker(worker))
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    "DXIL reflection failed: unable to acquire DXC utils interface.");
            }
            struct WorkerReleaseGuard final
            {
                DxcModuleContext* Context = nullptr;
                DxcModuleContext::WorkerContext* Worker = nullptr;
                ~WorkerReleaseGuard()
                {
                    if (Context != nullptr && Worker != nullptr)
                    {
                        Context->ReleaseWorker(std::move(*Worker));
                    }
                }
            } releaseGuard{ m_dxcModule.get(), &worker };

            Detail::DxcComPtr<IDxcContainerReflection> containerReflection;
            if (!m_dxcModule->CreateContainerReflection(containerReflection))
            {
                return Core::Result<ShaderReflectionData, std::string>::Failure(
                    "DXIL reflection failed: DxcCreateInstance(CLSID_DxcContainerReflection) failed.");
            }

            return Detail::ReflectDxilBinary(
                bytecode,
                worker.Utils.Get(),
                containerReflection.Get(),
                expectedStage,
                expectedEntryPoint);
        }
        }
        return Core::Result<ShaderReflectionData, std::string>::Failure(
            "Unsupported ShaderTargetFormat for ReflectBytecode.");
    }

    ShaderReflectionData ShaderCompiler::ReflectSourceAndBinary(
        const ShaderCompileRequest& request,
        std::span<const u8> bytecode)
    {
        if (request.TargetFormat == ShaderTargetFormat::SPIRV && !bytecode.empty())
        {
            auto spvRes = Detail::ReflectSpirvBinary(bytecode, request.Stage, request.EntryPoint);
            if (spvRes.IsSuccess())
            {
                return std::move(spvRes).ConsumeValue();
            }
        }

        ShaderReflectionData refl{};
        refl.TargetFormat = request.TargetFormat;
        refl.Stage = request.Stage;
        refl.EntryPoint = request.EntryPoint;

        // 1. Parse numthreads(X, Y, Z) for compute shaders
        {
            static const std::regex numThreadsRegex(R"(\[\s*numthreads\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*\])");
            std::smatch match;
            if (std::regex_search(request.SourceCode, match, numThreadsRegex) && match.size() >= 4)
            {
                refl.ThreadGroupSizeX = static_cast<u32>(std::stoul(match[1].str()));
                refl.ThreadGroupSizeY = static_cast<u32>(std::stoul(match[2].str()));
                refl.ThreadGroupSizeZ = static_cast<u32>(std::stoul(match[3].str()));
            }
        }

        // 2. Parse register bindings: e.g. StructuredBuffer<Foo> gBuf : register(t0, space1);
        {
            static const std::regex bindingRegex(
                R"((ConstantBuffer|cbuffer|StructuredBuffer|RWStructuredBuffer|ByteAddressBuffer|RWByteAddressBuffer|Texture2D|RWTexture2D|SamplerState)\s*(?:<[^>]+>)?\s+([A-Za-z0-9_]+)\s*(?:\[\s*\])?\s*:\s*register\s*\(\s*([btus])(\d+)(?:\s*,\s*space(\d+))?\s*\))");
            auto begin = std::sregex_iterator(request.SourceCode.begin(), request.SourceCode.end(), bindingRegex);
            auto end = std::sregex_iterator();

            for (auto it = begin; it != end; ++it)
            {
                const std::smatch& m = *it;
                ReflectedBinding binding{};
                binding.Name = m[2].str();
                const char regType = m[3].str()[0];
                binding.RegisterSlot = static_cast<u32>(std::stoul(m[4].str()));
                binding.RegisterSpace = m[5].matched ? static_cast<u32>(std::stoul(m[5].str())) : 0u;

                if (regType == 'b')
                {
                    binding.Type = ResourceBindingType::ConstantBuffer;
                }
                else if (regType == 't')
                {
                    binding.Type = ResourceBindingType::ShaderResource;
                }
                else if (regType == 'u')
                {
                    binding.Type = ResourceBindingType::UnorderedAccess;
                }
                else if (regType == 's')
                {
                    binding.Type = ResourceBindingType::Sampler;
                }

                if (m[0].str().find("[]") != std::string::npos)
                {
                    binding.Count = 0xFFFFFFFFu;
                    refl.UsesBindlessHeap = true;
                }

                refl.Bindings.push_back(std::move(binding));
            }
        }

        // 3. Detect Push Constants / Root Constants (vk::push_constant or push constant cbuffer)
        if (request.SourceCode.find("push_constant") != std::string::npos ||
            request.SourceCode.find("PushConstants") != std::string::npos)
        {
            refl.PushConstantSizeBytes = 64;
            refl.Bindings.push_back(ReflectedBinding{
                .Name = "PushConstants",
                .Type = ResourceBindingType::PushConstants,
                .RegisterSlot = 0,
                .RegisterSpace = 0,
                .Count = 1
            });
        }

        if (request.SourceCode.find("ResourceDescriptorHeap") != std::string::npos)
        {
            refl.UsesBindlessHeap = true;
        }

        (void)bytecode;
        return refl;
    }

    Core::Result<CompiledShaderArtifact, std::string> ShaderCompiler::Compile(const ShaderCompileRequest& request) const
    {
        if (!IsDXCAvailable())
        {
            const std::string targetDesc = m_dxcDirectory.empty() ? std::string("<default search paths>") : m_dxcDirectory.string();
            return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                "In-process DXC library not available (searched: " + targetDesc + ").");
        }

        if (request.SourceCode.empty())
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure("Shader source code is empty.");
        }

        if (request.EntryPoint.empty())
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure("Shader entry point is empty.");
        }

        DxcModuleContext::WorkerContext worker{};
        if (!m_dxcModule->AcquireWorker(worker))
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure("Failed to acquire in-process DXC compiler instance.");
        }
        ScopedWorkerGuard workerGuard{ *m_dxcModule, std::move(worker) };

        const char* profileName = GetTargetProfileName(request.Stage, request.Model);

        std::vector<std::wstring> argStorage;
        argStorage.reserve(24 + request.Defines.size() * 2 + request.IncludeDirs.size() * 2 + request.ExtraArguments.size());

        auto pushArg = [&argStorage](std::wstring arg) {
            argStorage.push_back(std::move(arg));
        };

        pushArg(L"-T");
        pushArg(Utf8ToWide(profileName));
        pushArg(L"-E");
        pushArg(Utf8ToWide(request.EntryPoint));
        pushArg(request.EnableOptimization ? L"-O3" : L"-Od");
        if (request.GenerateDebugInfo)
        {
            pushArg(L"-Zi");
        }

        if (request.TargetFormat == ShaderTargetFormat::SPIRV)
        {
            pushArg(L"-spirv");
            pushArg(L"-fspv-target-env=vulkan1.3");
            pushArg(L"-D");
            pushArg(L"MORBI_VULKAN=1");
        }
        else
        {
            pushArg(L"-D");
            pushArg(L"MORBI_DX12=1");
        }

        for (const auto& def : request.Defines)
        {
            if (def.Name.empty())
            {
                continue;
            }
            pushArg(L"-D");
            if (def.Value.empty())
            {
                pushArg(Utf8ToWide(def.Name));
            }
            else
            {
                pushArg(Utf8ToWide(def.Name + "=" + def.Value));
            }
        }

        for (const auto& incDir : request.IncludeDirs)
        {
            if (incDir.empty())
            {
                continue;
            }
            pushArg(L"-I");
            pushArg(Utf8ToWide(CanonicalizePathBestEffort(incDir).string()));
        }

        for (const auto& extra : request.ExtraArguments)
        {
            if (!extra.empty())
            {
                pushArg(Utf8ToWide(extra));
            }
        }

        const std::string virtualSourcePath = request.VirtualFilePath.empty() ? std::string("InlineShader.hlsl") : request.VirtualFilePath;
        pushArg(Utf8ToWide(virtualSourcePath));

        std::vector<LPCWSTR> argPtrs;
        argPtrs.reserve(argStorage.size());
        for (const auto& a : argStorage)
        {
            argPtrs.push_back(a.c_str());
        }

        std::filesystem::path primaryDir;
        {
            const std::filesystem::path vp(virtualSourcePath);
            if (vp.has_parent_path())
            {
                primaryDir = CanonicalizePathBestEffort(vp.parent_path());
            }
        }

        Detail::DxcComPtr<MorbiDxcIncludeHandler> includeHandler;
        includeHandler.Attach(new MorbiDxcIncludeHandler(
            workerGuard.Worker.Utils.Get(),
            std::move(primaryDir),
            request.IncludeDirs));

        const DxcBuffer sourceBuffer{
            .Ptr = request.SourceCode.data(),
            .Size = request.SourceCode.size(),
            .Encoding = DXC_CP_UTF8
        };

        Detail::DxcComPtr<IDxcResult> dxcResult;
        const HRESULT compileHr = workerGuard.Worker.Compiler->Compile(
            &sourceBuffer,
            argPtrs.data(),
            static_cast<UINT32>(argPtrs.size()),
            includeHandler.Get(),
            Detail::GetDxcInterfaceId<IDxcResult>(),
            dxcResult.PutVoid());

        std::string diagnostics;
        if (dxcResult && dxcResult->HasOutput(DXC_OUT_ERRORS))
        {
            Detail::DxcComPtr<IDxcBlobUtf8> errUtf8;
            if (SUCCEEDED(dxcResult->GetOutput(DXC_OUT_ERRORS, Detail::GetDxcInterfaceId<IDxcBlobUtf8>(), errUtf8.PutVoid(), nullptr)) && errUtf8)
            {
                diagnostics = ReadDxcUtf8Blob(errUtf8.Get());
            }
            else
            {
                Detail::DxcComPtr<IDxcBlob> errBlob;
                if (SUCCEEDED(dxcResult->GetOutput(DXC_OUT_ERRORS, Detail::GetDxcInterfaceId<IDxcBlob>(), errBlob.PutVoid(), nullptr)) && errBlob)
                {
                    diagnostics = ReadDxcUtf8Blob(errBlob.Get());
                }
            }
        }

        if (!includeHandler->GetCycleError().empty())
        {
            if (!diagnostics.empty() && diagnostics.back() != '\n')
            {
                diagnostics.push_back('\n');
            }
            diagnostics += includeHandler->GetCycleError();
        }

        HRESULT compileStatus = E_FAIL;
        if (dxcResult)
        {
            (void)dxcResult->GetStatus(&compileStatus);
        }

        if (FAILED(compileHr) || !dxcResult || FAILED(compileStatus))
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                "DXC compilation failed (" + std::string(profileName) + "): " +
                (diagnostics.empty() ? ("HRESULT=" + std::to_string(static_cast<long>(compileStatus))) : diagnostics));
        }

        Detail::DxcComPtr<IDxcBlob> objectBlob;
        if (!dxcResult->HasOutput(DXC_OUT_OBJECT) ||
            FAILED(dxcResult->GetOutput(DXC_OUT_OBJECT, Detail::GetDxcInterfaceId<IDxcBlob>(), objectBlob.PutVoid(), nullptr)) ||
            !objectBlob ||
            objectBlob->GetBufferSize() == 0)
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                "DXC produced empty shader bytecode. " + diagnostics);
        }

        std::vector<u8> bytecode(static_cast<usize>(objectBlob->GetBufferSize()));
        std::memcpy(bytecode.data(), objectBlob->GetBufferPointer(), bytecode.size());

        if (request.TargetFormat == ShaderTargetFormat::SPIRV && request.ValidateSpirV)
        {
            if (IsSpirVValidatorAvailable())
            {
                std::string valDiag;
                if (!ValidateSpirVBytecode(bytecode, valDiag))
                {
                    return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                        "SPIR-V validation failed: " + valDiag);
                }
            }
            else
            {
                if (!diagnostics.empty() && diagnostics.back() != '\n')
                {
                    diagnostics.push_back('\n');
                }
                diagnostics += "[WARN] spirv-val unavailable; SPIR-V validation skipped.";
            }
        }

        const ShaderCompilerVersion& ver = GetCompilerVersion();
        std::vector<ShaderIncludeDependency> deps = includeHandler->GetDependencies();
        std::sort(deps.begin(), deps.end(), [](const auto& a, const auto& b) {
            return a.CanonicalPath < b.CanonicalPath;
        });

        CompiledShaderArtifact artifact{};
        artifact.Stage = request.Stage;
        artifact.TargetFormat = request.TargetFormat;
        artifact.Model = request.Model;
        artifact.CompilerVersionHash = ver.VersionHash;
        artifact.CompilerVersionString = ver.DisplayString;
        artifact.IncludeDependencies = std::move(deps);
        artifact.ContentHash = ComputeRequestHash(request, &ver, artifact.IncludeDependencies);
        {
            auto reflRes = ReflectBytecode(request.TargetFormat, bytecode, request.Stage, request.EntryPoint);
            if (!reflRes.IsSuccess())
            {
                return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                    "Post-compile binary reflection failed: " + reflRes.Error());
            }
            artifact.Reflection = std::move(reflRes).ConsumeValue();
        }
        artifact.Bytecode = std::move(bytecode);
        artifact.CompilerMessages = std::move(diagnostics);

        return Core::Result<CompiledShaderArtifact, std::string>::Success(std::move(artifact));
    }

    Core::Result<CompiledShaderArtifact, std::string> ShaderCompiler::CompileFromFile(
        const std::filesystem::path& filePath,
        std::string_view entryPoint,
        ShaderStage stage,
        ShaderTargetFormat targetFormat,
        std::span<const ShaderMacro> defines,
        ShaderModel model) const
    {
        const std::filesystem::path canonicalFile = CanonicalizePathBestEffort(filePath);
        std::ifstream file(canonicalFile, std::ios::binary);
        if (!file)
        {
            return Core::Result<CompiledShaderArtifact, std::string>::Failure(
                "Shader source file not found: " + filePath.string());
        }

        std::ostringstream ss;
        ss << file.rdbuf();

        ShaderCompileRequest req{};
        req.SourceCode = ss.str();
        req.VirtualFilePath = canonicalFile.string();
        req.EntryPoint = std::string(entryPoint);
        req.Stage = stage;
        req.TargetFormat = targetFormat;
        req.Model = model;
        req.Defines.assign(defines.begin(), defines.end());
        if (canonicalFile.has_parent_path())
        {
            req.IncludeDirs.push_back(canonicalFile.parent_path());
        }

        return Compile(req);
    }
}
