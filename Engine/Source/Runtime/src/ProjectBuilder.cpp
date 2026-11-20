#include "Morbi/Runtime/ProjectBuilder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
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
#include "Morbi/IO/MorbiPak.hpp"
#include "Morbi/Script/ScriptEngine.hpp"

namespace Morbi::Runtime
{
    namespace
    {
        std::string SanitizeExeBaseName(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            for (char c : name)
            {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                {
                    out.push_back(c);
                }
            }
            return out.empty() ? std::string("MorbiGame") : out;
        }

        bool RunCommandLine(
            const std::filesystem::path& workingDir,
            const std::wstring& cmdLine,
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
                outLog = "Failed to create build process pipe.";
                return false;
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = writePipe;
            si.hStdError = writePipe;

            PROCESS_INFORMATION pi{};
            std::wstring mutableCmd = cmdLine;
            const BOOL ok = CreateProcessW(
                nullptr,
                mutableCmd.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                workingDir.empty() ? nullptr : workingDir.c_str(),
                &si,
                &pi);

            CloseHandle(writePipe);
            if (!ok)
            {
                CloseHandle(readPipe);
                outLog = "Failed to launch build command (Win32 error=" + std::to_string(GetLastError()) + ").";
                return false;
            }

            char buf[2048];
            DWORD readLen = 0;
            while (ReadFile(readPipe, buf, sizeof(buf), &readLen, nullptr) && readLen > 0)
            {
                outLog.append(buf, readLen);
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
            (void)workingDir;
            (void)cmdLine;
            outExitCode = -1;
            outLog = "Unsupported OS.";
            return false;
#endif
        }

        std::filesystem::path LocateCMakeExecutable(const std::filesystem::path& engineBinDir)
        {
            std::error_code ec;
            const std::vector<std::filesystem::path> directCandidates = {
                engineBinDir / "Tools" / "cmake" / "bin" / "cmake.exe",
                engineBinDir / "Tools" / "cmake" / "cmake.exe",
                "C:/Program Files/CMake/bin/cmake.exe",
                "C:/Program Files (x86)/CMake/bin/cmake.exe"
            };

            for (const auto& c : directCandidates)
            {
                if (std::filesystem::exists(c, ec) && !ec)
                {
                    return c;
                }
            }

#if defined(_WIN32)
            wchar_t foundBuf[MAX_PATH]{};
            if (SearchPathW(nullptr, L"cmake.exe", nullptr, MAX_PATH, foundBuf, nullptr) > 0)
            {
                return std::filesystem::path(foundBuf);
            }
#endif
            return {};
        }

        void EnsureLauncherTemplateExists(const std::filesystem::path& launcherDir, const std::filesystem::path& engineBinDir)
        {
            std::error_code ec;
            if (std::filesystem::exists(launcherDir / "CMakeLists.txt", ec) &&
                std::filesystem::exists(launcherDir / "src" / "main.cpp", ec))
            {
                return;
            }

            const std::filesystem::path bundledTemplate = engineBinDir / "Tools" / "Launcher";
            if (std::filesystem::exists(bundledTemplate, ec))
            {
                std::filesystem::create_directories(launcherDir, ec);
                std::filesystem::copy(
                    bundledTemplate,
                    launcherDir,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                    ec);
            }
        }
    }

