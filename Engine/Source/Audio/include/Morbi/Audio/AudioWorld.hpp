#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Scene/Scene.hpp"

namespace Morbi::Audio
{
    struct AudioStats
    {
        u32 LoadedSoundCount = 0;
        u32 ActiveVoiceCount = 0;
        u32 TotalPlayedCount = 0;
    };

    class MORBI_API AudioWorld
    {
    public:
        AudioWorld();
        ~AudioWorld();

        AudioWorld(const AudioWorld&) = delete;
        AudioWorld& operator=(const AudioWorld&) = delete;

        bool Initialize(bool headlessNoSoundDevice = false);
        void Shutdown();

        // Generates a procedural PCM WAV file on disk (useful for synthesized impact/laser tones or tests)
        static bool GenerateProceduralToneWav(
            const std::filesystem::path& filePath,
            f32 frequencyHz = 440.0f,
            f32 durationSeconds = 0.25f);

        bool PreloadSound(std::string_view virtualKey, const std::filesystem::path& filePath);

        void SetListenerTransform(
            const Math::Vec3& position,
            const Math::Vec3& forward = Math::Vec3::Forward(),
            const Math::Vec3& up = Math::Vec3::Up());

        bool PlayOneShot2D(std::string_view virtualKey, f32 volume = 1.0f, f32 pitch = 1.0f);
        bool PlayOneShot3D(std::string_view virtualKey, const Math::Vec3& position, f32 volume = 1.0f, f32 pitch = 1.0f);

        // Synchronizes SoA AudioSourceComponents & TransformComponents with SoLoud 3D voices
        void Update(f32 deltaTime, Scene::Scene& scene);

        [[nodiscard]] AudioStats GetStats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
