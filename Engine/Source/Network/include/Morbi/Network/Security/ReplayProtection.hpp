#pragma once

#include "Morbi/Core/Types.hpp"

namespace Morbi::Network
{
    /// Two-phase 128-packet sliding replay window.
    /// Call CanAccept(seq) BEFORE AEAD authentication, and Commit(seq) ONLY AFTER AEAD succeeds
    /// so unauthenticated spoofed packets cannot shift the highest accepted sequence.
    class ReplayWindow final
    {
    public:
        static constexpr u64 kWindowSize = 128;

        void Reset() noexcept
        {
            m_highestSequence = 0;
            m_bitmapLow = 0;
            m_bitmapHigh = 0;
            m_hasReceived = false;
        }

        [[nodiscard]] bool CanAccept(u64 sequence) const noexcept
        {
            if (sequence == 0)
            {
                return false;
            }
            if (!m_hasReceived)
            {
                return true;
            }
            if (sequence > m_highestSequence)
            {
                return true;
            }

            const u64 delta = m_highestSequence - sequence;
            if (delta >= kWindowSize)
            {
                return false;
            }
            if (delta < 64)
            {
                return (m_bitmapLow & (1ull << delta)) == 0;
            }
            return (m_bitmapHigh & (1ull << (delta - 64))) == 0;
        }

        void Commit(u64 sequence) noexcept
        {
            if (sequence == 0)
            {
                return;
            }
            if (!m_hasReceived)
            {
                m_highestSequence = sequence;
                m_bitmapLow = 1ull;
                m_bitmapHigh = 0ull;
                m_hasReceived = true;
                return;
            }

            if (sequence > m_highestSequence)
            {
                const u64 shift = sequence - m_highestSequence;
                if (shift >= kWindowSize)
                {
                    m_bitmapLow = 1ull;
                    m_bitmapHigh = 0ull;
                }
                else if (shift >= 64)
                {
                    m_bitmapHigh = m_bitmapLow << (shift - 64);
                    m_bitmapLow = 1ull;
                }
                else
                {
                    m_bitmapHigh = (m_bitmapHigh << shift) | (m_bitmapLow >> (64 - shift));
                    m_bitmapLow = (m_bitmapLow << shift) | 1ull;
                }
                m_highestSequence = sequence;
                return;
            }

            const u64 delta = m_highestSequence - sequence;
            if (delta >= kWindowSize)
            {
                return;
            }
            if (delta < 64)
            {
                m_bitmapLow |= (1ull << delta);
            }
            else
            {
                m_bitmapHigh |= (1ull << (delta - 64));
            }
        }

        [[nodiscard]] u64 GetHighestSequence() const noexcept { return m_highestSequence; }
        [[nodiscard]] bool HasReceivedAny() const noexcept { return m_hasReceived; }

    private:
        u64 m_highestSequence = 0;
        u64 m_bitmapLow = 0;
        u64 m_bitmapHigh = 0;
        bool m_hasReceived = false;
    };
}