    ProjectBuildResult ProjectBuilder::BuildGame(
        const ProjectBuildRequest& request,
        Script::ScriptEngine& scriptEngine)
    {
        ProjectBuildResult result{};
        std::error_code ec;

        if (request.OutputDir.empty())
        {
            result.Error = "Output directory cannot be empty.";
            return result;
        }

        const std::filesystem::path absOutputDir = std::filesystem::absolute(request.OutputDir, ec).lexically_normal();
        const std::filesystem::path absProjectRoot = std::filesystem::absolute(request.ProjectRootDir, ec).lexically_normal();
        const std::filesystem::path absAssetsDir = request.AssetsDir.empty() ? std::filesystem::path{} : std::filesystem::absolute(request.AssetsDir, ec).lexically_normal();
        const std::filesystem::path absScriptsDir = request.ScriptsDir.empty() ? std::filesystem::path{} : std::filesystem::absolute(request.ScriptsDir, ec).lexically_normal();
        const std::filesystem::path absEngineBinDir = std::filesystem::absolute(request.EngineBinDir, ec).lexically_normal();

        const std::filesystem::path managedOutDir = absOutputDir / "Managed";
        const std::filesystem::path contentOutDir = absOutputDir / "Content";
        const std::filesystem::path toolsOutDir = absOutputDir / "Tools" / "dxc";
        std::filesystem::create_directories(managedOutDir, ec);
        std::filesystem::create_directories(contentOutDir, ec);
        std::filesystem::create_directories(toolsOutDir, ec);

        Core::Log::Info("BuildPipeline", "Starting standalone game build for '" + request.ProjectName + "' -> " + absOutputDir.string());

        // 1. Compile raw C# project scripts -> Managed/GameScripts.dll
        const std::filesystem::path gameScriptsDll = managedOutDir / "GameScripts.dll";
        if (!absScriptsDir.empty() && std::filesystem::exists(absScriptsDir, ec))
        {
            const Script::ScriptCompileResult scriptRes = scriptEngine.CompileProjectScripts(
                absScriptsDir,
                gameScriptsDll,
                false,
                request.ReleaseMode);
            result.CompiledScriptCount = scriptRes.CompiledFileCount;
            if (!scriptRes.Success)
            {
                result.Error = "C# script compilation failed:\n" + scriptRes.Diagnostics;
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }
            Core::Log::Info("BuildPipeline", "[1/4] Compiled " + std::to_string(result.CompiledScriptCount) + " C# script(s) -> GameScripts.dll");
        }

        // 2. Package Assets/ into bounded, category-sorted encrypted MorbiPak chunks
        std::vector<IO::Pak::PakFileSource> pakSources;
        if (!absAssetsDir.empty() && std::filesystem::exists(absAssetsDir, ec))
        {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(absAssetsDir, ec))
            {
                if (!entry.is_regular_file(ec)) continue;
                const std::filesystem::path rel = std::filesystem::relative(entry.path(), absAssetsDir, ec);
                if (ec) continue;
                const std::string relStr = rel.generic_string();
                pakSources.push_back(IO::Pak::PakFileSource{
                    .SourcePath = entry.path(),
                    .VirtualPath = "assets://" + relStr,
                    .OriginalRelativePath = relStr,
                    .Category = IO::Pak::CategorizeAssetPath(relStr)
                });
            }
        }

        std::vector<std::uint8_t> exportKey;
        if (request.EncryptPaks)
        {
            exportKey = IO::Pak::GenerateExportKey();
            std::ofstream keyOut(contentOutDir / "pakkey.bin", std::ios::binary | std::ios::trunc);
            if (!keyOut)
            {
                result.Error = "Failed to write Content/pakkey.bin";
                return result;
            }
            keyOut.write(reinterpret_cast<const char*>(exportKey.data()), static_cast<std::streamsize>(exportKey.size()));
        }

        IO::Pak::PakChunkWriteOptions chunkOpts{};
        chunkOpts.ProjectName = request.ProjectName;
        chunkOpts.MaxChunkSizeBytes = request.MaxChunkSizeBytes;
        chunkOpts.EncryptPayloads = request.EncryptPaks;
        chunkOpts.EncryptionKey = exportKey;

        IO::Pak::PakChunkIndex chunkIndex{};
        std::string pakErr;
        if (!IO::Pak::WriteChunkedPaks(contentOutDir, pakSources, chunkOpts, chunkIndex, pakErr))
        {
            result.Error = "Chunked MorbiPak packaging failed: " + pakErr;
            Core::Log::Error("BuildPipeline", result.Error);
            return result;
        }

