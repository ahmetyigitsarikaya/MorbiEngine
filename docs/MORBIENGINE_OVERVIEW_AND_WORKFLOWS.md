# MorbiEngine — Özlü Mimari Rehberi ve Alt Sistemlerin İşleyiş Akışları

> **Kapsam Notu:** Bu doküman, `legacy/` hariç tutularak tüm MorbiEngine ekosistemindeki **her özelliği** özetler ve her modülün perde arkasında **nasıl işlendiğini** adım adım açıklar. Detaylı struct/API/sabit listesi için [MORBIENGINE_DETAILED_FEATURES.md](MORBIENGINE_DETAILED_FEATURES.md) dosyasına bakın.

---

## 1. Bütünsel Başlatma ve Modül Bağlama Akışı

MorbiEngine, **15 paylaşımlı C++20 DLL** (`Core`, `IO`, `Platform`, `Shader`, `RHI`, `RHI.Vulkan`, `RHI.DX12`, `Asset`, `Render`, `Scene`, `Physics`, `Audio`, `Script`, `UI`, `Runtime`), **.NET 10 CoreCLR + Roslyn** çalışma zamanı, gömülü **WebView2/React Editörü**, **`MorbiNetwork`** çok oyunculu sunucuları ve **`Morbi Hub`** dağıtım platformundan oluşur.

**Nasıl İşler?**
1. **Pure C ABI Önyükleme:** `MorbiEditor.exe` (`--editor`), `Samples/Sandbox` veya proje build'indeki açık C++ `<ProjeAdi>.exe` başlatıcısı hiçbir motor başlığına statik bağlanmaz; `LoadLibraryExW("Morbi.Runtime.dll")` + `GetProcAddress("Morbi_Runtime_LaunchProject")` ile motoru başlatır.
2. **CLI & Proje Yapılandırması:** `--project`, `--api (vulkan|dx12|null)`, `--build-game`, `--smoke-test` argümanları çözülür; `game.morbiproj` okunarak `Assets/` ve `Scripts/` yolları aktif projeye kilitlenir.
3. **VFS İzolasyonu (`PathService`):** `engine://`, `editor://`, `templates://`, `tools://`, `project://`, `assets://`, `scripts://` ve `cache://` kökleri bağlanır. Paketlenmiş (built) oyunda `Content/pakchunks.index.json` + `Content/pakkey.bin` algılanırsa `MountPakIndex` çağrılır ve varlıklar doğrudan şifreli `.mpak` parçalarından streaming edilir.

---

## 2. Çekirdek (Core) Altyapı Nasıl İşler?

* **Öngörülebilir Bellek (`LinearAllocator` & `GenerationalPool`):** Kare içi geçici tahsisler 64-byte cache-line hizalı `LinearAllocator` üzerinden yapılır ve kare sonunda sıfır maliyetle `Reset()` edilir. GPU tamponları, dokular ve pipeline'lar ise `GenerationalHandle<TTag>` döndüren havuzlarda tutulur; silinmiş kaynağa eski kulpla erişilirse generation uyuşmazlığıyla yakalanır.
* **Çok Çekirdekli İş Dağıtımı (`TaskScheduler`):** Her işçi thread kendi `WorkerQueue` deque yapısına sahiptir. `ParallelFor` veya `Submit` ile verilen işler boşta kalan çekirdekler tarafından iş çalma (**Work-Stealing**) yöntemiyle yürütülür; `TaskCounter` üzerinden beklenir.
* **Kimlik, Matematik, Hata ve Ayarlar:** `StringId` 64-bit FNV-1a hash'i derleme zamanında (`_sid`) üretir. Tüm API'ler exception fırlatmak yerine `Result<TValue, TError>` döndürür. `EngineSettings` (VSync, WindowMode, çözünürlük, MSAA, ShadowQuality, TargetFramerate) değiştiğinde `SettingsDirtyFlags` üzerinden kayıtlı pencere/RHI dinleyicilerini anında tetikler. `Morbi::Math` SSE2/AVX2 kullanır ve `Transform` yapısını C# uyumluluğu için tam **40 byte** sıkı paketler.

