#include "Morbi/Audio/AudioWorld.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

#if MORBI_ENABLE_SOLOUD
    #include "soloud.h"
    #include "soloud_wav.h"
#endif

#include "Morbi/Core/Log.hpp"
#include "Morbi/Core/StringId.hpp"

namespace Morbi::Audio
{
    struct AudioWorld::Impl
    {
#if MORBI_ENABLE_SOLOUD
        SoLoud::Soloud Engine;
        std::unordered_map<u64, std::unique_ptr<SoLoud::Wav>> LoadedWavs;
#endif
        AudioStats Stats{};
        bool Initialized = false;
    };

    AudioWorld::AudioWorld()
        : m_impl(std::make_unique<Impl>())
    {
    }

    AudioWorld::~AudioWorld()
    {
        Shutdown();
    }

    bool AudioWorld::Initialize(bool headlessNoSoundDevice)
    {
        if (m_impl->Initialized)
        {
            return true;
        }

#if MORBI_ENABLE_SOLOUD
        const unsigned int backend = headlessNoSoundDevice
            ? static_cast<unsigned int>(SoLoud::Soloud::NULLDRIVER)
            : static_cast<unsigned int>(SoLoud::Soloud::MINIAUDIO);

        unsigned int res = m_impl->Engine.init(
            SoLoud::Soloud::CLIP_ROUNDOFF,
            backend);
        if (res != SoLoud::SO_NO_ERROR && !headlessNoSoundDevice)
        {
            // Fallback to NULLDRIVER mixer if no hardware audio output device is connected
            res = m_impl->Engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER);
        }
        if (res != SoLoud::SO_NO_ERROR)
        {
            Core::Log::Error("Audio", "SoLoud initialization failed with error code: " + std::to_string(res));
            return false;
        }
        m_impl->Engine.setGlobalVolume(1.0f);
#else
        (void)headlessNoSoundDevice;
#endif
        m_impl->Initialized = true;
        Core::Log::Info("Audio", std::string("Initialized SoLoud 3D Spatial Audio Engine [Backend=") +
            (headlessNoSoundDevice ? "NoSound/HeadlessMixer" : "Miniaudio") + "]");
        return true;
    }

    void AudioWorld::Shutdown()
    {
        if (!m_impl->Initialized)
        {
            return;
        }
#if MORBI_ENABLE_SOLOUD
        m_impl->Engine.stopAll();
        m_impl->LoadedWavs.clear();
        m_impl->Engine.deinit();
#endif
        m_impl->Initialized = false;
    }

    bool AudioWorld::GenerateProceduralToneWav(
        const std::filesystem::path& filePath,
        f32 frequencyHz,
        f32 durationSeconds)
    {
        if (filePath.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        constexpr u32 kSampleRate = 44100;
        constexpr u16 kNumChannels = 1;
        constexpr u16 kBitsPerSample = 16;
        const u32 numSamples = static_cast<u32>(static_cast<f32>(kSampleRate) * std::max(durationSeconds, 0.05f));
        const u32 dataChunkSize = numSamples * kNumChannels * (kBitsPerSample / 8);
        const u32 riffSize = 36 + dataChunkSize;
        const u32 byteRate = kSampleRate * kNumChannels * (kBitsPerSample / 8);
        const u16 blockAlign = kNumChannels * (kBitsPerSample / 8);

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            return false;
        }

        out.write("RIFF", 4);
        out.write(reinterpret_cast<const char*>(&riffSize), 4);
        out.write("WAVEfmt ", 8);
        const u32 fmtSize = 16;
        const u16 audioFormat = 1; // PCM
        out.write(reinterpret_cast<const char*>(&fmtSize), 4);
        out.write(reinterpret_cast<const char*>(&audioFormat), 2);
        out.write(reinterpret_cast<const char*>(&kNumChannels), 2);
        out.write(reinterpret_cast<const char*>(&kSampleRate), 4);
        out.write(reinterpret_cast<const char*>(&byteRate), 4);
        out.write(reinterpret_cast<const char*>(&blockAlign), 2);
        out.write(reinterpret_cast<const char*>(&kBitsPerSample), 2);
        out.write("data", 4);
        out.write(reinterpret_cast<const char*>(&dataChunkSize), 4);

        for (u32 i = 0; i < numSamples; ++i)
        {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(kSampleRate);
            const f32 envelope = std::max(0.0f, 1.0f - (t / durationSeconds));
            const f32 sampleFloat = std::sin(2.0f * Math::kPi * frequencyHz * t) * envelope * 0.45f;
            const i16 pcmSample = static_cast<i16>(std::clamp(sampleFloat, -1.0f, 1.0f) * 32767.0f);
            out.write(reinterpret_cast<const char*>(&pcmSample), sizeof(i16));
        }

        return out.good();
    }

    bool AudioWorld::PreloadSound(std::string_view virtualKey, const std::filesystem::path& filePath)
    {
        if (!m_impl->Initialized)
        {
            return false;
        }
        const u64 keyHash = Core::StringId::FromString(virtualKey).Value();

#if MORBI_ENABLE_SOLOUD
        auto wav = std::make_unique<SoLoud::Wav>();
        if (wav->load(filePath.string().c_str()) != SoLoud::SO_NO_ERROR)
        {
            return false;
        }
        m_impl->LoadedWavs[keyHash] = std::move(wav);
        m_impl->Stats.LoadedSoundCount = static_cast<u32>(m_impl->LoadedWavs.size());
#else
        (void)keyHash;
        (void)filePath;
#endif
        return true;
    }

    void AudioWorld::SetListenerTransform(const Math::Vec3& position, const Math::Vec3& forward, const Math::Vec3& up)
    {
        if (!m_impl->Initialized) return;
#if MORBI_ENABLE_SOLOUD
        m_impl->Engine.set3dListenerParameters(
            position.x, position.y, position.z,
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z,
            0.0f, 0.0f, 0.0f);
        m_impl->Engine.update3dAudio();
#else
        (void)position; (void)forward; (void)up;
#endif
    }

    bool AudioWorld::PlayOneShot2D(std::string_view virtualKey, f32 volume, f32 pitch)
    {
        if (!m_impl->Initialized) return false;
        const u64 keyHash = Core::StringId::FromString(virtualKey).Value();
#if MORBI_ENABLE_SOLOUD
        const auto it = m_impl->LoadedWavs.find(keyHash);
        if (it == m_impl->LoadedWavs.end() || !it->second)
        {
            return false;
        }
        const unsigned int voice = m_impl->Engine.play(*it->second, std::clamp(volume, 0.0f, 4.0f));
        if (voice == 0) return false;
        m_impl->Engine.setRelativePlaySpeed(voice, std::clamp(pitch, 0.1f, 4.0f));
        ++m_impl->Stats.TotalPlayedCount;
        return true;
#else
        (void)keyHash; (void)volume; (void)pitch;
        return false;
#endif
    }

    bool AudioWorld::PlayOneShot3D(std::string_view virtualKey, const Math::Vec3& position, f32 volume, f32 pitch)
    {
        if (!m_impl->Initialized) return false;
        const u64 keyHash = Core::StringId::FromString(virtualKey).Value();
#if MORBI_ENABLE_SOLOUD
        const auto it = m_impl->LoadedWavs.find(keyHash);
        if (it == m_impl->LoadedWavs.end() || !it->second)
        {
            return false;
        }
        it->second->set3dMinMaxDistance(1.0f, 50.0f);
        it->second->set3dAttenuation(SoLoud::AudioSource::INVERSE_DISTANCE, 1.0f);
        const unsigned int voice = m_impl->Engine.play3d(
            *it->second,
            position.x, position.y, position.z,
            0.0f, 0.0f, 0.0f,
            std::clamp(volume, 0.0f, 4.0f));
        if (voice == 0) return false;
        m_impl->Engine.setRelativePlaySpeed(voice, std::clamp(pitch, 0.1f, 4.0f));
        m_impl->Engine.update3dAudio();
        ++m_impl->Stats.TotalPlayedCount;
        return true;
#else
        (void)keyHash; (void)position; (void)volume; (void)pitch;
        return false;
#endif
    }

    void AudioWorld::Update(f32, Scene::Scene& scene)
    {
        if (!m_impl->Initialized) return;

#if MORBI_ENABLE_SOLOUD
        auto sources = scene.AudioSources().RawComponents();
        auto entities = scene.AudioSources().RawEntities();

        for (usize i = 0; i < sources.size(); ++i)
        {
            auto& src = sources[i];
            const auto* tr = scene.Transforms().TryGet(entities[i]);
            if (!tr) continue;

            if (src.PlayOnStart && src.ActiveVoiceHandle == 0 && src.SoundAssetId != 0)
            {
                if (const auto it = m_impl->LoadedWavs.find(src.SoundAssetId); it != m_impl->LoadedWavs.end() && it->second)
                {
                    it->second->setLooping(src.Looping != 0);
                    it->second->set3dMinMaxDistance(src.MinDistance, src.MaxDistance);
                    src.ActiveVoiceHandle = src.Spatial3D
                        ? m_impl->Engine.play3d(*it->second, tr->Position.x, tr->Position.y, tr->Position.z, 0.0f, 0.0f, 0.0f, src.Volume)
                        : m_impl->Engine.play(*it->second, src.Volume);
                    src.PlayOnStart = 0;
                    ++m_impl->Stats.TotalPlayedCount;
                }
            }

            if (src.ActiveVoiceHandle != 0 && m_impl->Engine.isValidVoiceHandle(src.ActiveVoiceHandle))
            {
                if (src.Spatial3D)
                {
                    m_impl->Engine.set3dSourcePosition(src.ActiveVoiceHandle, tr->Position.x, tr->Position.y, tr->Position.z);
                }
            }
        }

        m_impl->Engine.update3dAudio();
        m_impl->Stats.ActiveVoiceCount = m_impl->Engine.getActiveVoiceCount();
#else
        (void)scene;
#endif
    }

    AudioStats AudioWorld::GetStats() const noexcept
    {
        return m_impl->Stats;
    }
}