        result.ChunkIndexPath = contentOutDir / "pakchunks.index.json";
        result.ChunkCount = static_cast<std::uint32_t>(chunkIndex.Chunks.size());
        for (const auto& c : chunkIndex.Chunks)
        {
            result.TotalPackedBytes += c.PackageBytes;
        }
        Core::Log::Info("BuildPipeline", "[2/4] Packaged " + std::to_string(pakSources.size()) +
            " asset(s) into " + std::to_string(result.ChunkCount) + " MorbiPak chunk(s) (" +
            std::to_string(result.TotalPackedBytes / (1024 * 1024)) + " MB)");

        // 3. Compile Open C++ Launcher inside Project/Launcher (100% portable Zig C++20 first, CMake/MSVC fallback)
        const std::filesystem::path launcherDir = absProjectRoot / "Launcher";
        EnsureLauncherTemplateExists(launcherDir, absEngineBinDir);

        const std::string safeExeName = SanitizeExeBaseName(request.ProjectName) + ".exe";
        result.OutputExePath = absOutputDir / safeExeName;

        const std::filesystem::path portableZig = absEngineBinDir / "Tools" / "zig" / "zig.exe";
        const std::filesystem::path launcherBuildDir = absOutputDir / ".launcher_obj";
        std::filesystem::create_directories(launcherBuildDir, ec);