---

## 3. Veri Odaklı ECS, Fizik, Ses ve Girdi Döngüsü

**Nasıl İşler?**
1. **Sayfalı Sparse-Set SoA (`Scene`):** 9 bileşen (`Transform` 40B, `Hierarchy` 32B, `Camera` 32B, `MeshRenderer` 32B, `Light` 32B, `Script` 32B, `RigidBody` 44B, `AudioSource` 32B, `Animator`) 1024'lük sayfalara bölünmüş seyrek indeks + bitişik yoğun dizilerde tutulur. Bu sayede silme/ekleme O(1) iken sistem taramaları L1/L2 önbelleğe sığar.
2. **Fizik (`PhysicsWorld` - Jolt v5.2.0):** Her kare `StepSimulation(dt)` `1/60s` sabit zaman akümülatörünü tüketir; yeni `RigidBodyComponent` nesneleri Jolt `BodyInterface`'e kaydedilir (`Box/Sphere/Capsule`), çarpışmalar (`CollisionPairEvent`) ve raycast sorguları çözülüp sonuç pozisyon/rotasyonlar doğrudan yoğun `TransformComponent` dizisine yazılır.
3. **Hiyerarşi & Çok Çekirdekli Dönüşüm:** `UpdateTransformHierarchySystem`, ebeveyn-çocuk ilişkilerini (`HierarchyComponent`) takip ederek tüm `m_worldMatrices` tamponunu günceller.
4. **Ses (`AudioWorld` - SoLoud + Miniaudio):** Dinleyici kameranın konumu/yönü güncellenir ve `AudioSourceComponent` düğümleri 3D mekansal ses kanallarıyla senkronize edilir (testlerde `headlessNoSoundDevice` modu veya `GenerateProceduralToneWav` desteklenir).
5. **Girdi (`InputState`):** 512 klavye tarama kodu, 5 fare tuşu, tekerlek ve ham fare deltası her kare başında `BeginFrame()` ile devredilir; FPS/TPS kamera kontrolü için `SetMouseCaptured` imleci kilitleyip gizler.

---

## 4. RHI, Çift Hedefli Shader (DXC) ve RenderGraph İşleyişi

**Nasıl İşler?**
1. **RHI Seçimi ve Bindless Model:** `RHIFactory::CreateDevice`, Vulkan 1.3 (`vulkan-1.dll` dinamik dispatch, Timeline Semaphore, Dynamic Rendering) veya DirectX 12 (`ID3D12Device`, DXGI 1.6, 32 MB Placed Heap) başlatır (`NullDevice` yedektir). İki arka uç da **4096 slotlu Bindless kaynak tablosu** sunar.
2. **HLSL -> SPIR-V & DXIL (`ShaderCompiler` & `ShaderCache`):** Tek bir `.hlsl` kaynağı `Tools/dxc/dxc.exe` ile Vulkan için `SPIRV` (`-D MORBI_VULKAN=1`), DX12 için `DXIL` (`vs_6_0/ps_6_0/cs_6_0`, `-D MORBI_DX12=1`) olarak derlenir; register/push-constant yansıması çıkarılır ve içerik hash'i ile RAM + disk önbelleğine (`ShaderCache`) yazılır.
3. **Kare İçi `RenderGraph` Yürütmesi:**
   - Motor her kare `GBufferPass` -> `LightingReadPass` -> `PostProcessWritePass` -> `GameUIHudPass` -> `FinalCompositePass` düğümlerini ekler.
   - `Compile()` fazında kaynak ömürleri analiz edilerek örtüşmeyen geçici dokular aynı VRAM alanını paylaşır (**Memory Aliasing**) ve `ResourceState` geçiş bariyerleri otomatik sentezlenir.
   - 3D sahne gölgelendiricisi Blinn-Phong, Tangent-Space Normal Map, Specular Mask, Rim Light, Yarımküre Ortam Işığı ve **100 kemikli (4 ağırlık/vertex, 6400B UBO) GPU Skeletal Skinning** hesaplar.

