#include "Lfo.h"
#include <algorithm>
#include <cmath>

namespace mm
{

void Lfo::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    reset();
}

void Lfo::reset() noexcept
{
    phase = 0.0;
    fadeElapsed = 1.0e9;
}

static double wrap01 (double x) noexcept
{
    x -= std::floor (x);
    return x >= 1.0 ? 0.0 : x;
}

float Lfo::evaluate (LfoShape shape, double p, const float* table) noexcept
{
    p = wrap01 (p);

    switch (shape)
    {
        case LfoShape::sine:       return (float) std::sin (p * juce::MathConstants<double>::twoPi);
        case LfoShape::triangle:   return (float) (p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));
        case LfoShape::square:     return p < 0.5 ? 1.0f : -1.0f;
        case LfoShape::saw:        return (float) (2.0 * p - 1.0);
        case LfoShape::reverseSaw: return (float) (1.0 - 2.0 * p);
        case LfoShape::custom:
        {
            if (table == nullptr)
                return 0.0f;

            const double pos = p * kLfoTableSize;
            const int i0 = (int) pos % kLfoTableSize;
            const int i1 = (i0 + 1) % kLfoTableSize;
            const float frac = (float) (pos - std::floor (pos));
            return table[i0] + (table[i1] - table[i0]) * frac;
        }
        case LfoShape::numShapes:  break;
    }

    return 0.0f;
}

float Lfo::process (const LfoSettings& s, const TransportInfo& t, int numSamples,
                    float rateHz, float rateMultiplier, float depth, float phaseOffset,
                    const float* customTable) noexcept
{
    const bool retrig = (s.retrigger == LfoRetrigger::transport && t.playStarted)
                     || (s.retrigger == LfoRetrigger::note && t.noteOn);

    if (retrig)
    {
        phase = 0.0;
        fadeElapsed = 0.0;
    }

    double cyclesPerSecond;
    const double quarters = getDivisions()[(size_t) juce::jlimit (0, (int) getDivisions().size() - 1, s.division)].quarters;

    if (s.sync)
    {
        cyclesPerSecond = (t.bpm / 60.0) / quarters * rateMultiplier;

        // Lock to the song position so the LFO lands on the grid every time
        // playback starts at the same place.
        if (t.isPlaying && t.hasPpq && std::abs (rateMultiplier - 1.0f) < 1.0e-6f
             && s.retrigger != LfoRetrigger::note)
            phase = wrap01 (t.ppq / quarters);
    }
    else
    {
        cyclesPerSecond = rateHz;
    }

    float value = evaluate (s.shape, phase + phaseOffset, customTable);

    if (s.invert)
        value = -value;

    float fade = 1.0f;
    if (s.fadeInSeconds > 0.0f)
        fade = (float) std::min (1.0, fadeElapsed / s.fadeInSeconds);

    const float out = juce::jlimit (-1.0f, 1.0f, value * depth * fade + s.offset);

    const double dt = numSamples / sampleRate;
    phase = wrap01 (phase + cyclesPerSecond * dt);
    fadeElapsed += dt;

    return out;
}

std::vector<WavePoint> Lfo::defaultCustomPoints()
{
    return { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.75f, -1.0f, 0.0f } };
}

static float shapeCurve (float t, float curve) noexcept
{
    if (std::abs (curve) < 1.0e-4f)
        return t;

    const float k = curve * 6.0f;
    return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
}

void Lfo::buildCustomTable (std::vector<WavePoint> points, float* table)
{
    for (auto& p : points)
    {
        p.x = juce::jlimit (0.0f, 1.0f, p.x);
        p.y = juce::jlimit (-1.0f, 1.0f, p.y);
        p.curve = juce::jlimit (-1.0f, 1.0f, p.curve);
    }

    std::sort (points.begin(), points.end(), [] (const WavePoint& a, const WavePoint& b) { return a.x < b.x; });

    if (points.empty())
    {
        std::fill (table, table + kLfoTableSize, 0.0f);
        return;
    }

    if (points.size() == 1)
    {
        std::fill (table, table + kLfoTableSize, points[0].y);
        return;
    }

    const size_t n = points.size();

    for (int i = 0; i < kLfoTableSize; ++i)
    {
        const float x = (float) i / (float) kLfoTableSize;

        // Find the segment [a, b) containing x, wrapping from the last point
        // back to the first so the waveform loops without a jump.
        size_t ai = n - 1;
        for (size_t k = 0; k < n; ++k)
            if (points[k].x <= x)
                ai = k;

        const bool beforeFirst = x < points[0].x;
        const WavePoint& a = beforeFirst ? points[n - 1] : points[ai];
        const WavePoint& b = beforeFirst ? points[0] : points[(ai + 1) % n];

        float ax = a.x, bx = b.x, px = x;
        if (bx <= ax) bx += 1.0f;           // wrapping segment
        if (px < ax)  px += 1.0f;

        const float span = bx - ax;
        const float tt = span > 1.0e-6f ? (px - ax) / span : 0.0f;
        table[i] = a.y + (b.y - a.y) * shapeCurve (juce::jlimit (0.0f, 1.0f, tt), a.curve);
    }
}

} // namespace mm