        if (std::filesystem::exists(portableZig, ec) && !ec)
        {
            std::vector<std::filesystem::path> launcherSources;
            const std::filesystem::path launcherSrcDir = launcherDir / "src";
            if (std::filesystem::exists(launcherSrcDir, ec))
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(launcherSrcDir, ec))
                {
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".cpp")
                    {
                        launcherSources.push_back(entry.path());
                    }
                }
            }
            if (launcherSources.empty())
            {
                result.Error = "No .cpp files found inside Project Launcher/src/";
                return result;
            }

            const std::filesystem::path zigGlobalCache = absEngineBinDir / "cache" / "zig_global_cache";
            const std::filesystem::path zigLocalCache = launcherBuildDir / "zig_local";
            std::filesystem::create_directories(zigGlobalCache, ec);
            std::filesystem::create_directories(zigLocalCache, ec);
            SetEnvironmentVariableW(L"ZIG_GLOBAL_CACHE_DIR", zigGlobalCache.wstring().c_str());
            SetEnvironmentVariableW(L"ZIG_LOCAL_CACHE_DIR", zigLocalCache.wstring().c_str());

            std::wstring zigCmd = L"\"" + portableZig.wstring() + L"\" c++ -std=c++20 -O2 -DNDEBUG";
            for (const auto& src : launcherSources)
            {
                zigCmd += L" \"" + src.wstring() + L"\"";
            }
            zigCmd += L" -lole32 -o \"" + result.OutputExePath.wstring() + L"\"";

            int zigExit = -1;
            std::string zigLog;
            if (!RunCommandLine(launcherDir, zigCmd, zigExit, zigLog) || zigExit != 0 ||
                !std::filesystem::exists(result.OutputExePath, ec))
            {
                result.Error = "Portable Zig C++ Launcher compile failed:\n" + zigLog;
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }
            std::filesystem::remove_all(launcherBuildDir, ec);
            Core::Log::Info("BuildPipeline", "[3/4] Compiled Open C++ Launcher (Portable Zig C++20) -> " + safeExeName);
        }
        else
        {
            const std::filesystem::path cmakeExe = LocateCMakeExecutable(absEngineBinDir);
            if (cmakeExe.empty())
            {
                result.Error = "Neither portable Tools/zig/zig.exe nor CMake executable found.";
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }

            if (!std::filesystem::exists(launcherDir / "CMakeLists.txt", ec))
            {
                result.Error = "Project Launcher/CMakeLists.txt missing at: " + launcherDir.string();
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }

            int cmdExit = -1;
            std::string cmdLog;
            const std::wstring cfgCmd = L"\"" + cmakeExe.wstring() + L"\" -S \"" + launcherDir.wstring() +
                L"\" -B \"" + launcherBuildDir.wstring() + L"\"";
            if (!RunCommandLine(launcherDir, cfgCmd, cmdExit, cmdLog) || cmdExit != 0)
            {
                result.Error = "Open C++ Launcher CMake configure failed:\n" + cmdLog;
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }

            cmdLog.clear();
            const std::wstring buildCmd = L"\"" + cmakeExe.wstring() + L"\" --build \"" + launcherBuildDir.wstring() +
                L"\" --config Release";
            if (!RunCommandLine(launcherDir, buildCmd, cmdExit, cmdLog) || cmdExit != 0)
            {
                result.Error = "Open C++ Launcher C++ compile failed:\n" + cmdLog;
                Core::Log::Error("BuildPipeline", result.Error);
                return result;
            }

            std::filesystem::path builtLauncherExe = launcherBuildDir / "Release" / "MorbiGame.exe";
            if (!std::filesystem::exists(builtLauncherExe, ec))
            {
                builtLauncherExe = launcherBuildDir / "MorbiGame.exe";
            }
            if (!std::filesystem::exists(builtLauncherExe, ec))
            {
                result.Error = "Compiled launcher binary MorbiGame.exe was not found in " + launcherBuildDir.string();
                return result;
            }

            std::filesystem::copy_file(builtLauncherExe, result.OutputExePath, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove_all(launcherBuildDir, ec);
            Core::Log::Info("BuildPipeline", "[3/4] Compiled Open C++ Launcher (CMake) -> " + safeExeName);
        }

        // 4. Stage Morbi Runtime DLLs, Managed SDK, Portable .NET CoreCLR, DXC tools & project config
        for (const auto& item : std::filesystem::directory_iterator(absEngineBinDir, ec))
        {
            if (!item.is_regular_file(ec)) continue;
            const std::filesystem::path ext = item.path().extension();
            if (ext == ".dll")
            {
                std::filesystem::copy_file(
                    item.path(),
                    absOutputDir / item.path().filename(),
                    std::filesystem::copy_options::update_existing,
                    ec);
            }
        }

        const std::filesystem::path engineManagedDir = absEngineBinDir / "Managed";
        if (std::filesystem::exists(engineManagedDir, ec))
        {
            std::filesystem::copy(
                engineManagedDir,
                managedOutDir,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing,
                ec);
        }

        const std::filesystem::path engineDxcDir = absEngineBinDir / "Tools" / "dxc";
        if (std::filesystem::exists(engineDxcDir, ec))
        {
            std::filesystem::copy(
                engineDxcDir,
                toolsOutDir,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing,
                ec);
        }

        const std::filesystem::path engineDotnetDir = absEngineBinDir / "Tools" / "dotnet";
        if (std::filesystem::exists(engineDotnetDir, ec))
        {
            std::filesystem::copy(
                engineDotnetDir,
                absOutputDir / "Tools" / "dotnet",
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing,
                ec);
        }

        if (!request.ProjectFilePath.empty() && std::filesystem::exists(request.ProjectFilePath, ec))
        {
            std::filesystem::copy_file(
                request.ProjectFilePath,
                absOutputDir / "game.morbiproj",
                std::filesystem::copy_options::overwrite_existing,
                ec);
        }

        result.Success = true;
        result.Summary = "Build succeeded -> '" + result.OutputExePath.string() + "' (Scripts=" +
            std::to_string(result.CompiledScriptCount) + ", Chunks=" + std::to_string(result.ChunkCount) + ")";
        Core::Log::Info("BuildPipeline", "[4/4] " + result.Summary);
        return result;
    }
}
