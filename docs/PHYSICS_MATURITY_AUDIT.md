# MorbiEngine — Physics Maturity Audit (Teknik Denetim Raporu)

**Tarih:** 2026-11-16  
**Kapsam:** `Morbi.Physics`, `Morbi.Scene`, `Morbi.Runtime`, `Morbi.Script`, `Managed/Morbi.Engine`, `Editor/Frontend`, `cmake/MorbiDependencies.cmake` (`legacy/` hariç)  
**Durum:** Salt İnceleme / Denetim (Kod değişikliği yapılmadı)

---

## 1. Yönetici Özeti ve Gerçek Durum

MorbiEngine kod tabanında sınıf, struct ve log isimlendirmeleri Jolt Physics 5.2.0 entegrasyonu varmış gibi adlandırılmış olsa da (`JoltBodyId`, `JoltBodyRecord`, `"Initialized Jolt Physics World"`), **gerçekte Jolt Physics 5.2.0 derlenmemekte, bağlanmamakta ve çalıştırılmamaktadır**.

* **Derleme Durumu (`cmake/MorbiDependencies.cmake:143`):** `MORBI_FETCH_JOLT_FROM_GITHUB` seçeneği varsayılan olarak **`OFF`** durumundadır; `build/_deps` altında Jolt kaynakları veya ikilileri yoktur; `MORBI_HAS_JOLT_LIB` makrosu tanımlı değildir (`Engine/Source/Physics/CMakeLists.txt:19-22`).
* **Çalışma Zamanı Durumu (`Engine/Source/Physics/src/PhysicsWorld.cpp:1-428`):** Dosyada tek bir `#include <Jolt/...>` veya `JPH::PhysicsSystem` / `JPH::BodyInterface` çağrısı yoktur. Bunun yerine yalnızca **Y-ekseni AABB çakışma itmesi** yapan yaklaşık 428 satırlık yer tutucu (stub) bir çözücü çalışmaktadır.
* **Test Doğrulama Durumu:** `--smoke-test` (`Engine/Source/Runtime/src/EngineRuntime.cpp:1816-1818`) yalnızca `StepSimulation(0.016f)` çağırıp `ActiveBodyCount` sayısını loglar; çarpışma, istifleme, sekme, sürtünme, kütle veya yaşam döngüsü testi mevcut değildir.

> **Denetim Kuralı Gereği İşaretleme:** Mevcut durumda katı cisim simülasyonu, şekil ayrımı, rotasyonlu çarpışma, sürtünme, kütle tabanlı çarpışma, CCD, thread-safety ve yaşam döngüsü temizliği dahil **hiçbir üretim seviyesi fizik özelliği tamamlanmış sayılmaz**.

---

## 2. Mevcut Mimari Analizi

