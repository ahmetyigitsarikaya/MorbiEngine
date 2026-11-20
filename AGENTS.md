# MorbiEngine - Agent Guidelines & Engineering Standards

Bu dosya, projede yer alan [`.agents/`](.agents) kural, iş akışı, yetenek (skills) ve uzman ajan (subagents) ekosistemini **MorbiEngine** geliştirme süreçlerine bağlayan ana kılavuzdur.

---

## 1. Mimari ve Teknoloji Yığını

* **Çekirdek & Render (C++20):**
  * Modern C++20 standartları, RAII, Rule of Zero/Five, sıfır ham işaretçi sızıntısı.
  * `/W4` seviyesinde sıfır hata ve sıfır uyarı derleme disiplini.
  * Vulkan 1.3 (Dynamic Rendering, Timeline Semaphore, 4096-slot Bindless Descriptors) ve DirectX 12 Agility SDK.
  * DXC Dual-Target Shader derleme hattı (HLSL -> DXIL & SPIR-V).
  * Özel Sparse-Set SoA ECS mimarisi (40-byte hizalı contiguous TransformComponent bellek havuzu).
  * Jolt Physics 3D fizik motoru ve SoLoud 3D mekansal ses motoru.
* **Scripting (.NET 8 CoreCLR):**
  * `hostfxr` tabanlı CoreCLR host ve `UnmanagedCallersOnly` sıfır ek yük fonksiyon tablosu.
  * `Span<TransformComponent>` ile C++ ECS bellek havuzuna doğrudan zero-copy erişim.
  * `Collectible AssemblyLoadContext` ile çalışma zamanında dinamik C# Hot-Reload.
* **Editör Arayüzü (React 18 + TypeScript + Tailwind CSS):**
  * Edge WebView2 ve Win32 izole child viewport penceresi (`viewportChildHwnd`).
  * Yüksek frekanslı (60+ Hz) 64-byte Binary IPC halka arabelleği (`Local\MorbiEngine_UI_SharedArena`).
  * GPU RenderGraph üzerinde doğrudan çalışan 3D Transform Gizmo (Translate, Rotate, Scale) ve Game HUD.

---

## 2. `.agents/` Kural Entegrasyonu (Rules)

Geliştirme sırasında aşağıdaki kural setleri koşulsuz uygulanır:

### C++ Standartları ([`cpp-coding-style.md`](.agents/rules/cpp-coding-style.md), [`cpp-patterns.md`](.agents/rules/cpp-patterns.md))
* Manuel `new`/`delete` yasaktır; her zaman RAII ve `std::unique_ptr` / `std::shared_ptr` kullanılır.
* Fonksiyonlar küçük (<50 satır), dosyalar odaklı (<800 satır) tutulur.
* Derin iç içe geçmiş koşullardan kaçınılır (`early return` deseni).
* Hatalar asla yutulmaz; açık şekilde loglanır veya `Morbi::Core::Result<T, E>` / `std::optional` ile döndürülür.

### C# Standartları ([`csharp-coding-style.md`](.agents/rules/csharp-coding-style.md), [`csharp-patterns.md`](.agents/rules/csharp-patterns.md))
* Nullable reference types (`#nullable enable`) zorunludur.
* Değişmez modeller için `record` / `record struct` tercih edilir.
* Bellek tahsisini minimize etmek için `ref struct`, `Span<T>` ve `ReadOnlySpan<T>` kullanılır.

### TypeScript & React Standartları ([`typescript-coding-style.md`](.agents/rules/typescript-coding-style.md), [`react-coding-style.md`](.agents/rules/react-coding-style.md))
* Kesinlikle `any` kullanılmaz; tüm modeller ve proplar açık `interface` veya `type` ile tanımlanır.
* Fonksiyonel bileşenler ve özel React hook'ları (`use...`) kullanılır.
* Stil için Tailwind CSS kullanılır; inline style'dan kaçınılır.

### Ortak İlkeler ([`common-coding-style.md`](.agents/rules/common-coding-style.md), [`common-security.md`](.agents/rules/common-security.md))
* **KISS & DRY & YAGNI:** En yalın çalışan çözüm seçilir, spekülatif karmaşıklıktan kaçınılır.
* **Girdi Doğrulama:** VFS yolları, IPC paketleri ve kullanıcı girdileri sistem sınırında doğrulanır.
* **Sıfır Güvenlik Açığı:** Tampon taşması, bellek sızıntısı ve güvensiz dosya erişimleri engellenir.

---

## 3. Geliştirme ve Doğrulama İş Akışı (Workflow)

Her kod değişikliği aşağıdaki kapılardan geçmek zorundadır:

1. **Önce Planla:** Mimari ve etki analizi yap (`planner` veya `code-architect`).
2. **Minimal ve Hassas Değişiklik:** Var olan dokümantasyon ve yorumları koru, gereksiz dosyaları kirletme.
3. **Derleme Doğrulaması:**
   * Frontend: `cmd /c "cd /d Editor\Frontend && npm run build"`
   * C++ Motoru: `cmake --build --preset release` (0 hata, 0 uyarı).
4. **Smoke Test:**
   * `build\bin\Release\MorbiEditor.exe --smoke-test` çalıştırılarak tüm fazların [PASS] olduğu doğrulanır.
5. **Kod İncelemesi (Review):**
   * C++ değişikliklerinde `cpp-reviewer`, C# scriptlerinde `csharp-reviewer`, UI'da `react-reviewer` ve güvenlik kritik alanlarda `security-reviewer` kullanılır.

---

## 4. Uzman Ajanların (Subagents) Proaktif Kullanımı

Gerektiğinde şu ajanlar devreye alınır:
* `cpp-reviewer` / `cpp-build-resolver`: C++ derleme veya mimari sorunlarında.
* `csharp-reviewer`: .NET CoreCLR ve script entegrasyonlarında.
* `react-reviewer` / `react-build-resolver`: Editör arayüzü ve Vite/TSX geliştirmelerinde.
* `security-reviewer`: VFS, şifreleme ve IPC sınırlarında.
* `performance-optimizer`: Profiling, GPU rendergraph ve bellek optimizasyonlarında.