---

## 5. Varlık (Asset), Animasyon ve Parçalı `MorbiPak` İşleyişi

* **Model & Doku Boru Hattı:** `ModelImporter`, Assimp ile `.fbx/.gltf/.glb/.obj` dosyalarını okuyup MikkTSpace teğetleri + kemik ağırlıklarıyla **92-byte `StaticMeshVertex`** dizisine çevirir. `TextureImporter` görselleri RGBA8'e açar. İkisi de `CookedAssetSerializer` ile `.mAsset` v2 (`'MAST'`) ikili formatında pişirilebilir. `AssetRegistry` tüm `assets://` dosyalarına deterministik 64-bit `AssetUUID` üretir.
* **İskelet Animasyon Harmanlama (`AnimationEvaluator`):** `CrossFade` çağrısı smoothstep (`t*t*(3-2*t)`) eğrisiyle iki klibi harmanlar. Bir geçiş bitmeden yeni geçiş tetiklenirse mevcut anlık poz `m_SnapshotLocalPos/Rot/Scale` içine yakalanır; böylece karakter pozu sıçramaz. `InPlaceRootMotion` kök kemik X/Z kaymasını engeller, `GetNodeGlobalTransform` ile el/ekipman soketleri alınır.
* **Chunked & Şifreli `MorbiPak`:** Paketleme sırasında `CategorizeAssetPath` varlıkları türlerine göre (`Scenes`, `Models`, `Textures`, `Audio`, vb.) sıralar, `ChaCha20-SHA256` (`Content/pakkey.bin`) ile şifreler ve **256 MB** üst sınırıyla parçalara (`pakchunk_*.mpak` + `pakchunks.index.json`) böler. Çalışma zamanında `StreamReadPakEntry` tüm paketi açmadan O(1) seek+read yapar.

---

## 6. .NET 10 CoreCLR, Gömülü Roslyn ve Canlı Hot-Reload Akışı

**Nasıl İşler?**
1. **Host Başlatma:** `ScriptEngine`, taşınabilir `Tools/dotnet` (`.NET 10.0.13`) çalışma zamanını `hostfxr` ile yükler ve C++ fonksiyon işaretçilerini (`NativeEngineFunctionTable` v3) C# `delegate* unmanaged[Cdecl]` tablosuna bağlar. Açılışta tüm C++/C# struct boyutları doğrulanır.
2. **Sıfır Kopyalı C# SDK:** C# `Transform`, `Camera`, `Light`, `MeshRenderer`, `RigidBody`, `AudioSource` sınıfları C++ dizilerini kopyalamadan doğrudan `Span<T>` ile günceller. `PlayerController`, `CameraController`, `OnlineMatchController` ve `SceneBootstrapper` bu SDK üzerine kuruludur.
3. **Gömülü Roslyn Derleme & Kilitlenmeyen Hot-Reload:** `ScriptCompiler.cs` içindeki Roslyn `5.12.0`, harici .NET SDK gerektirmeden `Project/Scripts/*.cs` dosyalarını `Managed/GameScripts.dll` olarak derler. `ManagedHost`, DLL'i dosya kilidi oluşmaması için **`MemoryStream`** üzerinden `CollectibleScriptLoadContext (isCollectible: true)` içine yükler. Editörde **PIE Play**'e her basıldığında veya `F7` / `reload` komutu verildiğinde eski context boşaltılır (`Unload()`), betikler anında derlenip taze kodla çalıştırılır.

---

## 7. MorbiEditor, WebView2 Compositing, Binary IPC ve GPU HUD/Gizmo