| Bileşen | Dosya Referansı | Mevcut Durum ve Teknik Gözlem |
| :--- | :--- | :--- |
| **`PhysicsWorld`** | `PhysicsWorld.hpp:55-95`, `PhysicsWorld.cpp:95-428` | `Impl` içinde `std::vector<JoltBodyRecord> Bodies`, `Accumulator`, `Gravity`, `Collisions` tutar. Jolt `PhysicsSystem`, `TempAllocatorImpl`, `JobSystemThreadPool` kullanmaz. |
| **Jolt Entegrasyonu** | `cmake/MorbiDependencies.cmake:143-156`, `Engine/Source/Physics/CMakeLists.txt:19-22` | Jolt FetchContent kapalıdır (`OFF`). Açılıp linklense dahi `PhysicsWorld.cpp` Jolt başlıklarını ve sınıflarını hiç kullanmamaktadır. |
| **`RigidBodyComponent`** | `Components.hpp:119-133`, `NativeBridge.cs:52-65` | 44 byte POD struct (`LinearVelocity`, `Mass`, `HalfExtents`, `Restitution`, `Friction`, `JoltBodyId`, `MotionType`, `Shape`, `IsAwake`, `UseGravity`). Eksikler: `AngularVelocity`, `LinearDamping`, `AngularDamping`, `GravityFactor`, `LockedAxes` (DOF kilidi), `IsSensor`, `CollisionLayer`, `MotionQuality` (CCD), `DirtyFlags`, `Rotation`/şekil boyut ayrımları. |
| **Collider / Shape** | `Components.hpp:111-116`, `PhysicsWorld.cpp:188, 265-294, 404` | `ColliderShapeType` (`Box`, `Sphere`, `Capsule`) tanımlıdır ancak çözücüde ve `Raycast`'te **hiç okunmaz**. Her şekil rotasyonsuz eksen-hizalı kutu (`HalfExtents`) gibi davranır. |
| **Scene / ECS Bağlantısı** | `EngineRuntime.cpp:4418-4429`, `PhysicsWorld.cpp:160-204, 300-316`, `Scene.cpp:394-435` | `StepSimulation` her kare başında mevcut gövdelerin `Position/Rotation/LinearVelocity` değerlerini Scene'den okuyup, adım sonunda Dynamic olanları Scene'e geri yazar. `HierarchyComponent` world-space hesabı kullanılmaz (`tr->Position` lokaldir). `Scene::SaveToJsonFile` `m_rigidBodies` bileşenini **hiç kaydetmez**. |
| **Entity Yaşam Döngüsü** | `Scene.cpp:47-87`, `EngineRuntime.cpp:2666-2676, 2978, 3382` | `SetEntityDestroyedCallback` yalnızca `AnimatorComponent::GpuBuffer` temizler; `PhysicsWorld` bundan haberdar edilmez. `ResetSimulation` gövde vektörünü temizlemez. |
| **`CollisionPairEvent`** | `PhysicsWorld.hpp:36-43`, `PhysicsWorld.cpp:215, 286-292` | `std::vector<CollisionPairEvent>` her substep başında `clear()` edilir (`Collisions.clear()`). Yani **1'den fazla substep** çalışırsa önceki substep çarpışmaları kaybolur; yalnızca son substep'in Y-ekseni temasları kalır ve C# tarafına da expose edilmez. |
| **Fixed Timestep & Accumulator** | `PhysicsWorld.cpp:109, 206-215` | `FixedTimestep` minimum `1/240s` ile alt sınırlanır. `Accumulator += min(deltaTime, 0.25f)`, `kMaxSubSteps = 4`. Kalan `Accumulator` kesri için render **enterpolasyonu (`alpha`)** yoktur; yüksek FPS veya kare atlamada görsel titreme (stutter) oluşur. |
| **Raycast** | `PhysicsWorld.cpp:31-92, 383-417` | `scene.RigidBodies()` üzerinde lineer O(N) tarama ve rotasyonsuz `IntersectRayAABB` kullanır. Layer maskesi, sensor filtresi, Sphere/Capsule/OBB kesişimi, zero-direction koruması ve Jolt BroadPhase hızlandırması yoktur. |
| **Bellek ve Kaynak Yönetimi** | `PhysicsWorld.cpp:100-101, 170-192` | `Bodies` yalnızca büyüyen `std::vector`'dür; silme veya boş slot geri dönüşümü yoktur (`swap-and-pop` veya free-list yok). Her yeni entity eklenip silindikçe vektör sonsuza kadar büyür. |

---

## 3. Kritik Araştırma Sorularına Kanıtlı Yanıtlar

1. **Dynamic body çarpışmalarında penetrasyon oluşuyor mu? — EVET (Kritik)**
   * `PhysicsWorld.cpp:274`: Yalnızca `if (oy <= ox && oy <= oz)` koşulunda pozisyon düzeltmesi yapılır. X veya Z ekseninde temas eden iki kutuda (`else` dalı olmadığı için) **sıfır çarpışma tepkisi** üretilir; cisimler birbirinin içinden tamamen geçer.
   * `oy <= ox && oy <= oz` sağlansa bile (`PhysicsWorld.cpp:277`) tüm düzeltme `bodyA.Position.y += oy * signY` ile yalnızca `bodyA` üzerine uygulanır; `bodyB` Dynamic ise `bodyB` hiç hareket ettirilmez ve iteratif çözücü (NGS/Sequential Impulse) yoktur.
2. **Üst üste dizilmiş cisimler titriyor mu? — EVET (Kritik)**
   * `PhysicsWorld.cpp:248-296`: Tek geçişli, warm-start'sız, sleep'siz (`IsAwake` kullanılmıyor) çözücü çalışır. 30 kutuluk yığında alt kutunun düzeltmesi üst kutunun aynı substep'te tekrar çözülmesini tetiklemez; gövdeler birbirinin içine gömülür, sırayla itilir ve kalıcı titreşim/çökme (jitter & sinking) oluşur.
3. **Restitution ve friction değerleri doğru uygulanıyor mu? — HAYIR (Kritik)**
   * **Friction:** `JoltBodyRecord::Friction` (`PhysicsWorld.cpp:24, 187`) okunduğu **tek bir satır bile yoktur**. Sürtünme tamamen uygulanmamaktadır.
   * **Restitution:** `PhysicsWorld.cpp:280`: Yalnızca `bodyA.Restitution` kullanılır (`bodyB.Restitution` yok sayılır), iki dynamic cismin göreli hızı (`v_rel`) ve kütle oranı yerine sadece `bodyA.LinearVelocity.y` ters çevrilir; `0.15f` sabit eşiği (`PhysicsWorld.cpp:281`) nedeniyle düşük hızdaki `Restitution = 1.0` sekmeleri dahi sıfırlanır.
