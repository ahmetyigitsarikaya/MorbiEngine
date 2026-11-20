# MorbiEngine — Shader, Rendering ve Animation Sistemleri Teknik Mimari Analizi

> **Kapsam:** Bu rapor; `Morbi.Shader`, `Morbi.RHI` (`Vulkan`/`DX12`), `Morbi.Render`, `Morbi.Asset`, `Morbi.Scene`, `Morbi.Script` ve `EngineRuntime.cpp` kaynak kodlarının doğrudan incelenmesine dayanır. Yalnızca kodda fiilen bulunan yapılar, sınırlar ve davranışlar raporlanmıştır.

---

## 1. Mevcut Mimarinin Röntgeni

### Modül Sorumlulukları ve Bağlantılar
* **`Morbi.Shader` (`ShaderCompiler`, `ShaderCache`):** `dxc.exe`'yi alt süreç (`CreateProcessW`) olarak çalıştırıp HLSL -> DXIL (`SM 6.0+`) ve SPIR-V (`vulkan1.2`) üretir; bellek/disk önbelleği tutar.
* **`Morbi.RHI` + `Morbi.RHI.Vulkan` / `Morbi.RHI.DX12` (`IDevice`, `ICommandList`):** Dynamic Rendering (`BeginRendering`/`EndRendering`), 4096 slotluk global Bindless heap (`space0` buffer, `space1` texture), timeline semaphore ve tek `GetActiveCommandList()` soyutlaması sunar.
* **`Morbi.Render` (`RenderGraph`):** Pass grafiği kurar, `ResourceState` geçişlerinden otomatik pre-pass barrier listesi üretir ve transient texture havuzunu yönetir.
* **`Morbi.Asset` (`ModelImporter`, `TextureImporter`, `AnimationEvaluator`, `CookedAssetSerializer`):** Assimp ile `.fbx`/`.glb`/`.gltf`/`.obj` import eder, 92-byte `StaticMeshVertex` üretir, `.morbimesh` cook eder, GPU skinning matrislerini (`PerObjectBuffer`, max 100 kemik) hesaplar.
* **`Morbi.Scene` (`Scene`, `Components`):** Sparse-Set SoA ECS havuzlarında `TransformComponent` (40B), `MeshRendererComponent`, `AnimatorComponent`, `LightComponent` ve `CameraComponent` tutar.
* **`Morbi.Runtime` (`EngineRuntime.cpp`):** Tüm render pass kurulumu, inline HLSL shader'lar, PSO oluşturma, kamera/ışık, culling'siz draw döngüsü ve GPU kaynak yüklemesi tek monolitik dosyada (`EngineRuntime.cpp`, ~5269 satır) toplanmıştır.

### Veri Akışı
```text
Assimp / Disk -> ModelImporter / CookedAssetSerializer -> GPUMeshBuffers (VB/IB + MaterialInstance)
                                                               |
Scene SoA (Transforms, MeshRenderers, Animators) ------------> EngineRuntime::TickFrame()
  ├─ Scene::UpdateAnimationSystem (CPU pose eval -> GpuBoneBuffer[100])
  └─ RenderGraph (Import backbuffer -> AddPass -> Compile -> Execute -> ICommandList -> Vulkan/DX12)
```

### Threading ve CPU/GPU İş Yükü Dağılımı
* **CPU Çoklu İş Parçacığı:** `Scene::UpdateAnimationSystem`, `TaskScheduler::ParallelFor` kullanır (`count >= 16` eşiğiyle); `ModelImporter` model cook işlemini paralel görev olarak çalıştırabilir.
* **Tek Thread Darboğazı:** Tüm `RenderGraph` kurulumu, barrier sentezi, materyal/bone buffer güncellemesi (`UpdateBufferData`) ve `DrawIndexed` gönderimi ana thread üzerinde sıralı çalışır; `IDevice` tek `GetActiveCommandList()` sunduğu için paralel command recording yoktur.
* **Shader Derleme:** `ShaderCompiler::Compile` senkron `CreateProcessW` ile bloklayıcıdır (asenkron worker kuyruğu yoktur).