**Nasıl İşler?**
1. **Tek Dosya Frontend + Delik Delme (Hole Punching):** React 18 + TS + Tailwind arayüzü Vite ile tek `dist/index.html` olarak `MorbiEditor.exe` `RCDATA`'sına gömülür. `WebView2Compositor`, ortadaki 3D sahne alanını Win32 `SetWindowRgn` ile oyarak alttaki Vulkan/DX12 child HWND'sini sıfır ek kopyalamayla görünür kılar; açılır menü açılınca bölge anında güncellenir.
2. **Hibrit İletişim (`BinaryIPCBridge` `'MUIB'` + JSON):** 60+ Hz frekanslı Transform gizmo sürükleme (`48B`), viewport resize (`24B`), kamera orbit (`16B`), telemetri (`32B`) ve 3D raycast picking (`8B`) paketleri JSON parser'a sokulmadan ikili halka arabelleği (`Local\MorbiEngine_UI_SharedArena`) ve `PostWebMessageAsBinary` üzerinden akar; `scene_sync`, `assets_sync` ve `Core::Log` olayları JSON olarak panellere (`TopBar`, `QuickToolsRail`, `HierarchyPanel`, `ViewportArea`, `InspectorPanel`, `AssetBrowserPanel`, `ConsolePanel`, `MiniMapPanel`, `StatusBar`) aktarılır.
3. **PIE Anlık Görüntü & Geri Sarma:** Editörden **Play**'e geçerken `Scene::CreateSnapshot()` alınır; **Stop** basıldığında oyun içi değişiklikler `RestoreSnapshot()` ile geri sarılır.
4. **GPU HUD & 3D Transform Gizmo (`GameUIRenderer`):** 128-byte Push Constant ve shader içi 5x7 bitmask font kullanarak RenderGraph offscreen pass üzerinde sağlık barı, telemetri, nişangah, pause menüsü ve edit modunda 3D Translate/Rotate/Scale gizmo çizip ana sahneye alpha-blend eder.

---

## 8. %100 Taşınabilir Oyun Build Hattı & Şablon İzolasyonu

**Build Game** komutu (veya `--build-game`) çalıştığında `ProjectBuilder::BuildGame` geliştirici makinesinde hiçbir SDK/IDE kurulumuna ihtiyaç duymadan 4 adımı işletir:
1. **`[1/4]`** Roslyn ile `Scripts/*.cs` -> `Managed/GameScripts.dll`.
2. **`[2/4]`** `Assets/` -> `ChaCha20-SHA256` şifreli (`Content/pakkey.bin`), kategorili, 256 MB chunk'lı `.mpak` dosyaları + `pakchunks.index.json`.
3. **`[3/4]`** Projeye kopyalanmış açık C++ `Launcher/` kaynağı, gömülü **`Tools/zig/zig.exe c++ -std=c++20 -O2`** ile derlenip `<ProjeAdi>.exe` üretilir (yedek olarak paketli `Tools/cmake`).
4. **`[4/4]`** `Morbi.*.dll`, `Managed/`, taşınabilir `Tools/dotnet/`, `Tools/dxc/` ve `game.morbiproj` çıktı dizinine paketlenir.

*(Tüm 4 şablon — `EmptyScene`, `ThirdPersonSandbox`, `OnlineArenaSandbox`, `LightingShowcase` — kendi izole `Assets/`, `Scripts/`, `Launcher/`, `game.morbiproj` ve `template.json` dosyalarıyla gelir.)*

---

## 9. Morbi Hub Dağıtım Akışı

* **Dağıtım Akışı (`hub_and_web_syss/`):**
  - Yönetici `admin-ui` (React) üzerinden motor zip sürümünün SHA-256 özetini hesaplayıp Fly.io (`apimorbihub.fly.dev`) üzerindeki Go API aracılığıyla **Cloudflare R2**'ya ve PostgreSQL'e kaydeder; HWID cihaz yetkilerini yönetir.
  - Son kullanıcı masaüstünde **Morbi Hub** (Tauri 2 + Rust + React) uygulamasını açar; `WMI`/`sysinfo` ile üretilen donanım HWID'si ve JWT ile kimlik doğrular, Go API'nin ürettiği R2 presigned URL'den HTTP `Range` bloklarıyla duraklat/devam-ettir destekli ve SHA-256 doğrulamalı kurulum yapar ve `MorbiEditor.exe`'yi başlatır.