4. **Mass ve inertia tensor yönetimi doğru mu? — HAYIR (Kritik)**
   * `InvMass` (`PhysicsWorld.cpp:185`) hesaplanır ama çarpışma çözümünde **hiç kullanılmaz**. 1 kg ile 1000 kg kutu arasındaki çarpışmada kütle farkı hiçbir momentum/itki farkı yaratmaz.
   * **Inertia tensor ve açısal dinamik hiç yoktur** (`AngularVelocity`, tork, atalet tensörü mevcut değildir; cisimler çarpışınca devrilmez/dönmez).
5. **Kinematic ve dynamic cisimler doğru etkileşiyor mu? — HAYIR (Kritik)**
   * `PhysicsWorld.cpp:174`: `isDyn = (MotionType == Dynamic)`. Kinematic cisimler Static gibi `kNonMoving` katmanına atılır. X/Z çarpışma çözümü olmadığı için yatay hareket eden Kinematic bir platform/kapı Dynamic cisimleri itemez; üstten düşen Dynamic cisim ise Kinematic hızını paylaşmaz.
6. **Hızlı hareket eden cisimlerde tunneling oluşuyor mu? — EVET (Kritik)**
   * `PhysicsWorld.cpp:234`: Ayrık (discrete) Euler adım atılır (`pos += vel * dt`), CCD (`LinearCast`) yoktur. Tek substep'te `vel * dt > 2 * halfExtent` olan her cisim ince engelin içinden geçer. Ayrıca X/Z yönünde zaten hiçbir çarpışma çözümü yoktur.
7. **Entity silindiğinde Jolt body kaynakları temizleniyor mu? — HAYIR (Kritik)**
   * `Scene::DestroyEntity` (`Scene.cpp:64-78`) çağrıldığında `EngineRuntime.cpp:2667-2676`'daki callback yalnızca GPU skinning buffer'ını siler. `PhysicsWorld::Impl::Bodies` içindeki `JoltBodyRecord` sonsuza kadar `Active = true` kalır (**zombie collider leak**).
8. **Scene yeniden yüklendiğinde fizik durumu doğru kuruluyor mu? — HAYIR (Kritik)**
   * `EngineRuntime.cpp:2978`: Sahne yüklemede/değiştirmede ve PIE başlangıç/bitişinde (`EngineRuntime.cpp:3359, 3383`) `Initialize()` yerine `ResetSimulation(activeScene)` çağrılır. `ResetSimulation` (`PhysicsWorld.cpp:326-350`) **`Bodies` vektörünü temizlemez**! Eski sahnenin tüm fizik gövdeleri yeni sahnede hayalet engel olarak kalır. Üstelik `Scene::SaveToJsonFile` (`Scene.cpp:394-435`) `RigidBodyComponent`'i JSON'a **hiç yazmaz**.
9. **Fizik güncellemeleri thread-safe mi? — HAYIR (Yüksek)**
   * `PhysicsWorld.cpp:238-245` yalnızca Euler hız/konum döngüsünü `TaskScheduler::ParallelFor` ile paralel çalıştırır; ancak `m_impl->Bodies`, `m_impl->Collisions`, `StepSimulation`, `Raycast`, `AddLinearImpulse`, `SetLinearVelocity`, `ResetSimulation` ve `scene.Transforms()` erişimlerinde **hiçbir kilit (`mutex`/`shared_mutex`) veya çift tamponlu komut kuyruğu yoktur**. Eşzamanlı script/render sorgularında data race oluşur.

---

## 4. Önem Derecesine Göre Sorun Kataloğu

