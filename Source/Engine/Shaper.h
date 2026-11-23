#pragma once

#include "Lfo.h"

namespace mm
{

struct ShaperSettings
{
    ShaperTrigger trigger   = ShaperTrigger::note;
    bool  sync              = false;
    int   division          = 3;        // 1 bar
    float timeMs            = 1000.0f;
    float depth             = 1.0f;
    float thresholdDb       = -24.0f;   // audio trigger level
};

/** A drawn envelope generator. The curve (a custom waveform table, -1..1)
    plays once from start to end when triggered, or repeats in loop mode.
    Output is unipolar: 0..1 times depth.

    Triggers: a note-on, an audio transient crossing the threshold, the
    transport starting, or a manual trigger from the editor (any mode). */
class Shaper
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Starts the envelope at the next process() call. Any thread. */
    void trigger() noexcept   { pendingTrigger.store (true, std::memory_order_relaxed); }

    /** Watches audio for transients (audio trigger mode). */
    void feedAudio (const float* const* channels, int numChannels, int numSamples, float thresholdDb) noexcept;

    /** Value at the start of the sub-block, then advances.
        lengthSeconds already includes cross-modulation of the time;
        allowSongLock is false while the time is being modulated. */
    float process (const ShaperSettings& s, const TransportInfo& t, int numSamples, double lengthSeconds,
                   bool allowSongLock, float depth, const float* table) noexcept;

    double getPhase() const noexcept    { return phase; }
    bool isRunning() const noexcept     { return running; }

    /** The curve's value at a position, mapped to 0..1. */
    static float valueAt (const float* table, double position) noexcept;

    static std::vector<WavePoint> defaultPoints();

private:
    double sampleRate = 44100.0;
    double phase = 1.0;          // 0..1 through the curve; 1 = finished
    bool running = false;
    std::atomic<bool> pendingTrigger { false };
    bool audioTriggered = false;

    // Transient detector
    float detector = 0.0f;
    bool above = false;
    double holdoffSeconds = 0.0;
};

} // namespace mm
