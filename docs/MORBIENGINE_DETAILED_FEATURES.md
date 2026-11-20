# MorbiEngine — Kapsamlı Özellik Kataloğu ve Teknik Mimari Referansı

> **Kapsam Notu:** Bu doküman, `legacy/` dizini **kesinlikle hariç tutularak** MorbiEngine deposundaki tüm aktif alt sistemlerin doğrudan kaynak kod incelemesine dayanılarak hazırlanmıştır. C++20 motor çekirdeği, RHI (Vulkan 1.3 & DX12), DXC shader boru hattı, RenderGraph, VFS/MorbiPak, Asset/Animasyon sistemi, Sparse-Set SoA ECS, Jolt Fizik, SoLoud Ses, .NET 10 CoreCLR + Roslyn betik motoru, WebView2 + React Editör & Binary IPC, GPU Prosedürel HUD/Gizmo, %100 taşınabilir bağımsız oyun derleyici, proje şablonları, `MorbiNetwork` çok oyunculu sunucu/istemci yığını ve `hub_and_web_syss` (Morbi Hub & Dağıtım Platformu) en ince ayrıntısına kadar listelenmiştir.

---

## İçindekiler
1. [Mimari Genel Bakış, C++20 Modülleri ve Derleme Seçenekleri](#1-mimari-genel-bakış-c20-modülleri-ve-derleme-seçenekleri)
2. [Komut Satırı Bayrakları (CLI) ve Başlatma Mimarisi](#2-komut-satırı-bayrakları-cli-ve-başlatma-mimarisi)
3. [Morbi.Core — Bellek, Matematik, Zamanlayıcı, Ayarlar ve Günlükleme](#3-morbicoresistemleri--bellek-matematik-zamanlayıcı-ayarlar-ve-günlükleme)
4. [Morbi.IO — Sanal Dosya Sistemi (VFS), İkili Akış, FileWatcher ve Şifreli/Chunked MorbiPak](#4-morbiio--vfs-ikili-akış-filewatcher-ve-şifrelichunked-morbipak)
5. [Morbi.Platform — Pencere Yönetimi ve Yüksek Hassasiyetli Girdi Sistemi](#5-morbiplatform--pencere-yönetimi-ve-girdi-sistemi)
6. [Morbi.RHI, RHI.Vulkan & RHI.DX12 — Çoklu Grafik API ve Bindless Bellek](#6-morbirhi-rhivulkan--rhidx12--çoklu-grafik-api-ve-bindless-bellek)
7. [Morbi.Shader & Morbi.Render — Çift Hedefli DXC, RenderGraph ve 3D Gölgelendirme](#7-morbishader--morbirender--dxc-rendergraph-ve-3d-gölgelendirme)
8. [Morbi.Asset — Assimp 3D İçe Aktarım, MikkTSpace, .mAsset v2 ve İskelet Animasyon](#8-morbiasset--model-doku-masset-v2-ve-iskelet-animasyon)
9. [Morbi.Scene — Sayfalı Sparse-Set SoA ECS, Hiyerarşi, JSON ve PIE Snapshot](#9-morbiscene--sayfalı-sparse-set-soa-ecs-hiyerarşi-json-ve-pie-snapshot)
10. [Morbi.Physics & Morbi.Audio — Jolt Physics v5.2.0 ve SoLoud 3D Ses](#10-morbiphysics--morbiaudio--jolt-physics-ve-soloud-3d-ses)
11. [Morbi.Script & Managed SDK — .NET 10 CoreCLR, Roslyn 5.12.0 ve Zero-Copy Köprü](#11-morbiscript--managed-sdk--net-10-coreclr-roslyn-5120-ve-zero-copy-köprü)
12. [Morbi.UI & MorbiEditor — Edge WebView2, Binary IPC, React Panelleri ve GPU HUD/Gizmo](#12-morbiui--morbieditor--webview2-binary-ipc-react-panelleri-ve-gpu-hudgizmo)
13. [Morbi.Runtime & ProjectBuilder — %100 Taşınabilir Bağımsız Oyun Derleyici ve Açık C++ Launcher](#13-morbiruntime--projectbuilder--100-taşınabilir-oyun-derleyici-ve-açık-c-launcher)
14. [Proje Şablonları (Templates/) ve Örnek Proje Yapısı](#14-proje-şablonları-templates)
15. [MorbiNetwork — Python FastAPI + Rust 60 Hz UDP Tick Server + C# Zero-GC SDK](#15-morbinetwork--python-fastapi--rust-60-hz-udp-tick-server--c-zero-gc-sdk)
16. [Morbi Hub & Dağıtım Platformu (`hub_and_web_syss/`)](#16-morbi-hub--dağıtım-platformu-hub_and_web_syss)

---

## 1. Mimari Genel Bakış, C++20 Modülleri ve Derleme Seçenekleri

### 1.1 C++20 Derleyici ve Standart Politikası (`CMakeLists.txt`, `cmake/MorbiCompilerOptions.cmake`)
* **Standart:** CMake `3.24+`, `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`, `CMAKE_EXPORT_COMPILE_COMMANDS ON`.
* **MSVC Bayrakları:** `/W4` (sıfır uyarı disiplini, üçüncü parti SoLoud `/W0`), `/permissive-`, `/Zc:__cplusplus`, `/EHsc`, `/MP` (çok çekirdekli derleme), `/arch:AVX2`, DLL export için `WINDOWS_EXPORT_ALL_SYMBOLS ON` + `MORBI_BUILDING_DLL=1`, `/wd4251`, `/wd4275`.
* **Windows Ön Tanımları:** `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `UNICODE`, `_UNICODE`.
* **GCC / Clang Bayrakları:** `-Wall -Wextra -Wpedantic -mavx2 -mfma`.

### 1.2 15 Paylaşımlı C++20 Motor Modülü (`Engine/Source/`)
| Modül | CMake Hedefi / Alias | Temel Sorumluluğu | Doğrudan Bağımlılıkları |
| :--- | :--- | :--- | :--- |
| **Core** | `Morbi.Core` / `Morbi::Core` | Tip sistemi, bellek havuzları, SIMD matematik, `TaskScheduler`, `Log`, `Result`, `StringId`, `EngineSettings` | Yok (kök modül) |
| **IO** | `Morbi.IO` / `Morbi::IO` | `PathService` VFS, `BinaryReader`/`BinaryWriter`, OS `FileWatcher`, şifreli ve chunked `MorbiPak` | `Morbi::Core`, `nlohmann_json` |
| **Platform** | `Morbi.Platform` / `Morbi::Platform` | Win32/SDL3 pencere oluşturma, DPI ölçekleme, 512 tuş + 5 fare tuşu `InputState` | `Morbi::Core`, `SDL3::SDL3` |
| **Shader** | `Morbi.Shader` / `Morbi::Shader` | DXC CLI üzerinden HLSL -> `DXIL` ve `SPIRV` derleme, regex reflection, `ShaderCache` | `Morbi::Core` |
| **RHI** | `Morbi.RHI` / `Morbi::RHI` | Soyut `IDevice`, `ICommandList`, `RHITypes`, bindless tipler ve `NullDevice` | `Morbi::Core`, `Morbi::Platform` |
| **RHI.Vulkan** | `Morbi.RHI.Vulkan` / `Morbi::RHI.Vulkan` | `VK_NO_PROTOTYPES` ile dinamik Vulkan 1.3 cihazı (`vulkan-1.dll`) | `Morbi::Core`, `Platform`, `RHI`, `Vulkan::Headers` |
| **RHI.DX12** | `Morbi.RHI.DX12` / `Morbi::RHI.DX12` | Direct3D 12 + DXGI 1.6 cihazı ve 32 MB Placed Heap ayırıcı | `Morbi::Core`, `Platform`, `RHI`, `d3d12`, `dxgi`, `dxguid` |
| **Asset** | `Morbi.Asset` / `Morbi::Asset` | `AssetRegistry`, Assimp `ModelImporter`, MikkTSpace, `TextureImporter`, `.mAsset` serializer, `AnimationEvaluator` | `Morbi::Core`, `IO`, `RHI`, `Morbi::Assimp`, `windowscodecs`, `ole32` |
| **Render** | `Morbi.Render` / `Morbi::Render` | `RenderGraph` geçici kaynak aliasing + bariyer sentezi ve `CreateDevice` fabrikası | `Morbi::Core`, `RHI`, `RHI.Vulkan`, `RHI.DX12` |
| **Scene** | `Morbi.Scene` / `Morbi::Scene` | Paged `SparseSet` SoA ECS, 9 bileşen havuzu, JSON sahne yükle/kaydet, `SceneSnapshot` | `Morbi::Core`, `Asset`, `nlohmann_json` |
| **Physics** | `Morbi.Physics` / `Morbi::Physics` | Jolt Physics v5.2.0 60 Hz simülasyon, Raycast ve `CollisionPairEvent` | `Morbi::Core`, `Scene`, `Jolt` |
| **Audio** | `Morbi.Audio` / `Morbi::Audio` | SoLoud + Miniaudio 3D mekansal ses ve prosedürel WAV üretici | `Morbi::Core`, `Scene`, `SoLoud::SoLoud` |
| **Script** | `Morbi.Script` / `Morbi::Script` | `nethost`/`hostfxr` CoreCLR barındırma ve `NativeEngineFunctionTable` v3 | `Morbi::Core`, `Platform`, `Asset`, `Scene`, `Physics`, `Audio`, `UI`, `nethost.lib` |
| **UI** | `Morbi.UI` / `Morbi::UI` | Edge WebView2 GDI hole-punching compositor, `BinaryIPCBridge` (`'MUIB'`), `GameUIRenderer` | `Morbi::Core`, `IO`, `Platform`, `RHI`, `Render`, `Scene`, `nlohmann_json`, `dcomp`, `dxgi`, `user32`, `ole32`, `comdlg32` |
| **Runtime** | `Morbi.Runtime` / `Morbi::Runtime` | `EngineRuntime` ana motor döngüsü, C ABI host exports, `ProjectBuilder` | Tüm üst modüller |

### 1.3 CMake Dış Bağımlılıkları ve Yan Hedefler (`cmake/MorbiDependencies.cmake`)
* **`MorbiManagedBuild` Hedefi:** `Tools/dotnet` (veya sistem `dotnet`) ile `Managed/Morbi.Engine/Morbi.Engine.csproj`'i `Release` olarak derleyip `build/bin/Managed/` içine çıkarır (`DOTNET_CLI_TELEMETRY_OPTOUT=1`, `DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1`).
* **Yerel / FetchContent Bağımlılıklar:** `nlohmann_json` (3.11.3), `SDL3` (3.2.0), `Vulkan::Headers` (`VK_NO_PROTOTYPES` + `VK_USE_PLATFORM_WIN32_KHR`), `DXC` (`dxc.exe`, `dxcompiler.dll`, `dxil.dll`), `.NET Host Pack` (`nethost.h` / `nethost.lib`), `MorbiSoLoud` (statik kütüphane + Miniaudio/Null arka uç + `stb_vorbis` + `dr_impl` WAV), `JoltPhysics` v5.2.0, `Morbi::Assimp` (+ `zlibstatic`) ve `WebView2Loader.dll`.

---

## 2. Komut Satırı Bayrakları (CLI) ve Başlatma Mimarisi

### 2.1 Pure C ABI Başlatıcı (`Engine/Source/Runtime/include/Morbi/Runtime/EngineRuntime.hpp`)
Editör (`MorbiEditor.exe`), Proje Örneği (`Samples/Sandbox/src/main.cpp`) ve her projeye özel derlenen açık C++ Launcher (`LauncherTemplate/src/main.cpp`) doğrudan C++ sınıflarına statik bağlanmaz. Çalışma zamanında `LoadLibraryExW("Morbi.Runtime.dll")` + `GetProcAddress` ile şu iki sembolü çağırır:
* `Morbi_Runtime_GetEngineVersion()`
* `Morbi_Runtime_LaunchProject(const MorbiHostLaunchArgs* args)` (`Argc`, `Argv`, `ProjectFilePath`, `EngineBinDir`)

### 2.2 Desteklenen CLI Parametreleri (`EngineRuntime.cpp`, `Editor/src/main.cpp`)
| Bayrak | İşlevi |
| :--- | :--- |
| `--editor` | Motoru WebView2 + React Editör arayüzü ile başlatır (`MorbiEditor.exe` bu bayrağı otomatik ekler). |
| `--project <yol>` | Açılacak `game.morbiproj` proje tanım dosyasının yolunu belirler. |
| `--api vulkan \| vk \| dx12 \| d3d12 \| null` | `game.morbiproj` içindeki `default_rhi` ayarını geçersiz kılarak grafik arka ucunu seçer. |
| `--build-game <çıktıDizini>` | Başsız/otomatik olarak `ProjectBuilder::BuildGame` çalıştırıp bağımsız oyun paketi üretir. |
| `--smoke-test` | Tüm doğrulama fazlarını (Core, IO/ChunkedPak, DXC, ECS, Scene/Asset, Animasyon/Skinning, GameUI, Editör/IPC, NativeBridge) otomatik test edip `[PASS]` logları yazar. |

---

## 3. Morbi.Core — Bellek, Matematik, Zamanlayıcı, Ayarlar ve Günlükleme

### 3.1 Bellek Ayırıcılar (`Core/Memory/`)
* **`LinearAllocator` (`LinearAllocator.hpp`):**
  * Varsayılan hizalama `kDefaultAlignment = 64` byte (cache line hizalı).
  * `Allocate(size, alignment)`, şablonlu `Emplace<TValue>(args...)` ve `Reset()` metotları ile kare içi sıfır parçalanmalı arena tahsisi.
* **`GenerationalPool<TValue, TTag>` & `GenerationalHandle<TTag>` (`PoolAllocator.hpp`):**
  * Tüm GPU (Buffer, Texture, Pipeline) ve ECS/Motor kaynaklarını kuşak sayacı (`Generation`) içeren hafif kulplarla (`GenerationalHandle`) yönetir; silinmiş kaynağa eski kulpla erişimi imkansız kılar.

### 3.2 Çok Çekirdekli İş Zamanlayıcı (`Core/Task/TaskScheduler.hpp`)
* **`WorkerQueue` & `TaskScheduler`:**
  * Her işçi iş parçacığı için ayrı çift uçlu kuyruk (`deque`) barındıran **Work-Stealing** zamanlayıcı.
  * `TaskCounter` senkronizasyonu ile `Submit(TaskFn)`, paralel döngüler için `ParallelFor(count, batchSize, body)`, `Wait(counter)` ve `WaitIdle()` metotları.

### 3.3 SIMD Matematik ve Sıkı Paketlenmiş Dönüşümler (`Core/Math/Math.hpp`)
* SSE2/AVX2 (`MORBI_SIMD_SSE2`) hızlandırmalı vektör ve matris cebiri:
  * `Vec2` (8B), `Vec3` (12B — `Zero`, `One`, `Up`, `Down`, `Forward`, `Back`, `Right`, `Left` sabit yönleri), `alignas(16) Vec4` (16B), `Quat`/`Quaternion` (16B — `Identity`), `alignas(16) Mat4` (64B — row-major `At(row, col)`).
  * `Morbi::Math::Transform`: `Position` (Vec3) + `Rotation` (Quat) + `Scale` (Vec3) = **tam 40 byte** (`static_assert(sizeof(Transform) == 40)`).

### 3.4 Kimlik, Ayarlar, Sonuç ve Loglama
* **`StringId` (`Core/StringId.hpp`):** `kOffsetBasis = 14695981039346656037ULL`, `kPrime = 1099511628211ULL` kullanarak derleme zamanında (`constexpr`) 64-bit FNV-1a hash üretir (`"..."_sid` literal operatörü ve `std::hash<StringId>` desteği).
* **`Result<TValue, TError>` (`Core/Result.hpp`):** İstisna (exception) yerine tür güvenli hata/değer dönüş tipi (`Success`, `Failure`, `IsSuccess`, `Value`, `ConsumeValue`, `Error`).
* **`Log` (`Core/Log.hpp`):** `Trace`, `Info`, `Warn`, `Error`, `Fatal` seviyeleri; `SetMinLevel` filtreleme ve editör konsoluna canlı log akışı sağlayan `SetCallback(LogCallbackFn)`.
* **`EngineSettings` (`Core/EngineSettings.hpp`):**
  * Thread-safe singleton (`EngineSettings::Global()`).
  * `GraphicsSettingsData` (C# blittable POD): `VSyncEnabled` (`true`), `WindowMode` (`Windowed=0`, `Borderless=1`, `Fullscreen=2`), `MsaaSamples` (`1`), `ShadowQuality` (`Off=0..Ultra=4`, varsayılan `High`), `ResolutionWidth` (`1280`), `ResolutionHeight` (`720`), `TargetFramerate` (`-1` = sınırsız).
  * `SettingsDirtyFlags` (`VSync`, `WindowMode`, `Resolution`, `TargetFramerate`, `MSAA`, `ShadowQuality`, `All`) ile `RegisterListener` aboneliklerine anlık bildirim; JSON yükle/kaydet (`LoadFromJsonFile`, `SaveToJsonFile`) ve `Morbi_Settings_*` C ABI export'ları.

---

## 4. Morbi.IO — VFS, İkili Akış, FileWatcher ve Şifreli/Chunked MorbiPak

### 4.1 `PathService` (Sanal Dosya Sistemi - VFS)
* Desteklenen sanal protokol ön ekleri:
  * `engine://` — Merkezi motor çalışma dizini
  * `editor://` — Editör varlıkları
  * `templates://` — Sahne ve proje şablonları (`Templates/`)
  * `tools://` — Taşınabilir araç zinciri (`Tools/`)
  * `project://` — Aktif proje kök dizini
  * `assets://` — Aktif projenin kendi `Assets/` dizini (her proje açıldığında izole edilir)
  * `scripts://` — Aktif projenin kendi `Scripts/` dizini
  * `cache://` — Derlenmiş shader, Zig önbelleği ve streamed pak dosyaları önbelleği
* `MountPakIndex` ve `ResolveOrStreamToCache` ile fiziksel dosya yoksa doğrudan bağlı chunked `.mpak` içinden önbelleğe çıkarıp yol döndürür.

### 4.2 Şifreli & Parçalı (Chunked) `MorbiPak` (`Engine/Source/IO/include/Morbi/IO/MorbiPak.hpp`)
* **Kategorileştirme (`PakCategory` & `CategorizeAssetPath`):** Varlıkları otomatik olarak `Scenes`, `Models`, `Textures`, `Materials`, `Audio`, `Scripts`, `Prefabs`, `EngineContent`, `Other` sınıflarına ayırıp chunk içinde gruplar.
* **Şifreleme (`PakEncryption::ChaCha20Sha256`):**
  * `GenerateExportKey()` ile proje build'ine özgü rastgele kriptografik anahtar (`Content/pakkey.bin`) oluşturur.
  * Her giriş için düz metin ve şifreli veri `PlaintextSha256`, `PayloadSha256`, `NonceHex` ve `AuthTagHex` bütünlük doğrulaması tutulur.
* **Chunked Paketleme (`WriteChunkedPaks`, `LoadChunkedPakIndex`, `ReadChunkedPakFile`):**
  * Varsayılan parça boyutu tavanı: **`MaxChunkSizeBytes = 256 MB`** (`256 * 1024 * 1024`). Çok GB'lık projelerde belleği boğmaz.
  * Çıktı: `pakchunk_*.mpak` dosyaları + merkezi JSON harita (`pakchunks.index.json`).
  * `VirtualPathIndex`: Sanal yoldan dosya ofsetine doğrudan eşleme ile **O(1) seek+read streaming (`StreamReadPakEntry`)**.

### 4.3 `BinaryReader` / `BinaryWriter` ve `FileWatcher`
* `BinaryStream.hpp`: `std::span<const std::byte>` üzerinde sınır kontrollü, `memcpy` tabanlı güvenli tip/byte/string ikili serileştirme.
* `FileWatcher.hpp`: İşletim sistemi düzeyinde dosya oluşturma/değiştirme/silme olaylarını izleyerek sahne veya C# betik değişikliği bildirimlerini sağlar.

---

## 5. Morbi.Platform — Pencere Yönetimi ve Girdi Sistemi

### 5.1 `Window` (`Platform/Window.hpp`)
* `NativeWindowBackend`: `Win32`, `Cocoa`, `Wayland`, `X11` tanımı (`NativeWindowHandle` ile `Window` + `DisplayOrInstance` kulpu).
* High-DPI ölçekleme (`DpiScale()`), başlık/genişlik/yükseklik yapılandırması (`WindowDesc`), `ApplyWindowSettings(WindowMode, width, height)`, fare yakalama (`SetMouseCaptured`) ve imleç görünürlüğü (`SetCursorVisible`).

### 5.2 `InputState` (`Platform/InputState.hpp`)
* **Klavye:** `kMaxKeys = 512` tarama kodu kapasitesi; `IsKeyDown`, `WasKeyPressedThisFrame`, `WasKeyReleasedThisFrame`.
* **Fare:** 5 tuş (`MouseButton`: `Left`, `Right`, `Middle`, `X1`, `X2`), mutlak pozisyon (`MouseX`, `MouseY`, `MouseZ`), kare başına ham delta (`MouseDeltaX`, `MouseDeltaY`, `MouseDeltaZ`), yatay/dikey tekerlek (`MouseWheelX`, `MouseWheelY`), `SetMouseCaptured` (yakalandığında imleci otomatik gizler) ve `BeginFrame` durum devri.

---

## 6. Morbi.RHI, RHI.Vulkan & RHI.DX12 — Çoklu Grafik API ve Bindless Bellek

### 6.1 Soyut RHI Arabirimleri (`IDevice`, `ICommandList`, `RHITypes`)
* **Arka Uç Türleri:** `BackendAPI::Null`, `Vulkan`, `DirectX12`, `Metal`.
* **Kaynak Durumları (`ResourceState`):** `Undefined`, `Common`, `VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `ShaderResource`, `UnorderedAccess`, `RenderTarget`, `DepthWrite`, `DepthRead`, `CopySource`, `CopyDest`, `Present`.
* **Buffer Kullanımları (`BufferUsage`):** `Vertex`, `Index`, `Uniform`, `Storage`, `Indirect`, `TransferSrc`, `TransferDst`; bellek türleri `GpuOnly`, `CpuToGpu`, `GpuToCpu`.
* **Dinamik Render Pass (`ICommandList`):** `TransitionTexture`, `TransitionBuffer`, `BeginRendering(DynamicRenderPassDesc)` / `EndRendering`, `SetViewport`, `SetScissor`, `SetPipeline`, `PushConstants`, `BindVertexBuffer`, `BindIndexBuffer` (32/16-bit), `BindConstantBuffer`, `Draw`, `DrawIndexed`, `Dispatch`, GPU profiler için `BeginDebugMarker` / `EndDebugMarker`.

### 6.2 Vulkan 1.3 Arka Ucu (`Engine/Source/RHI.Vulkan/`)
* **Dinamik Yükleme:** `VK_NO_PROTOTYPES` + `VulkanDispatch.hpp` ile `vulkan-1.dll` çalışma zamanında yüklenir; sistemde Vulkan SDK olmasa da derlenir ve sürücü üzerinden çalışır.
* **Kapasite & Özellikler:** `kMaxFramesInFlight = 2`, **4096 Bindless kaynak slotu** (`kMaxBindlessResources = 4096`), **256 Bindless UBO slotu** (`kMaxBindlessUniformBuffers = 256`), Vulkan 1.3 Dynamic Rendering ve Timeline Semaphore (`GetCompletedTimelineValue()`).

### 6.3 DirectX 12 Arka Ucu (`Engine/Source/RHI.DX12/`)
* **Altyapı:** `d3d12.lib`, `dxgi1_6`, `WRL ComPtr`.
* **Kapasite & Bellek Yönetimi:** `kMaxFramesInFlight = 2`, **4096 Bindless tanımlayıcı**, 256 RTV, 64 DSV.
* **`DX12MemoryAllocator`:** D3D12MA tarzı **32 MB Default GPU Heap** (`D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES`) üzerinde Placed/Transient kaynak yerleşimi.

---

## 7. Morbi.Shader & Morbi.Render — DXC, RenderGraph ve 3D Gölgelendirme

### 7.1 Çift Hedefli HLSL Derleyici & Yansıma (`ShaderCompiler.hpp`)
* **Tek Kaynak -> İki Makine Kodu:** Taşınabilir `Tools/dxc/dxc.exe` CLI aracılığıyla tek HLSL kaynağını iki hedefe derler:
  * **`ShaderTargetFormat::DXIL`:** `vs_6_0`, `ps_6_0`, `cs_6_0` profilleri ve `-D MORBI_DX12=1`.
  * **`ShaderTargetFormat::SPIRV`:** `-spirv -fspv-target-env=vulkan1.2` ve `-D MORBI_VULKAN=1`.
* **Reflection (`ShaderReflectionData`):** Stage, EntryPoint, PushConstantSizeBytes, ThreadGroupSizeX/Y/Z, `UsesBindlessHeap` ve `ReflectedBinding` listesi (`ConstantBuffer`, `ShaderResource`, `UnorderedAccess`, `Sampler`, `PushConstants`, register slot/space, unbounded bindless `0xFFFFFFFF` dizileri).

### 7.2 İki Katmanlı `ShaderCache` (`ShaderCache.hpp`)
* `ContentHash` tabanlı bellek içi (`m_memoryCache`) + disk (`m_cacheDirectory`) önbellekleme; `MemoryHits`, `DiskHits`, `Compilations` istatistik takibi ve thread-safe `std::mutex` koruması.

### 7.3 `RenderGraph` ve Kare Yürütme Akışı (`RenderGraph.hpp`, `EngineRuntime.cpp`)
* **Otomatik Kaynak Ömrü ve Aliasing:**
  * `ImportTexture` (swapchain backbuffer gibi dış kaynaklar), `CreateTransientTexture` (kare içi geçici dokular), `AddPass`.
  * `Compile()` aşamasında her geçici dokunun `FirstPass` ve `LastPass` ömür aralığını hesaplar; örtüşmeyen dokuların GPU belleğini yeniden kullanır (**Aliased Memory**).
  * `RenderGraphCompileStats`: `TotalPasses`, `TransientTexturesCreated`, `AliasedMemoryBlocksReused`, `SynthesizedBarriers`, `UnaliasedTransientBytes`, `AliasedTransientBytesPeak`.
* **Motorun Kare İçi Pass Zinciri:**
  1. **`GBufferPass`**: `TransientGBufferColorA` ve `TransientSceneDepth` yazımı.
  2. **`LightingReadPass`**: Aydınlatma okuma geçişi.
  3. **`PostProcessWritePass`**: `TransientPostProcessB` son işleme geçişi.
  4. **`GameUIHudPass`**: `GameUIRenderer::AddRenderGraphHUDPass` ile offscreen HUD/Gizmo dokusu üretimi.
  5. **`FinalCompositePass`**: 3D görüntü üzerine offscreen HUD/Gizmo dokusunun alpha blending kompozisyonu.
* **3D Gölgelendirme Özellikleri:**
  * Bindless doku indeksleri üzerinden Albedo (`AlbedoTexIndex`), Tangent-Space Normal Map (`NormalTexIndex` + MikkTSpace TBN matrisi) ve Specular Mask (`SpecularTexIndex`).
  * Roughness bazlı Blinn-Phong parıltı (`shininess = lerp(96.0, 12.0, roughness)`), çift yönlü ana/dolgu ışık, Fresnel-tarzı Rim Lighting ve gökyüzü/zemin yarımküre ortam ışığı.
  * **GPU Skeletal Skinning:** `BoneIDs[4]` + `BoneWeights[4]` ile 100 kemiğe kadar row-vector (`mul(float4, skinMatrix)`) deformasyon; Vulkan'da bindless `g_BoneBuffers[boneBufIdx]`, DX12'de root CBV desteği.

---

## 8. Morbi.Asset — Model, Doku, `.mAsset` v2 ve İskelet Animasyon

### 8.1 `AssetRegistry` (`AssetRegistry.hpp`)
* 64-bit deterministik FNV-1a tabanlı `AssetUUID` (sanal yoldan otomatik üretilir).
* Desteklenen türler: `StaticMesh`, `Texture2D`, `Material`, `AudioClip`, `Scene`, `Script`.
* `ScanDirectory` ile `.obj`, `.gltf`, `.glb`, `.fbx`, `.png`, `.jpg`, `.tga`, `.mscene.json` taraması; `FindByUUID`, `FindByVirtualPath`, `SaveToJsonFile`.

### 8.2 `ModelImporter` & 92-Byte Vertex Düzeni (`ModelImporter.hpp`)
* **Assimp + Yerleşik Ayrıştırıcılar:** `.fbx`, `.gltf`, `.glb`, `.obj` içe aktarma (`ImportFromFile`, `ImportFromOBJText`, `ImportFromGLTFJson`, `ImportAnimationsFromFile`).
* **MikkTSpace Teğet Çözücü (`ComputeMikkTSpaceTangents`):** Tangent, `TangentSign` (±1.0 el yönü) ve Bitangent hesaplar.
* **`StaticMeshVertex` (Tam 92 Byte, `#pragma pack(1)`):**
  * `Position` (`Vec3`, 12B) + `Normal` (`Vec3`, 12B) + `UV` (`Vec2`, 8B) + `Tangent` (`Vec3`, 12B) + `TangentSign` (`f32`, 4B) + `Bitangent` (`Vec3`, 12B) + `BoneIDs[4]` (`i32[4]`, 16B) + `BoneWeights[4]` (`f32[4]`, 16B) = **92B**.
  * Tepe noktası başına 4'ten fazla kemik varsa en küçük ağırlığı değiştirip (`AddBoneData`) toplamı normalize eder (`NormalizeBoneWeights`).
* **`ImportedMeshData`:** Vertices, Indices, Submeshes (`BoundsMin/Max`, `MaterialIndex`), Materials (`MaterialDescriptor`), `BoneMapping`, `BoneOffsets` (Inverse bind pose), `SkeletonHierarchy` (`SkeletonNode` ağacı), `Animations` (`AnimationClip`), `BoundingBoxMin/Max`.

### 8.3 `TextureImporter` & `MaterialInstance` (`TextureImporter.hpp`, `Material.hpp`)
* `.png`, `.jpg`, `.tga`, `.bmp` içe aktarma (`ImportFromFile`, `ImportFromMemory`); tüm dokuları sıkı paketlenmiş 4-kanallı RGBA8 formatına çevirip `UploadToGPU` ile bindless GPU dokusuna yazar.
* `GenerateProceduralCheckerboardTGA`: Test ve varsayılan yüzeyler için 32-bit RGBA TGA üretir.
* `MaterialInstance`: Albedo, Normal ve Specular RHI doku kulpları + Bindless slotları + `BaseColorFactor`, `Roughness`, `Metallic`.

### 8.4 `CookedAssetSerializer` (`.mAsset` v2) (`CookedAssetSerializer.hpp`)
* `kCookedAssetMagic = 0x4D415354` (`'MAST'`), `kCookedAssetVersion = 2`.
* **32-byte `CookedAssetHeader`** + ikili payload: `CookStaticMeshToFile`, `LoadCookedStaticMesh`, `CookTextureToFile`, `LoadCookedTexture`, `UploadMeshToGPU` (`GPUMeshBuffers`), `DestroyGPUMeshBuffers`.

### 8.5 İskelet Animasyon Değerlendirici (`Animation.hpp`, `AnimationEvaluator.hpp`)
* **`AnimationClip`:** `Duration` (tick cinsinden), `TicksPerSecond` (varsayılan 24.0), kemik başına `BoneAnimationChannel` (`Positions`, `Rotations`, `Scales` anahtar kareleri).
* **`AnimationEvaluator`:**
  * `CrossFade(newClip, blendDurationSeconds)`: İki animasyon arasında smoothstep eğrisi (`t*t*(3-2*t)`) ile harmanlama.
  * **Kesintili Atalet Pozu (Inertial Pose Snapshot):** Devam eden bir geçiş sırasında üçüncü bir animasyona geçilirse `m_SnapshotLocalPos/Rot/Scale` yakalanır; sıfıra dönüş veya ani poz sıçraması yaşanmaz.
  * `InPlaceRootMotion`: Kök düğümün X/Z ötelemesini sıfırlayarak karakterin animasyon yüzünden sahnede kaymasını engeller.
  * **Kemik Soketleri (`GetNodeGlobalTransform`):** Seçili bir kemiğin (ör. `mixamorig:RightHand`) dünya/model uzayındaki matrisini döndürür.
  * **`PerObjectBuffer`:** GPU Skinning için `alignas(16) Mat4 BoneMatrices[100]` = **6400 byte** buffer doldurur (`FillGPUBuffer`).

---

## 9. Morbi.Scene — Sayfalı Sparse-Set SoA ECS, Hiyerarşi, JSON ve PIE Snapshot

### 9.1 `SparseSet<TComponent>` (`SparseSet.hpp`)
* `kPageSize = 1024` sayfalı seyrek dizi (seyrek varlık ID'leri için bellek israfını önler) + bitişik yoğun (`m_denseComponents`, `m_denseEntities`) diziler (O(1) ekleme/silme, swap-and-pop).

### 9.2 9 Yerleşik Bileşen ve Tam Bellek Boyutları (`Components.hpp`)
| Bileşen | Boyut | POD / C# `Span<T>` | İçerdiği Alanlar |
| :--- | :--- | :--- | :--- |
| **`TransformComponent`** | **40 B** | Evet | `Math::Transform` (`Position`, `Rotation`, `Scale`) |
| **`HierarchyComponent`** | **32 B** | Evet | `Parent`, `FirstChild`, `NextSibling`, `PrevSibling` (`EntityId`) |
| **`CameraComponent`** | **32 B** | Evet | `FovDegrees` (60), `NearPlane` (0.1), `FarPlane` (500), `AspectRatio` (16:9), `IsActive`, `Priority`, `ProjectionMode` (0=Persp, 1=Ortho), `OrthoSize` (10) |
| **`MeshRendererComponent`** | **32 B** | Evet | `MeshAssetId`, `MaterialAssetId`, `SubmeshIndex`, `BindlessVertexBufferIndex`, `BindlessIndexBufferIndex`, `Flags` |
| **`LightComponent`** | **32 B** | Evet | `LightType` (Directional=0, Point=1, Spot=2), `Color` (`Vec3`), `Intensity`, `Range`, `SpotInnerConeRadians`, `SpotOuterConeRadians` |
| **`ScriptComponent`** | **32 B** | Evet | `ScriptClassId`, `ManagedGCHandle`, `TickInterval`, `AngularSpeed`, `Enabled` |
| **`RigidBodyComponent`** | **44 B** | Evet | `LinearVelocity`, `Mass`, `HalfExtents`, `Restitution`, `Friction`, `JoltBodyId`, `MotionType` (Static/Kinematic/Dynamic), `Shape` (Box/Sphere/Capsule), `IsAwake`, `UseGravity` |
| **`AudioSourceComponent`** | **32 B** | Evet | `SoundAssetId`, `Volume`, `Pitch`, `MinDistance`, `MaxDistance`, `ActiveVoiceHandle`, `Spatial3D`, `Looping`, `PlayOnStart` |
| **`AnimatorComponent`** | Nesne | Hayır (C++ & Köprü) | `CurrentClip`, `PreviousClip`, `Evaluator`, `GpuBoneBuffer`, `GpuBuffer`, `IsPlaying`, `PlaybackSpeed`, `NeedsGpuUpload` |

* **Yerleşik Prosedürel Mesh Sabitleri:** `kProceduralCubeMeshId = 0x1000C0BE01ull`, `kProceduralCameraMeshId = 0x1000CA3001ull`.

### 9.3 `Scene` Yönetimi (`Scene.hpp`)
* **Varlık Yaşam Döngüsü:** Kuşaklı `EntityId` geri dönüşümü (`m_freeIndices`), `EntityDestroyedCallback`, isimle arama (`FindEntityByName`), ebeveyn-çocuk bağlama (`SetParent`, `GetParent`, `GetChildren`).
* **Zero-Copy C# Arena Export (`GetNativeArenaView` & `MORBI_SCENE_C_API`):** `DenseComponentsPtr`, `DenseEntitiesPtr`, `Count`, `ComponentStrideBytes` döndürür.
* **Sistem Güncellemesi:** `UpdateScriptSystem`, `UpdateAnimationSystem`, `UpdateTransformHierarchySystem` (`TaskScheduler` ile paralel `m_worldMatrices` hesabı).
* **Serileştirme ve PIE Rollback:** `.mscene.json` kaydetme/yükleme (`SaveToJsonFile`, `LoadFromJsonFile`), editörden oyuna geçerken tam sahne klonu alma ve dönüşte geri yükleme (`CreateSnapshot` / `RestoreSnapshot`).

---

## 10. Morbi.Physics & Morbi.Audio — Jolt Physics ve SoLoud 3D Ses

### 10.1 `PhysicsWorld` (`PhysicsWorld.hpp`)
* **Jolt Physics v5.2.0** tabanlı çok çekirdekli fizik dünyası (`BroadPhaseLayers::kNonMoving=0 / kMoving=1`).
* **Sabit Adımlı Döngü:** Varsayılan `1.0f / 60.0f` sn akümülatör; `StepSimulation(deltaTime, scene, scheduler)` yeni `RigidBodyComponent` varlıklarını Jolt'a senkronize eder ve güncellenen pozisyon/rotasyonları doğrudan `TransformComponent` dizisine yazar.
* **Sorgu ve Etkileşim:** `AddLinearImpulse`, `SetLinearVelocity`, `Raycast` (`RaycastHit`: HitEntity, Point, Normal, Distance, JoltBodyId), `GetRecentCollisions` (`CollisionPairEvent`: EntityA/B, ContactPoint, ContactNormal, PenetrationDepth), `ResetSimulation`, `PhysicsStats`.

### 10.2 `AudioWorld` (`AudioWorld.hpp`)
* **SoLoud + Miniaudio** arka ucu (başsız testler için `Initialize(headlessNoSoundDevice = true)` modu).
* `PreloadSound`, `PlayOneShot2D`, `PlayOneShot3D`, `SetListenerTransform(pos, forward, up)`, `Update(deltaTime, scene)` ile `AudioSourceComponent` 3D ses pozisyonlarını günceller.
* **`GenerateProceduralToneWav`:** Testler ve ses efektleri için diskte sentetik PCM `.wav` tonu üretir.

---

## 11. Morbi.Script & Managed SDK — .NET 10 CoreCLR, Roslyn 5.12.0 ve Zero-Copy Köprü

### 11.1 `ScriptEngine` & `NativeEngineFunctionTable` v3 (`ScriptEngine.hpp`)
* `nethost.lib` + `hostfxr` kullanarak taşınabilir `Tools/dotnet` (`.NET 10.0.13`) çalışma zamanını C++ içine gömer.
* **`NativeEngineFunctionTable` (`ApiVersion = 3`):** C# tarafında `delegate* unmanaged[Cdecl]<...>` olarak çağrılan P/Invoke yükü olmayan doğrudan C fonksiyon işaretçileri:
  * **Çekirdek/Arena:** `LogInfo`, `GetComponentArenaView`.
  * **Fizik/Ses/Ayar:** `AddLinearImpulse`, `SetLinearVelocity`, `Raycast`, `PlaySpatialSound3D`, `SetVSync`.
  * **HUD Data-Binding:** `SetGameUIProperty`, `TriggerGameUIEvent`.
  * **Sahne & ECS (v3):** `FindEntityByName`, `CreateEntity`, `DestroyEntity`, `IsEntityAlive`, `SetEntityParent`, `GetEntityParent`, `GetEntityName`, `SetEntityName`, `GetAliveEntityCount`, `HasComponent`, `AddComponent`, `RemoveComponent`, `LoadScene`, `SaveScene`, `ClearScene`.
  * **Animasyon:** `PlayAnimation`, `PauseAnimation`, `StopAnimation`, `SetAnimationClip`, `SetAnimationSpeed`, `IsAnimationPlaying`, `CrossFadeAnimationClip`, `IsAnimationBlending`.
  * **Girdi & Fare:** `IsKeyDown`, `WasKeyPressed`, `WasKeyReleased`, `IsMouseButtonDown`, `WasMouseButtonPressed`, `WasMouseButtonReleased`, `GetMousePosition`, `GetMouseDelta`, `GetMouseWheelDelta`, `GetMousePosition3D`, `GetMouseDelta3D`, `SetMouseCaptured`, `IsMouseCaptured`, `SetCursorVisible`, `IsCursorVisible`.
  * **VFS:** `ResolveVirtualPath`.

### 11.2 `ManagedHost` & `CollectibleScriptLoadContext` (`ManagedHost.cs`)
* Açılışta C++/C# tüm blittable struct boyutlarını assert seviyesinde doğrular (`TransformComponent == 40`, `RigidBodyComponent == 44`, `PacketHeader == 16`, `WirePlayerState == 40`, `WireJoinPayload == 32`, diğer ECS bileşenleri `== 32`).
* **`CollectibleScriptLoadContext` (`isCollectible: true`):**
  * DLL dosyasını Windows'un kilitlememesi için **`MemoryStream`** üzerinden bellekten yükler (`File.ReadAllBytes` -> `LoadFromStream`).
  * `IMorbiScript` arayüzünü implemente eden somut tipleri otomatik bulup `OnStart()` ve her kare `OnUpdate(deltaTime)` çalıştırır.
  * `UnloadActiveScriptContext()` + `GC.Collect` + `WeakReference` doğrulamasıyla eski betik assembly'sini güvenle boşaltıp `HotReloadGeneration` sayacını artırır.

### 11.3 Gömülü Roslyn Canlı C# Derleyicisi (`ScriptCompiler.cs`)
* `Microsoft.CodeAnalysis.CSharp` **5.12.0** NuGet paketi motorla birlikte gelir; geliştirici bilgisayarında harici `.NET SDK` veya `msbuild` gerektirmez.
* `Project/Scripts/` altındaki tüm `.cs` dosyalarını tarar, TPA (`TRUSTED_PLATFORM_ASSEMBLIES`) içindeki temel .NET assembly'lerini + `Morbi.Engine.dll` referansını ekler; `Release` veya `Debug` modunda doğrudan **`Managed/GameScripts.dll`** (+ `.pdb`) üretir ve ayrıntılı tanısal hata/uyarı raporu döndürür.

### 11.4 C# Oyun SDK'sı (`Managed/Morbi.Engine/`)
* **`Script` / `IMorbiScript`:** `OnCreate`, `OnStart`, `OnUpdate(float deltaTime)`.
* **`Entity` & Bileşen Sınıfları (`Components.cs`, `Transform.cs`, `Animator.cs`):**
  * Zero-copy `Span<T>` erişimli `Transform` (`Position`, `Rotation`, `Scale`, `LookAt`, `SetPositionAndRotation`), `Camera`, `Light`, `MeshRenderer`, `RigidBody`, `AudioSource`.
  * `Animator` / `AnimatorComponent`: `Play`, `Pause`, `Stop`, `CrossFade(clipPath, blendSeconds)`, `SetClip`, `SetSpeed`, `IsPlaying`, `IsBlending`.
* **`Scene.cs` & `Input.cs`:** Varlık alias çözümü (`Player` <-> `RemyCharacter`, `MainCamera` <-> `Camera`, `Floor` <-> `CubeMesh`/`StaticGroundFloor`), `KeyCode` ve `MouseButton` (Back/Forward/Button1..5) API'si.
* **Yerleşik Kontrolcüler:**
  * `PlayerController.cs`: 13 animasyon durumu (`Idle`, `Walk`, `Run`, `Jump`, `Punch`, `WalkBackward`, `StrafeLeft/Right`, `CrouchIdle`, `CrouchWalkForward/Back/Left/Right`), yumuşak karakter dönüşü, yer değiştirme ve hız HUD property güncellemeleri.
  * `CameraController.cs`: Üçüncü şahıs kamera yörüngesi ve karakter takibi.
  * `OnlineMatchController.cs`: Sahnede `NetworkManager` varlığı bulunduğunda otomatik 60 Hz `MorbiNet` çok oyunculu oturum yöneticisi (`[F]` CustomRpc, `[G]` Ghost Botlar, `[R]` Yeniden Bağlan).
  * `SceneBootstrapper.cs`: Floor/Player/MainCamera başlangıç kurulumu ve geçici test varlığı temizliği.

---

## 12. Morbi.UI & MorbiEditor — WebView2, Binary IPC, React Panelleri ve GPU HUD/Gizmo

### 12.1 `WebView2Compositor` & GDI Delik Delme (`WebView2Compositor.hpp`)
* **Tek Dosyaya Gömülü Arayüz (`Editor/src/EmbeddedFrontend.rc`):** `Editor/Frontend` (React 18 + TS + Tailwind CSS + Vite) tek `dist/index.html` haline derlenip `MorbiEditor.exe` kaynağına (`RCDATA`) gömülür.
* **Airspace Çözümü (`SetViewportChildWindow` + `SetWindowRgn`):** WebView2 penceresi tüm ana pencereyi kaplarken ortadaki 3D Viewport dikdörtgeni GDI bölge çıkarmayla oyulur; alttaki izole `viewportChildHwnd` Vulkan/DX12 ile sıfır ek kopya maliyetiyle görünür. Açılır menüler (`sendMenuState` + DOM `getBoundingClientRect` + DPI ölçeği) açıldığında oyulma alanı menü alanını örtecek şekilde dinamik daraltılır.

### 12.2 64-Byte Hizalı Binary IPC Protokolü (`BinaryIPCBridge.hpp`, `nativeBridge.ts`)
* Paylaşılan bellek adı: `Local\MorbiEngine_UI_SharedArena` (512 KB varsayılan halka arabellek) + WebView2 `PostWebMessageAsBinary`.
* **Sabit Başlık (`BinaryPacketHeader` = 16 byte, Magic `'MUIB' = 0x4D554942`, Version `1`):**
| Opcode | İsim | Payload Boyutu | Veri İçeriği |
| :--- | :--- | :--- | :--- |
| `1` | `SyncEntityTransform` | **48 B** | `PackedEntityId (u64)`, `Pos[3]`, `RotQuat[4]`, `Scale[3]` |
| `2` | `ViewportResize` | **24 B** | `Width`, `Height`, `DpiScale`, `SharedTextureHandle (u64)` |
| `3` | `CameraOrbitInput` | **16 B** | `DeltaYaw`, `DeltaPitch`, `DeltaZoom`, `Buttons` |
| `4` | `EngineTelemetrySnapshot` | **32 B** | `FrameTimeMs`, `Fps`, `AliveEntities`, `PhysicsActiveBodies`, `HotReloadGeneration`, `RenderGraphPasses`, `AliasedTexturesReused`, `AudioVoicesPlayed` |
| `5` | `SelectEntity` | **8 B** | `PackedEntityId (u64)` |
| `6` | `ExecuteCommand` | Değişken | UTF-8 konsol/motor komutu |
| `7` | `ViewportPick` | **8 B** | `NormalizedX (f32)`, `NormalizedY (f32)` |

### 12.3 Editör Arayüz Panelleri (`Editor/Frontend/src/components/`)
* **`TopBar`:** `File`, `Edit`, `View`, `GameObject`, `Component`, `Window`, `Help` menüleri; 4 hazır sahne şablonu yükleme; **PIE Play / Pause / Step / Stop** kontrolleri; `F7` Hot Reload ve **Build Game** tetikleyicisi.
* **`QuickToolsRail`:** `Select (Q)`, `Translate (W)`, `Rotate (E)`, `Scale (R)`, `Terrain Sculpt`, `Foliage Density`, `Physics Snap/Magnet` ve Gizmo ayarları.
* **`HierarchyPanel`:** Canlı `scene_sync` varlık ağacı, arama kutusu, `+` menüsünden varlık ekleme ve sağ-tık bağlam menüsü (silme/çoğaltma).
* **`ViewportArea`:** `Scene`, `Game`, `Asset Browser`, `Visual Script`, `Material Editor`, `Build` sekmeleri; `Lit/Wireframe/Unlit`, `Perspective/Orthographic`, `World/Local` düğmeleri; Asset Browser'dan veya Windows Explorer'dan sürükle-bırak 3D model içe aktarma; tıklama ile 3D raycast varlık seçimi.
* **`InspectorPanel`:** Ad/etiket düzenleme, X/Y/Z Position/Rotation/Scale alanları, mevcut bileşen kartları (`MeshRenderer`, `Rigidbody`, `Script`, `Light`, `Camera`, `Animator`) ve dinamik `+ Add Component` açılır menüsü.
* **`AssetBrowserPanel`:** Canlı `assets_sync` (`Models`, `Textures`, `Scenes`, `Scripts`), arama, dosya rozetleri (`MESH`, `TEX2D`, `SCENE`, `CS`) ve sürükle-bırak desteği.
* **`ConsolePanel`:** `Console`, `Profiler`, `Debug` sekmeleri; `Info`/`Warning`/`Error` filtreleri; C++ `Core::Log` canlı akışı ve komut çalıştırma (`reload`, vb.).
* **`MiniMapPanel`:** `Scene View / Game View` 2D X/Z radarı; zoom/pan, Auto-Follow, grid/label aç-kapa ve haritada çift tıklama ile oyuncuyu ışınlama (`onTeleportPlayer`).
* **`StatusBar`:** PIE aktif/duraklatılmış/düzenleme göstergesi, aktif proje, `Windows`, `x64`, `Release` etiketi.

### 12.4 `GameUIRenderer` — GPU Prosedürel Oyun HUD & 3D Transform Gizmo
* **128-Byte Push Constant (`GameUIPushConstants`):** Tamamen shader üzerinde (5x7 bitmask font + geometrik çizim) offscreen `RenderGraph` geçişi olarak çizilir.
* **Oyun HUD Öğeleri:** Sağlık barı (`HealthProgress`), sağ üst canlı telemetri rozeti, nişangah (`CrosshairSpread`), ve klavye/fare odaklı Pause Menüsü (`RESUME MISSION`, `RELOAD WEAPONS`, `GRAPHICS SETTINGS`, `EXIT TO DESKTOP`).
* **3D Editör Transform Gizmo:** Edit modunda seçili varlığın 3D pozisyonunu viewport UV'sine projekte eder; `Translate` (X/Y/Z okları + XY/XZ/YZ düzlem kareleri + eksen harfleri), `Rotate` (Pitch/Yaw/Roll halkaları) ve `Scale` (küp uçlu eksenler + üniform merkez) çizer; C++ tarafında `GizmoDragAxis` etkileşimiyle sürükleyerek anlık `TransformComponent` güncellemesi yapar.

---

## 13. Morbi.Runtime & ProjectBuilder — %100 Taşınabilir Oyun Derleyici ve Açık C++ Launcher

`Engine/Source/Runtime/src/ProjectBuilder.cpp` ve `ThirdParty/Tools/` (çıktıda `Tools/`), hedef makinede Visual Studio, MSVC, harici CMake veya harici .NET SDK gerektirmeden bağımsız oyun üretir:

1. **Aşama `[1/4]` — Roslyn C# Betik Derleme:** `Project/Scripts/*.cs` dosyaları `ScriptEngine::CompileProjectScripts` ile `Managed/GameScripts.dll` olarak derlenir. (Ayrıca editörde her PIE başlatışında otomatik olarak derlenir.)
2. **Aşama `[2/4]` — Şifreli & Chunked `MorbiPak` Paketleme:** `Project/Assets/` taranır, `CategorizeAssetPath` ile sınıflandırılır, `GenerateExportKey()` ile `Content/pakkey.bin` yazılır ve `256 MB` üst limitli `ChaCha20-SHA256` şifreli `.mpak` parçaları + `Content/pakchunks.index.json` oluşturulur.
3. **Aşama `[3/4]` — Açık C++ Launcher Derleme:**
   * Proje kökünde `Launcher/` yoksa `EngineBinDir/Tools/Launcher/` şablonu (`CMakeLists.txt` + `src/main.cpp`) otomatik kopyalanır.
   * Öncelikli derleyici olarak paketli **`Tools/zig/zig.exe c++ -std=c++20 -O2 -DNDEBUG <src/*.cpp> -lole32 -o <ProjeAdi>.exe`** kullanılır (`ZIG_GLOBAL_CACHE_DIR` ve `ZIG_LOCAL_CACHE_DIR` izole edilir). Zig bulunamazsa paketli `Tools/cmake` yedeğine geçilir.
4. **Aşama `[4/4]` — Tam Taşınabilir Dağıtım Paketleme:** Tüm `Morbi.*.dll`, `Managed/` (ve `Morbi.Engine.dll`), `Tools/dotnet/` (`.NET 10.0.13` hostfxr/shared), `Tools/dxc/` (`dxc.exe`, `dxcompiler.dll`, `dxil.dll`) ve `game.morbiproj` çıktı klasörüne kopyalanır.

---

## 14. Örnek Proje Yapısı (`Samples/Sandbox/`)

*(Not: Eski `Templates/` proje/sahne şablonları ve eski `MorbiNetwork/` Python/Rust/C# ağ yığını kaldırılmıştır; yeni yerel C++20 `Morbi.Network` altyapısı `Engine/Source/Network/` altında geliştirilmektedir. `Samples/Sandbox/`, merkezi motor DLL'lerini dinamik çözümleyerek çalışan örnek proje başlatıcısını içerir.)*

---

## 16. Morbi Hub & Dağıtım Platformu (`hub_and_web_syss/`)

Motor sürümlerini merkezi bulut üzerinden dağıtmak, lisanslı donanımları yetkilendirmek ve yerel projeleri yönetmek için tasarlanmış üç parçalı ekosistem:

### 16.1 Morbi Hub Masaüstü İstemcisi (`hub_and_web_syss/hub/`)
* **Teknoloji:** Tauri 2 + Rust (`tokio`, `reqwest/rustls`, `sha2`, `zip`, `sysinfo`, `wmi`, `tauri-plugin-store`) + React 18 + Tailwind CSS + Framer Motion.
* **Özellikler:**
  * **Donanım Bağlı DRM (`hwid.rs`):** Windows'ta ayrı COM iş parçacığı üzerinde WMI (`Win32_BaseBoard` + `Win32_Processor`) ve çapraz platform `sysinfo` bilgileriyle kalıcı SHA-256 HWID üretir.
  * **Duraklatılabilir & Doğrulamalı İndirme (`downloader.rs`):** `/api/v1/download/{version}` isteğinden Authorization başlığını sızdırmadan R2 presigned URL'yi yakalar (`Policy::none()`), 2 MB bloklar ve HTTP `Range` başlıklarıyla Pause/Resume yapar, indirme sonunda **SHA-256 doğrulaması** yapıp ZIP'i `app_data_dir/engines/<version>/` altına açar.
  * **Güvenli Depo & Proje Yönetimi (`lib.rs`, `projects.rs`):** JWT ve yerel manifest önbelleğini `morbi_secure_store.json` içinde saklar; şablonlardan yeni proje üretir, `MorbiEditor.exe` başlatır ve bozuk kurulumları onarır.

### 16.2 Paket Yönetimi & DRM Dağıtım API'si (`hub_and_web_syss/api/`)
* **Teknoloji:** Go 1.23 (`go-chi/chi/v5`, `jackc/pgx/v5` PostgreSQL, `golang-jwt/jwt/v5`, `aws-sdk-go-v2/service/s3` Cloudflare R2, `x/crypto`).
* **Barındırma (`fly.toml`):** Fly.io `apimorbihub.fly.dev` (Docker + `1 GB RAM`, otomatik durma/başlatma, `/healthz`).
* **Rotalar (`/api/v1/`):**
  * Açık: `POST /api/v1/auth/login`.
  * Korumalı (`JWT + X-Morbi-HWID + aktif cihaz` `DRMMiddleware`): `GET /manifest.json`, `GET /download/{version}`.
  * Admin (`RequireAdmin`): `GET /admin/stats`, `GET/POST /admin/users`, `GET/POST/DELETE /admin/releases`, `POST /admin/releases/upload`, `POST /admin/releases/upload-url`, `GET /admin/devices`, `PATCH /admin/devices/{id}/status` + `/admin/*` altında gömülü SPA sunumu.

### 16.3 Yönetim Web Arayüzü (`hub_and_web_syss/admin-ui/`)
* **Teknoloji:** React 18 + TypeScript + Vite + Tailwind CSS.
* **Sekmeler & Yetenekler:**
  * **Releases:** İstemci tarafında SHA-256 hesaplama (`computeFileSha256`), ilerleme çubuklu doğrudan R2 arşiv yükleme, stabil/sürüm notu yönetimi ve silme.
  * **Users:** Yeni `tester`/`admin` hesabı açma ve rol takibi.
  * **Devices:** Kullanıcıların kayıtlı HWID cihazlarını görüntüleme ve cihaz erişim durumunu onaylama/askıya alma.