### CRITICAL-01: Jolt Physics 5.2.0 Motorunun Gerçekte Bağlı ve Aktif Olmaması
* **Sorunun Tanımı:** `Morbi.Physics` Jolt Physics 5.2.0 tabanlıymış gibi arayüz ve log sunsa da derlemede Jolt kapalıdır ve çalışma zamanında hiç çağrılmaz.
* **Mevcut İmplementasyon:** `cmake/MorbiDependencies.cmake:143` (`MORBI_FETCH_JOLT_FROM_GITHUB OFF`), `Engine/Source/Physics/CMakeLists.txt:19-22`, `Engine/Source/Physics/src/PhysicsWorld.cpp:1-428`.
* **Teknik Sebep:** Jolt kaynak kodu veya kütüphanesi yerel/FetchContent olarak etkinleştirilmemiş ve `PhysicsWorld.cpp` yerine geçici bir AABB taslağı bırakılmıştır.
* **Önerilen Çözüm:**
  1. Jolt Physics v5.2.0'ı varsayılan olarak derlemeye dahil et (`ThirdParty/` veya `FetchContent` önbelleğiyle `MORBI_FETCH_JOLT_FROM_GITHUB ON` ya da vendored kaynak).
  2. `PhysicsWorld::Impl` içinde Jolt'un resmi çekirdek nesnelerini kur: `JPH::RegisterDefaultAllocator()`, `JPH::Factory`, `JPH::RegisterTypes()`, `JPH::TempAllocatorImpl`, `JPH::JobSystemThreadPool` (veya `Morbi::Core::TaskScheduler`'a köprülenmiş özel `JPH::JobSystem`), `BroadPhaseLayerInterface`, `ObjectVsBroadPhaseLayerFilter`, `ObjectLayerPairFilter` ve `JPH::PhysicsSystem`.
* **Etkilenen Dosyalar:** `cmake/MorbiDependencies.cmake`, `Engine/Source/Physics/CMakeLists.txt`, `Engine/Source/Physics/src/PhysicsWorld.cpp`.
* **Test Yöntemi:** CMake configure logunda `Jolt` hedefinin bağlı olduğunu doğrula; `MORBI_HAS_JOLT_LIB == 1` altında `JPH::PhysicsSystem::GetNumBodies()` döndüren başlatma testini çalıştır.

### CRITICAL-02: X/Z Eksen Çarpışmalarının Hiç Çözülmemesi, Yatay Penetrasyon ve Kütle/Momentum Yoksayılması
* **Sorunun Tanımı:** Dynamic-Dynamic ve Dynamic-Static çarpışmalar yalnızca Y ekseni en küçük taşma ekseniyse tek yönlü pozisyon itmesiyle çözülmekte, yatay çarpmalarda cisimler iç içe geçmekte ve kütle (`InvMass`) hiç kullanılmamaktadır.
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:256-295`.
* **Teknik Sebep:** `if (oy <= ox && oy <= oz)` dışında X ve Z eksenleri için hiçbir çözücü yok; impüls denklemi ($\mathbf{j} = \frac{-(1+e)\mathbf{v}_{rel}\cdot\mathbf{n}}{m_A^{-1} + m_B^{-1}}$) yerine sabit Y ters çevirme kullanılıyor; Brute-Force $O(N^2)$ döngü var.
* **Önerilen Çözüm:** Özel çarpışma çözücü yazmak yerine doğrudan `JPH::PhysicsSystem::Update` + `JPH::BodyInterface` kullanarak Jolt'un Sequential Impulse + Warm Starting + BroadPhase BVH/Quadtree çözücüsünü devreye almak; `JPH::MassProperties` üzerinden kütle ve atalet tensörünü ayarlamak (`OverrideMassProperties::CalculateInertia`).
* **Etkilenen Dosyalar:** `Engine/Source/Physics/src/PhysicsWorld.cpp`.
* **Test Yöntemi:** Yatay çarpışma ve farklı kütleli (1 kg vs 50 kg) çarpışma testlerinde momentum korunumunu ($p_{\text{once}} = p_{\text{sonra}}$) ve sıfır yatay penetrasyonu doğrula.

### CRITICAL-03: Açısal Dinamik, Atalet Tensörü, Rotasyonlu Şekil (OBB) ve Sürtünme/Sekme Eksikliği
* **Sorunun Tanımı:** Cisimler dönmez (inertia tensor yok), eğik yüzeylerde `Rotation` çarpışmaya katılmaz, `Friction` hiç okunmaz, `Restitution` tek taraflıdır ve `0.15f` eşiğiyle kesilir, `Box/Sphere/Capsule` şekilleri ayırt edilmez.
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:22-26, 265-285, 404`; `Components.hpp:111-128`.
* **Teknik Sebep:** `JoltBodyRecord` ve `IntersectRayAABB` sadece `Position` ve `HalfExtents` kullanır; quaternion rotasyonu, atalet matrisi ve Jolt shape hiyerarşisi yoktur.
* **Önerilen Çözüm:** `ColliderShapeType` değerlerini Jolt'un `JPH::BoxShapeSettings`, `JPH::SphereShapeSettings`, `JPH::CapsuleShapeSettings` (kapsül yarıçap/yarı-yükseklik eşlemesi ile) sınıflarına bağlamak; `BodyCreationSettings` içine `mRestitution`, `mFriction`, `mLinearDamping`, `mAngularDamping`, `mGravityFactor` geçmek; `PhysicsSystem::SetContactListener` üzerinden Jolt temas materyal birleştirme kurallarını işletmek.
* **Etkilenen Dosyalar:** `Engine/Source/Physics/src/PhysicsWorld.cpp`, `Engine/Source/Scene/include/Morbi/Scene/Components.hpp`.
* **Test Yöntemi:** 30° eğik zemin üzerinde farklı sürtünmeli kutuların kayma/devrilme testi ve $e \in \{0.0, 0.5, 0.9\}$ küre sekme yükseklik oranı doğrulaması ($h_1 / h_0 \approx e^2$).