---

## 2. Teknik Borç, Sorunlar ve Oyun Testi Kök Nedenleri

### Oyun Testinde Yaşanan Sorunların Kök Nedenleri
1. **Alt Entity'nin (`AmberVisual`) Hareket Etmemesi:** `EngineRuntime.cpp:4886` render sırasında `Scene::UpdateTransformHierarchySystem` çıktısı olan dünya matrisini değil, doğrudan yerel `activeScene.Transforms().TryGet(renderEid)` değerini (`Math::TRS(entityTr)`) kullanmaktadır.
2. **Texture'ların Yüklenmemesi (`TexturesUploaded=0`):** `ModelImporter.cpp:466-470`, `AssetsRoot` verilmediğinde yalnızca klasör adı `"models"` ise bir üst dizine çıkar; farklı klasör yapılarında `assets://Textures/` çözülemez.
3. **Amber'ın T-Pose / İskelet Uyumsuzluğu:** `ScriptEngine.cpp` başlangıçta tüm otomatik eklenen `AnimatorComponent`'lere `Remy.fbx` iskeletini atıyordu (düzeltildi; ancak genel bir asset-bazlı `SkeletonAsset` referansı hâlâ yoktur).

### Sistemik Teknik Borçlar
* **Monolitik Renderer (`EngineRuntime.cpp`):** Shader kaynakları C++ string literal olarak gömülüdür; sahne render mantığı `Morbi.Render` yerine `Morbi.Runtime` içindedir.
* **Bellek Sızıntısı (`RenderGraph.cpp:290-297`):** `CreateTransientBuffer` ile oluşturulan `m_buffers` kaynakları `m_transientTexturePool` gibi havuzlanmaz ve `Reset()`/`Execute()` sonrası `DestroyBuffer` çağrılmaz; her frame oluşturulan transient buffer'lar GPU belleğinde sızar.
* **Devre Dışı Bellek Aliasing (`RenderGraph.cpp:241`):** `AliasingPoolOffset = 0xFFFFFFFFFFFFFFFFULL` sabitlenmiştir; aliasing yalnızca `m_stats.AliasedTransientBytesPeak` metriğini hesaplar, gerçek GPU bellek paylaşımı yapmaz.
* **Gerçek Olmayan Reflection (`ShaderCompiler.cpp:180-260`):** `ReflectSourceAndBinary` SPIR-V/DXIL ikili reflection yerine HLSL kaynak kodu üzerinde `std::regex` araması yapar ve `PushConstantSizeBytes = 64` sabit döner (oysa `MeshPushConstants` 128 byte'tır).
* **Ölçeklenebilirlik:** Frustum/Occlusion culling yok, instancing yok, per-submesh `PushConstants` + `DrawIndexed` yapılıyor (`procedural_city_6.glb` tek entity'de 79 submesh = 79 draw call; 1000 mesh'te CPU submit darboğazı).

---

## 3. Shader System Analizi

| Alt Başlık | Mevcut Durum |
|---|---|
| **Compilation Pipeline** | `ShaderCompiler::Compile` her istekte temp `.hlsl` dosyası yazar, `dxc.exe`'yi `CreateProcessW` ile çağırır, çıktıyı okur ve temp dosyayı siler. Asenkron derleme yoktur; Windows dışı platformlarda (`dxc.exe`) süreç yolu uyarlanmamıştır. |
| **Reflection & Binding** | `ReflectSourceAndBinary` regex ile `: register(btus...)` tarar. `#ifdef` dallarını, struct padding/offset'lerini, CBV üye yerleşimini ve gerçek push-constant boyutunu çözemez. |
| **PSO Yönetimi** | RHI'da `CreateGraphicsPipeline` / `CreateComputePipeline` vardır; ancak merkezi bir `PipelineCache` veya hash tabanlı PSO yönetim katmanı yoktur. `EngineRuntime` açılışta sabit `compositePSO` ve `sceneMeshPSO` oluşturur. |
| **Cache & Hot-Reload** | `ShaderCache` bellek (`unordered_map<u64, ...>`) + disk (`.morbishader` ikili cache) destekler; `ComputeRequestHash` kaynak kod + entry + stage + target + makroları hash'ler. `#include` dosya içeriği değişikliği hash'e dahil edilmez; runtime shader hot-reload izlemesi yoktur. |
| **Material & Varyantlar** | `MaterialInstance` yalnızca sabit `sceneMeshPSO`'ya `Albedo/Normal/Specular` bindless indekslerini ve `Roughness`'ı `MeshPushConstants` üzerinden gönderir. Shader varyantı, permutation veya `ShaderFeature` anahtarı yoktur. |
| **DXC / SPIR-V** | DXC üzerinden dual-target (`DXIL` + `SPIRV`) üretimi çalışmaktadır. `glslang` entegrasyonu yoktur (ihtiyaç da yoktur, DXC her iki hedefi üretir). |

---

## 4. Rendering System Analizi

| Alt Başlık | Mevcut Durum |
|---|---|
| **Render Graph** | `RenderGraph` (`Compile` + `Execute`) lineer eklenen pass'ler arasında `RGBarrierEvent` üretir. **Eksikler:** Topolojik sıralama (DAG sort), kullanılmayan pass'leri budama (dead-pass culling) ve gerçek placed-resource bellek aliasing yoktur. |
| **Render Pass & Pipeline** | `EngineRuntime` içinde 2-3 sabit pass (`FinalCompositePass` + içinde `meshPass` + HUD/Gizmo) vardır. `RenderGraph` pass'i içinde manuel `BeginRendering`/`EndRendering` açılıp kapanır. |
| **Forward / Deferred** | Yalnızca tek pass **Forward** çizim vardır. |
| **PBR & Material** | `MaterialDescriptor` (`Roughness`, `Metallic`, `BaseColorFactor`) mevcuttur; ancak `EngineRuntime.cpp:342-360` içindeki `PSMain` gerçek Cook-Torrance PBR yerine **Blinn-Phong** kullanır ve `Metallic` değerini hiç okumaz (`PackedRoughMetal` üst 16 biti `boneBufIdx` için kullanılmıştır). |
| **Lighting & Shadow** | Sahnedeki `LightComponent` havuzu `PSMain`'e bağlanmamıştır; shader içinde 2 adet sabit yönlü ışık (`L1`, `L2`) ve sabit `V = (0, 0, -1)` görüş vektörü hardcoded'dır. Shadow map, Point/Spot light ve IBL yoktur. |
| **Culling** | Frustum culling, occlusion culling veya distance culling **yoktur**; görünür flag'i açık tüm `MeshRendererComponent`'ler her kare çizilir. |
| **GPU Resource Lifetime** | Kalıcı mesh/texture handle'ları sahne ömrünce tutulur; `CommandList` içinde in-flight frame silme kuyruğu (deferred deletion queue) sınırlıdır (`TransientBuffer` sızıntısı mevcut). |
| **Draw Call & Submission** | `for (renderEntities) -> for (submeshes) -> PushConstants + DrawIndexed`. Instancing, state sorting ve `DrawIndexedIndirect` yoktur. |
| **Post-Processing & AA** | Tone mapping (ACES/Reinhard), bloom, SSAO, TAA/FXAA/MSAA yoktur. |
| **GPU-Driven Hazırlığı** | `Bindless` descriptor heap (`4096` slot) ve `Dispatch` RHI'da mevcuttur; `DrawIndexedIndirect` / `MultiDrawIndirect` RHI komutu henüz `ICommandList`'te yoktur. |

---

## 5. Animation System Analizi

| Alt Başlık | Mevcut Durum |
|---|---|
| **Skeleton & Hierarchy** | `ImportedMeshData` içinde `SkeletonNode` ağacı + `BoneInfo` (`OffsetMatrix`) tutulur. Ayrı bir `SkeletonAsset` yoktur; her mesh kendi hiyerarşisini taşır. |
| **Skeletal Animation & Sampling** | `AnimationEvaluator` lineer pozisyon/ölçek ve `Slerp` rotasyon interpolasyonu yapar (`FindKeyIndex` `upper_bound` ile O(log N)). |
| **Animation Blending** | `CrossFade` desteği (hem önceki klip hem inertial snapshot'tan) ve `EvaluateNode` içi pose blend çalışmaktadır. Çoklu katman (upper/lower body mask) ve Blend Space (1D/2D) yoktur. |
| **State Machine** | Motorda dahili `AnimationStateMachine` yoktur; durum yönetimi C# oyun kodunda `enum` + `Animator.CrossFade` ile yapılır. |
| **Root Motion** | `m_InPlaceRootMotion = true` sabitidir; root delta çıkarılıp fizik kapsülüne (`CharacterController`) aktarılma hattı yoktur. |
| **IK** | İki kemikli IK, Foot IK veya LookAt IK **yoktur**. |
| **Retargeting** | Mixamo -> CC3+ (`CC_Base_*`) global-delta retargeting `AnimationEvaluator` içinde yeni eklendi (`SetRetargetSourceSkeleton`). Genel amaçlı `RigAsset` / kemik haritası editörü yoktur. |
| **CPU/GPU Skinning** | Poz değerlendirmesi CPU'da (`UpdateAnimationSystem`), vertex skinning GPU vertex shader'da (`VSMain`) yapılır. `PerObjectBuffer` **100 kemik (`BoneMatrices[100]`)** ile sabit sınırlıdır; `Vulkan` tarafında bindless array, `DX12` tarafında `b1` slotu ile ayrışır. |
| **Animation LOD & ECS** | Animation LOD / ekran dışı culling yoktur. `Scene::UpdateAnimationSystem` `count >= 16` iken `TaskScheduler::ParallelFor` ile paralel çalışır. |
| **Import Pipeline** | `ModelImporter::ImportAnimationsFromFile` Assimp ile FBX/GLB kliplerini import eder; animasyonlar için `.morbianim` cooked cache formatı henüz yoktur (her açılışta Assimp'ten parse edilir, `s_clipCache` RAM'de tutar). |

---

## 6. Endüstriyel Standartlarla Karşılaştırma

| Konu | UE5 / Unity 6 / Godot 4 / O3DE Yaklaşımı | MorbiEngine İçin Gerçekçi Çıkarım |
|---|---|---|
| **Render Graph** | UE5 RDG / O3DE Atom: Pass culling, DAG sıralama, transient bellek aliasing, otomatik barrier birleştirme. | Mevcut `RenderGraph` korunup üzerine **dead-pass culling**, **batch barrier** ve **transient buffer havuzu** eklenmeli; aşırı soyutlama yerine hafif DAG yeterli. |
| **Shader & Reflection** | UE5/O3DE/Godot 4: `SPIRV-Cross` / `SPIRV-Reflect` + `IDxcContainerReflection` ile gerçek ikili reflection; async PSO/shader derleme. | `std::regex` kaldırılıp **SPIRV-Reflect** (tek C/C++ dosyası, Windows/Linux/Android uyumlu) + DXIL reflection entegre edilmeli; `ShaderCache` arka plan job thread'ine alınmalı. |
| **Material & Lighting** | Godot 4 / Unity URP: Clustered Forward+ (ışık listesi buffer'da), Cook-Torrance PBR, Cascaded Shadow Maps (CSM). | İlk adımda Deferred G-Buffer yerine **Forward+ (Clustered/Tile Light Culling)** çok daha az VRAM/karmaşıklıkla Windows/Linux/Android/DX12'de yüksek performans verir. |
| **GPU-Driven** | UE5 Nanite / O3DE: Meshlet/Instance buffer + `DrawIndexedIndirect`. | Önce **CPU Frustum Culling + Instanced Draw + `DrawIndexedIndirect`** eklenmeli; 500k+ vertex sahnelerde draw call sayısını düşürür. |
| **Animation** | Unity Animator / O3DE EMotionFX: Ayrı `SkeletonAsset`, `AnimationGraph` (State Machine + 1D/2D Blend Space), `SSBO` ile sınırsız kemik paleti. | `BoneMatrices[100]` CBV limiti yerine **StructuredBuffer (`SSBO`) kemik paleti** kullanılmalı; C++ tarafında hafif veri odaklı `AnimationStateMachine` + `1D/2D BlendSpace` + `TwoBoneIK` eklenmeli. |

---

## 7. Yeniden Yapılandırma Önerisi (Sorun Tablosu)

### Sorun 1: Transient Buffer Bellek Sızıntısı ve Eksik Havuzlama
| Alan | Detay |
|---|---|
| **Mevcut durum** | `RenderGraph::Compile()` her frame `m_buffers` için `m_device.CreateBuffer(...)` çağırır, `Reset()` yalnızca vektörü temizler; GPU buffer yok edilmez. |
| **Problem** | Transient buffer kullanan her pass her kare yeni GPU belleği ayırıp sızdırır; uzun süren oturumlarda OOM oluşur. |
| **Kök neden** | `RenderGraph` içinde `m_transientTexturePool` benzeri bir `m_transientBufferPool` ve frame-bazlı in-flight serbest bırakma mekanizması kurulmamıştır. |
| **Önerilen çözüm** | `m_transientBufferPool` ekle, boyut/usage eşleşmesiyle yeniden kullan; kullanılmayanları N frame fence sonrası `DestroyBuffer` ile serbest bırak. |
| **Öncelik** | **Kritik** |
| **Bağımlılıklar** | `Morbi.Render`, `Morbi.RHI` (`GetCompletedTimelineValue`). |
| **Risk** | Yanlış fence takibi GPU use-after-free hatasına yol açabilir; timeline semaphore değeri kontrol edilmeli. |

### Sorun 2: Monolitik `EngineRuntime.cpp` ve Sahte Render Pipeline
| Alan | Detay |
|---|---|
| **Mevcut durum** | Inline HLSL, PSO, sabit 2 ışıklı Blinn-Phong shader, görüş vektörü `V=(0,0,-1)` ve culling'siz draw döngüsü `EngineRuntime.cpp` içinde gömülüdür. |
| **Problem** | Sahne ışıkları (`LightComponent`) ve gerçek PBR çalışmaz; gölge yoktur; `Model` matris hiyerarşisi (`HierarchyMatrices`) okunmaz; kamera uzaklaştıkça ışık/speküler hatalıdır. |
| **Kök neden** | Phase 6 doğrulaması için yazılan prototip render yolu `Morbi.Render` içine modüler bir `SceneRenderer` olarak taşınmamıştır. |
| **Önerilen çözüm** | `Morbi.Render` altında `SceneRenderer` + `Assets/Shaders/*.hlsl` dosyalarına ayır; gerçek `CameraPos`, `LightBuffer` (Directional + Point/Spot), Cook-Torrance PBR ve hiyerarşik `WorldMatrix` kullan. |
| **Öncelik** | **Kritik** |
| **Bağımlılıklar** | `Morbi.Render`, `Morbi.Shader`, `Morbi.Scene`, `Morbi.Runtime`. |
| **Risk** | Editör viewport / Gizmo / GameHUD pass sıralamasının korunması gerekir (`--smoke-test` görsel doğrulaması şart). |

### Sorun 3: Regex Tabanlı Shader Reflection ve Senkron DXC Süreci
| Alan | Detay |
|---|---|
| **Mevcut durum** | `ShaderCompiler::ReflectSourceAndBinary` regex ile binding parse eder, `PushConstantSizeBytes = 64` sabit döner; her derleme `CreateProcessW("dxc.exe")` başlatır. |
| **Problem** | `#ifdef` makroları, 128B push constant (`MeshPushConstants` 128B olduğu için `DX12` root constant uyumsuzluğu riski) ve struct layout'ları yanlış raporlanır; açılışta frame drop olur. |
| **Kök neden** | Gerçek binary reflection kütüphanesi (`SPIRV-Reflect` / `IDxcUtils`) ve asenkron derleme kuyruğu yoktur. |
| **Önerilen çözüm** | `SPIRV-Reflect` (üçüncü parti hafif tek kaynak) ile binary reflection + `TaskScheduler` tabanlı `CompileAsync` ve placeholder fallback shader ekle; `.hlsl` dosya izleyicisi ile hot-reload bağla. |
| **Öncelik** | **Yüksek** |
| **Bağımlılıklar** | `Morbi.Shader`, `Morbi.RHI` (`PipelineLayout`), `Morbi.Core` (`TaskScheduler`). |
| **Risk** | Vulkan ve DX12 register/space eşleşmelerinin bozulmaması için iki backend'de de doğrulanmalı. |

### Sorun 4: GPU Skinning 100 Kemik Limiti ve Vulkan/DX12 Shader Ayrışması
| Alan | Detay |
|---|---|
| **Mevcut durum** | `PerObjectBuffer` `BoneMatrices[100]` CBV dizisi kullanır (`AnimationEvaluator.hpp:32`, `EngineRuntime.cpp:210`); `Vulkan` bindless array, `DX12` `b1` slotu kullanır. |
| **Problem** | 100 üzeri kemikli karakterlerde vertex'ler bozulur; her karakter ayrı CBV descriptor tüketir; `.morbianim` cooked formatı olmadığı için açılışta Assimp yavaşlar. |
| **Kök neden** | Sabit boyutlu `cbuffer` kullanımı ve `DX12`/`Vulkan` root parameter tablosunun birleşik olmaması. |
| **Önerilen çözüm** | Tek bir global dinamik `StructuredBuffer<float4x4>` (offset tabanlı skinning paleti) kullanarak kemik limitini kaldır; animasyon kliplerini `.morbianim` olarak cook et. |
| **Öncelik** | **Yüksek** |
| **Bağımlılıklar** | `Morbi.Asset`, `Morbi.RHI`, `Morbi.Render`, `Morbi.Shader`. |
| **Risk** | Vertex shader ve `AnimationEvaluator::FillGPUBuffer` arayüzünün eş zamanlı güncellenmesi gerekir. |

### Sorun 5: Culling, Instancing ve `DrawIndexedIndirect` Eksikliği
| Alan | Detay |
|---|---|
| **Mevcut durum** | Her submesh için `PushConstants` + `DrawIndexed` çağrılır; frustum culling yoktur; `ICommandList`'te `DrawIndexedIndirect` yoktur. |
| **Problem** | Yüksek poligonlu/çok submesh'li haritalarda (`procedural_city_6.glb` = 79 submesh) görünmeyen objeler de GPU'ya gönderilir, CPU draw call maliyeti artar. |
| **Kök neden** | `SceneRenderer` visibility culling katmanı ve GPU instance buffer mimarisi henüz yazılmamıştır. |
| **Önerilen çözüm** | İlk aşamada `TaskScheduler` ile paralel CPU Frustum Culling + materyal/mesh sıralama; ikinci aşamada `ICommandList::DrawIndexedIndirect` + GPU instance buffer ekle. |
| **Öncelik** | **Yüksek** |
| **Bağımlılıklar** | `Morbi.RHI`, `Morbi.Render`, `Morbi.Scene`. |
| **Risk** | AABB dünya dönüşümü hatalı yapılırsa kenardaki büyük mesh'ler (örn. NYC blokları) erken elenebilir. |

### Sorun 6: Animasyon State Machine, Blend Space, IK ve Root Motion Eksikliği
| Alan | Detay |
|---|---|
| **Mevcut durum** | Yalnızca tek klip `CrossFade` vardır; State Machine, 1D/2D Blend Space, IK ve Root Motion entegrasyonu yoktur. |
| **Problem** | Yürüme/koşma hız harmanlaması (blend), eğimli zeminde ayak basma (Foot IK) ve karmaşık geçiş kuralları C# tarafında manuel yönetilmek zorunda kalır. |
| **Kök neden** | `Morbi.Asset` / `Morbi.Scene` içinde veri odaklı bir animasyon grafiği katmanı henüz geliştirilmemiştir. |
| **Önerilen çözüm** | `AnimationGraph` (State Machine + 1D/2D BlendSpace) + `TwoBoneIK` (ayak/el) + `RootMotion` delta çıkarımı ekle; C# API'ye `SetFloat`/`SetBool`/`Trigger` parametreleri sun. |
| **Öncelik** | **Orta** |
| **Bağımlılıklar** | `Morbi.Asset`, `Morbi.Scene`, `Morbi.Physics` (Foot IK raycast), `Morbi.Script`. |
| **Risk** | Multiplayer reconciliation ile çakışmaması için `RootMotion` sadece sunucu otoriter veya deterministik input ile kullanılmalıdır. |

### Sorun 7: `RenderGraph` Dead-Pass Culling ve Fiziksel Bellek Aliasing
| Alan | Detay |
|---|---|
| **Mevcut durum** | `RenderGraph::Compile()` tüm eklenen pass'leri çalıştırır ve yalnızca tahmini `AliasedTransientBytesPeak` hesaplar (`AliasingPoolOffset = ~0ULL`). |
| **Problem** | Kullanılmayan pass'ler GPU zamanı harcar; Shadow/PostProcess eklendiğinde transient RT VRAM tüketimi gereksiz büyür. |
| **Kök neden** | Backbuffer üreticiden geriye doğru bağımlılık grafiği taraması ve RHI placed-resource (`VMA` / `D3D12PlacedResource`) desteği henüz eklenmemiştir. |
| **Önerilen çözüm** | `RenderGraph::Compile()` içinde backbuffer'dan geriye reachability ile dead-pass culling ekle; `IDevice` içine transient heap aliasing desteği ekle. |
| **Öncelik** | **Orta** |
| **Bağımlılıklar** | `Morbi.Render`, `Morbi.RHI.Vulkan` (VMA), `Morbi.RHI.DX12`. |
| **Risk** | Side-effect pass'lerin (örn. compute culling/readback) budanmaması için `PassFlags::NeverCull` bayrağı şarttır. |

---

## 8. Hedef Mimari ve Uygulama Yol Haritası

### A. Hedef Shader Mimarisi
* **Bileşenler:** `ShaderLibrary` (VFS `assets://Shaders/*.hlsl` + `#include` bağımlılık takibi) -> `AsyncShaderCompiler` (`TaskScheduler` worker + placeholder PSO fallback) -> `SPIRV-Reflect` (tek gerçek binding/push-constant/CS group-size kaynağı) -> `PipelineStateCache` (hash: shader hashes + blend/depth/raster state + render target formatları).
* **Varyant Yönetimi:** Bitmask tabanlı `ShaderPermutationKey` (örn. `HAS_SKINNING`, `HAS_NORMAL_MAP`, `ALPHA_TEST`, `RECEIVE_SHADOWS`) — kombinasyon patlamasını önlemek için yalnızca kullanılan permütasyonlar derlenir.
* **Platform & API:** `dxc` (Windows/Linux/Android uyumlu) tek HLSL kaynaktan `SPIR-V` + `DXIL` üretir; `MORBI_VULKAN` / `MORBI_DX12` makroları ortak `RootLayout` (`b0` Push/RootConstants, `b1` Frame/Light, `t0 space0/1` Bindless) ile birleştirilir.

### B. Hedef Rendering Mimarisi
* **Modern RHI:** `IDevice` çoklu `ICommandList` kaydı (worker thread başı 1 allocator/list), `DrawIndexedIndirect`, frame-ring deferred deletion queue ve `Vulkan`/`DX12` tam parite.
* **Modern Render Graph:** DAG topolojik sıralama + dead-pass culling + batch `Transition` barrier + VMA/D3D12 placed aliasing heap.
* **Forward+ (Clustered) PBR Pipeline:**
  1. **ShadowPass:** Directional CSM (2-4 cascade, PCF) -> `ShadowAtlas`.
  2. **DepthPrePass:** Z-prepass (overdraw azaltma + Hi-Z / cluster hazırlığı).
  3. **LightCullingComputePass:** Ekran uzayı 3D cluster'lara `PointLight`/`SpotLight` ataması.
  4. **OpaquePBRPass:** Cook-Torrance GGX PBR + `Normal` + `MetallicRoughness` + `Shadow` + basit IBL/Hemisphere ambient; CPU/GPU Frustum Culling + materyal/mesh sıralı `DrawIndexedIndirect` / Instancing.
  5. **Transparent & PostProcessPass:** ACES Tone Mapping + FXAA/TAA + Bloom + `GameUI` + Gizmo.

### C. Hedef Animation Mimarisi
* **Asset & Skeleton:** Ayrı `SkeletonAsset` (cook edilmiş `.morbiskel`) + `.morbianim` binary klip formatı (`CookedAssetSerializer`).
* **Değerlendirme & Grafik:** `AnimationGraph` -> `StateMachine` (koşullu geçişler, crossfade) + `BlendSpace1D/2D` (Idle/Walk/Run hız harmanlaması) + `TwoBoneIK` (ayak/zemin uyumu) + opsiyonel `RootMotion` delta çıktısı.
* **GPU Skinning & LOD:** Sabit 100 kemik sınırı kaldırılarak tek global `StructuredBuffer<float4x4>` kemik paletine geçilir; uzak/ekran dışı karakterler için `AnimationLOD` (güncelleme frekansı düşürme / skinning atlama).

### D. Uygulama Sırası ve Bağımlılıklar

```text
[Faz A: RHI & Shader Temeli]
  1. RenderGraph transient buffer pool + deferred deletion (Sızıntıyı kapat)
  2. Shader reflection (SPIRV-Reflect) + dosya tabanlı HLSL + PipelineCache + Async Compile
        |
        v
[Faz B: Modern SceneRenderer & PBR]
  3. HierarchyMatrices render entegrasyonu + Frustum Culling + SceneRenderer'ı EngineRuntime'dan ayırma
  4. Unified Bindless/SSBO layout (DX12 & Vulkan tam parite) + DrawIndexedIndirect
  5. Forward+ Clustered Lighting + Cook-Torrance PBR + Cascaded Shadow Maps + ACES ToneMapping/AA
        |
        v
[Faz C: Profesyonel Animation Sistemi]
  6. SSBO Sınırsız GPU Skinning + .morbianim/.morbiskel cooked formatı
  7. Animation State Machine + 1D/2D Blend Space (C# parametre köprüsü)
  8. TwoBoneIK (Foot IK) + Root Motion + Animation LOD
```

---

## 9. Gerçekçi Değerlendirme Özeti
* **Korunacak Çalışan Yapılar:** `Morbi.RHI` bindless 4096-slot mimarisi, `dxc` dual-target (DXIL+SPIR-V) akışı, `RenderGraph` barrier sentezi mantığı, `Scene` SoA bellek yerleşimi ve `TaskScheduler::ParallelFor` animasyon paralelleştirmesi **sıfırdan yazılmadan** üzerine inşa edilmelidir.
* **İlk Öncelik (Görsellikten Önce Doğruluk & Performans):** `TransientBuffer` sızıntısının kapatılması, `HierarchyMatrices`'in render'a bağlanması, `EngineRuntime.cpp` içindeki prototip çizim kodunun `Morbi.Render`'a çıkarılması ve gerçek `SPIRV-Reflect` entegrasyonu en yüksek getiriyi sağlayacaktır.