### CRITICAL-04: Entity Silme ve Sahne Yeniden Yükleme / PIE Geçişlerinde Zombie Body Sızıntısı
* **Sorunun Tanımı:** Silinen entity'lerin fizik gövdeleri temizlenmez; yeni sahne yüklendiğinde veya PIE sonlandırıldığında eski sahnenin tüm fizik gövdeleri `Bodies` içinde hayalet olarak yaşamaya devam eder.
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:170-192, 326-350`; `Scene.cpp:64-78`; `EngineRuntime.cpp:2667-2676, 2978, 3382`.
* **Teknik Sebep:** `PhysicsWorld` içinde entity yok edilme kancası (`RemoveBody`) yok; `ResetSimulation` `Bodies` dizisini temizlemiyor; `Scene::RestoreSnapshot` eski `JoltBodyId` değerlerini geri yüklerken fizik dünyasını yeniden kurmuyor.
* **Önerilen Çözüm:**
  1. `PhysicsWorld`'e `DestroyBody(Scene::EntityId)` / `ClearAllBodies(Scene::Scene&)` metodları eklemek ve `JPH::BodyInterface::RemoveBody` + `DestroyBody` çağırmak.
  2. `Scene::DestroyEntity` ve `RigidBodies().Remove()` noktalarını (ve `EngineRuntime.cpp`'deki `SetEntityDestroyedCallback` zincirini) `physicsWorld.OnRigidBodyRemoved(eid)` ile bağlamak; `Scene` sahiplenme tablosunda (`EntityId` generation kontrolüyle) ölü/eksik gövdeleri her adımda senkronize etmek.
  3. Sahne yükleme (`initializeLoadedScene`) ve PIE çıkışında (`RestoreSnapshot`) `physicsWorld.RebuildFromScene(activeScene)` (tüm eski Jolt body'leri yok edip `JoltBodyId = 0xFFFFFFFFu` ile baştan yaratma) çalıştırmak.
* **Etkilenen Dosyalar:** `Engine/Source/Physics/include/Morbi/Physics/PhysicsWorld.hpp`, `Engine/Source/Physics/src/PhysicsWorld.cpp`, `Engine/Source/Runtime/src/EngineRuntime.cpp`, `Engine/Source/Scene/src/Scene.cpp`.
* **Test Yöntemi:** 100 entity oluştur-sil-yeniden oluştur ve Sahne A -> Sahne B geçişi sonrası `PhysicsSystem::GetNumBodies()` sayısının yalnızca canlı `RigidBodyComponent` sayısına eşit olduğunu doğrula.

### CRITICAL-05: `Scene::SaveToJsonFile` İçinde `RigidBodyComponent` Serileştirmesinin Eksik Olması
* **Sorunun Tanımı:** Sahne dosyadan yüklenirken `rigid_body` okunabiliyor ancak kaydedilirken diske hiç yazılmıyor.
* **Mevcut İmplementasyon:** `Scene.cpp:394-435` (`SaveToJsonFile` `m_scripts`'ten sonra `m_cameras`'a geçer; `m_rigidBodies` ve `m_audioSources` atlanır) vs `Scene.cpp:600-627` (`LoadFromJsonFile` okur).
* **Teknik Sebep:** JSON kaydetme fonksiyonunda ilgili serileştirme bloğu unutulmuş.
* **Önerilen Çözüm:** `Scene::SaveToJsonFile` içine `m_rigidBodies.TryGet(eid)` bloğu ekleyip `mass`, `restitution`, `friction`, `use_gravity`, `half_extents`, `motion_type`, `shape` (ve eklenecek damping/CCD/layer alanları) anahtarlarını `LoadFromJsonFile` şemasıyla birebir uyumlu yazmak.
* **Etkilenen Dosyalar:** `Engine/Source/Scene/src/Scene.cpp`.
* **Test Yöntemi:** Bir sahneye `RigidBodyComponent` ekleyip `SaveToJsonFile` -> `Clear()` -> `LoadFromJsonFile` sonrası tüm fizik alanlarının birebir korunduğunu doğrula.

### HIGH-01: Kinematic - Dynamic Etkileşiminin ve Sürekli Çarpışma Tespitinin (CCD) Eksikliği
* **Sorunun Tanımı:** Kinematic gövdeler Static ile aynı katmana atılmakta, hız/sweep ile Dynamic gövdeleri itememekte ve yüksek hızlı cisimlerde CCD olmadığı için tunneling yaşanmaktadır.
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:174-200, 234`.
* **Teknik Sebep:** `isDyn` yalnızca `Dynamic`'i işaretler; Kinematic gövdeler `MoveKinematic` yerine doğrudan koordinat atamasıyla ışınlanır; `JPH::EMotionQuality::LinearCast` yoktur.
* **Önerilen Çözüm:** `RigidBodyMotionType` -> `JPH::EMotionType` (`Static`, `Kinematic`, `Dynamic`) 1:1 eşlemek; Kinematic cisimler için `ObjectLayers::kMoving` kullanıp Scene transform'undaki değişimi `BodyInterface::MoveKinematic(bodyId, targetPos, targetRot, fixedDt)` ile aktarmak; hızlı cisimler için `MotionQuality` (`Discrete` / `LinearCast`) bayrağı eklemek.
* **Etkilenen Dosyalar:** `Engine/Source/Scene/include/Morbi/Scene/Components.hpp`, `Engine/Source/Physics/src/PhysicsWorld.cpp`, `Managed/Morbi.Engine/NativeBridge.cs`, `Components.cs`.
* **Test Yöntemi:** Hareketli Kinematic küpün önündeki Dynamic küpü sürükleyip ittiğini ve `150 m/s` hızdaki merminin `0.05m` kalınlıklı duvardan geçmediğini doğrula.

### HIGH-02: Thread-Safety Eksikliği ve C++/C# Çift Yönlü Senkronizasyon Açıkları
* **Sorunun Tanımı:** `PhysicsWorld` API'lerinde mutex/read-write kilidi yoktur; ayrıca çalışma zamanında C# `Span<RigidBodyComponent>` üzerinden değiştirilen `Mass`, `Restitution`, `Friction`, `HalfExtents`, `MotionType`, `Shape`, `UseGravity` değerleri mevcut `JoltBodyRecord`'a güncellenmez.
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:194-200, 352-417`; `Components.cs:355-465`.
* **Teknik Sebep:** Gövde oluşturma anından sonra `PhysicsWorld` yalnız `Position`, `Rotation`, `LinearVelocity` alanlarını okuyor. Ayrıca `AddLinearImpulse`, `Raycast` ve `StepSimulation` üzerinde eşzamanlı erişim koruması yok.
* **Önerilen Çözüm:**
  1. `JPH::PhysicsSystem`'in kendi `BodyLockInterface` (`LockRead`/`LockWrite`) mekanizmasını `Raycast` ve dış sorgularda kullanmak; `PhysicsWorld::Impl` komut/olay tamponlarını `std::mutex` / `std::shared_mutex` ile korumak.
  2. `RigidBodyComponent` içine `DirtyFlags` veya gölge karşılaştırma (`ShadowState`) ekleyip runtime'da C# veya Editör Inspector'dan değişen `Mass`, `Friction`, `Restitution`, `UseGravity`, `MotionType`, `Shape` değerlerini Jolt `BodyInterface`'e yansıtmak.
  3. Parent'lı (`HierarchyComponent`) entity'lerde yerel `tr->Position` yerine world transform senkronizasyon kuralını netleştirmek (veya Dynamic body'ler için lokal-dünya dönüşümü uygulamak).
* **Etkilenen Dosyalar:** `Engine/Source/Physics/src/PhysicsWorld.cpp`, `Engine/Source/Scene/include/Morbi/Scene/Components.hpp`, `Managed/Morbi.Engine/NativeBridge.cs`.
* **Test Yöntemi:** Çalışma zamanında C#'tan kütle/yerçekimi değiştirme testi + eşzamanlı `Raycast` ve `StepSimulation` stres testi (TSan / Race kontrolü).

### MEDIUM-01: Raycast Kısıtları, Çoklu Substep Olay Kaybı ve `Transform.Scale` Uyumsuzluğu
* **Sorunun Tanımı:**
  1. `Collisions.clear()` (`PhysicsWorld.cpp:215`) her substep'te çalışır; birden fazla substep yürütüldüğünde ilk substep çarpışma olayları silinir.
  2. `Raycast` (`PhysicsWorld.cpp:390`) `{0,0,0}` yönünde hatalı AABB vuruşu üretebilir, katman/mesafe doğrulaması eksiktir ve sadece AABB görür.
  3. Sabit adım artığı için görsel render enterpolasyonu yoktur ve `Accumulator` 4 substep sınırına çarptığında simülasyon zamanı kaybolur (slow-motion drift); `TransformComponent::Scale` ile `RigidBodyComponent::HalfExtents` ilişkisi tanımsızdır; Editör Inspector'da `rb->Friction` "Linear Damping" olarak gösterilmektedir (`WebView2Compositor.cpp:1511`, `InspectorPanel.tsx:395`).
* **Mevcut İmplementasyon:** `PhysicsWorld.cpp:207-215, 383-417`; `WebView2Compositor.cpp:1507-1514`; `InspectorPanel.tsx:388-405`.
* **Teknik Sebep:** `Collisions.clear()` döngü içinde, `ContactListener` tamponu yok; `Raycast` Jolt `NarrowPhaseQuery` yerine manuel döngü kullanıyor; Inspector UI eksik alan eşlemesine sahip.
* **Önerilen Çözüm:**
  1. `m_impl->Collisions.clear()` çağrısını substep döngüsünün **dışına**, `StepSimulation` başına taşımak (Jolt `ContactListener` üzerinden biriken olayları kare bazında toplamak).
  2. `Raycast` için `direction` uzunluk ve `maxDistance > 0` doğrulaması eklemek, Jolt `GetNarrowPhaseQuery().CastRay` kullanmak (opsiyonel layer filtresiyle).
  3. `WebView2Compositor.cpp` ve `InspectorPanel.tsx` içindeki sahte `drag -> Friction` etiketini düzeltip gerçek `MotionType`, `Shape`, `HalfExtents`, `Restitution`, `Friction`, `UseGravity` ve `Damping` düzenleme desteği sağlamak.
* **Etkilenen Dosyalar:** `Engine/Source/Physics/src/PhysicsWorld.cpp`, `Engine/Source/UI/src/WebView2Compositor.cpp`, `Editor/Frontend/src/components/InspectorPanel.tsx`, `Editor/Frontend/src/types.ts`.
* **Test Yöntemi:** 2 substep tetiklenen (`dt = 0.034s`) karede ilk substep çarpışmasının `GetRecentCollisions()` içinde kaldığını doğrula; sıfır yönlü raycast'in `false` döndüğünü doğrula.

---

## 5. Doğrulama ve Performans Test Senaryoları (10 Senaryo)

Aşağıdaki 10 senaryo, Jolt Physics 5.2.0 entegrasyonu sonrası C++ birim/entegrasyon test paketi (`MorbiPhysicsTests` veya `--physics-test`) olarak otomatik doğrulanmalıdır:

| # | Senaryo | Kurulum / Girdi | Kabul / Başarı Kriteri |
| :--- | :--- | :--- | :--- |
| **1** | **Tek kutunun zemine düşmesi** | `Floor` Static Box ($y=0$, kalınlık $1\text{ m}$), `Box` Dynamic ($1\text{ kg}$, $y=5\text{ m}$, $g=-9.81\text{ m/s}^2$, $e=0$). | Serbest düşüş süresi kuramsal $\sqrt{2h/g}$ ile uyuşur; kutu zemin üst yüzeyinde ($y \approx 1.0\text{ m}$) durur, zemin içine penetre olmaz ve sleep durumuna geçer. |
| **2** | **Farklı kütlelerde iki kutunun çarpışması** | Kutu A ($m=10\text{ kg}$, $v_x=+5\text{ m/s}$) vs Kutu B ($m=1\text{ kg}$, $v_x=0$, $e=1$, $f=0$). | Yatay çarpışma gerçekleşir (penetrasyon yok); toplam lineer momentum $\Delta P < 10^{-3}$ ile korunur; hafif kutu beklenen daha yüksek hızla fırlar. |
| **3** | **30 kutunun üst üste istiflenmesi** | 30 adet $1\times 1\times 1\text{ m}$ kutu Static zemin üzerine dikey dizilir ($5\text{ sn}$ simülasyon). | Kule devrilmez/çökmez, net penetrasyon $< 1\text{ cm}$/katman kalır, kutular uyku (`!IsAwake()`) durumuna oturur, jitter sıfıra iner. |
| **4** | **Eğik yüzeyde kutunun kayması** | $30^\circ$ eğimli Static rampa üzerinde iki Dynamic kutu: Kutu 1 ($\mu = 0.1$) ve Kutu 2 ($\mu = 0.9$). ($\tan 30^\circ \approx 0.577$). | Kutu 1 eğim boyunca hızlanarak aşağı kayar; Kutu 2 yüksek sürtünme nedeniyle durgun kalır veya hemen durur. |
| **5** | **Farklı restitution değerlerinde küre sekmesi** | 3 adet `Sphere` ($e=0.0$, $e=0.5$, $e=0.9$), $h_0=10\text{ m}$ yükseklikten sert zemine ($e_{\text{floor}}=1.0$ veya max combine) bırakılır. | $e=0.0$ sekmez; $e=0.5$ ilk sekmesi $\approx 2.5\text{ m}$; $e=0.9$ ilk sekmesi $\approx 8.1\text{ m}$ yüksekliğe ulaşır (ve şekil Sphere olarak test edilir). |
| **6** | **Hızlı hareket eden cismin ince duvara çarpması** | $0.05\text{ m}$ kalınlıklı Static duvar, karşısında $200\text{ m/s}$ hızlı CCD aktif (`LinearCast`) mermi kutu ($1/60\text{ s}$ adımda $3.33\text{ m}$ yol). | Mermi duvarın arkasında doğmaz (tunneling = 0); duvar önünde çarpışma olayı (`CollisionPairEvent`) üretilir ve seker/durur. |
| **7** | **Dynamic ve Kinematic cisim etkileşimi** | Yatayda $3\text{ m/s}$ ile `MoveKinematic` hareketi yapan blok, önünde duran $2\text{ kg}$ Dynamic kutuya çarpar. | Kinematic blok sapmaz/durmaz; Dynamic kutu itilerek sürüklenir, Kinematic bloğun içine gömülmez. |
| **8** | **1000 rigid body performans testi** | 1000 adet Dynamic Box/Sphere rastgele yükseklikten geniş zemine düşürülür ($300$ kare). | Jolt BroadPhase + çok çekirdekli `JobSystem` ile ortalama fizik adım süresi `< 4.0 ms` (60 Hz bütçe içi) kalır; brute-force $O(N^2)$ darboğazı görülmez. |
| **9** | **Entity silme ve yeniden oluşturma testi** | 200 entity oluşturulup simüle edilir, 100 tanesi `DestroyEntity` ile silinir, 100 yeni entity eklenir ve sahne baştan yüklenir. | Her silme/yükleme sonrası Jolt'taki aktif/kayıtlı gövde sayısı Scene `RigidBodies().Size()` ile birebir eşleşir; hayalet çarpışma ve bellek sızıntısı olmaz. |
| **10** | **Uzun süreli simülasyonda fizik kararlılığı** | Karma sahnede (yığın + hareketli platform + küreler) $10.000+$ kare ($\sim 3$ dakika simülasyon) koşturulur. | Hiçbir body `NaN`/`Inf` üretmez, enerji artışı (patlama) oluşmaz, uyuyan gövdeler CPU kullanmaz ve `Accumulator` sapmaz. |

---

## 6. Önerilen Uygulama Yol Haritası (Sonraki Adım Planı)

1. **Faz 1 — Jolt Physics 5.2.0 Bağlama ve Temel Yaşam Döngüsü:**
   - Jolt Physics v5.2.0'ı CMake'te etkinleştir; `PhysicsWorld::Impl`'i `JPH::PhysicsSystem`, `TempAllocatorImpl`, `JobSystem`, `BroadPhaseLayerInterface`, `ContactListener` ile yeniden kur (`Box/Sphere/Capsule`, `Static/Kinematic/Dynamic`, `Restitution/Friction/Mass/Inertia`, `DestroyBody`, `RebuildFromScene`).
   - `Scene::SaveToJsonFile` `m_rigidBodies` serileştirmesini ve `EngineRuntime` entity/scene temizlik kancalarını tamamla.
2. **Faz 2 — Kinematik, CCD, Çoklu Substep Olayları & Raycast:**
   - `MoveKinematic`, `MotionQuality::LinearCast` CCD, substep dışı birikimli `CollisionPairEvent`, `JPH::NarrowPhaseQuery::CastRay` ve `Components.hpp` + C# `NativeBridge` kirli-bayrak/parametre senkronizasyonunu tamamla.
3. **Faz 3 — Thread-Safety, Editör Inspector & 10 Senaryo Otomatik Test Paketi:**
   - Jolt `BodyLockInterface` + komut tamponu kilitlemesi, `InspectorPanel` + `WebView2Compositor` gerçek `RigidBodyComponent` düzenleyicisi ve 10 senaryoluk deterministik `PhysicsTests` çalıştırıcısının eklenmesi.
