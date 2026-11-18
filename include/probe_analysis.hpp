#pragma once

#include "probe_data.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include <numbers>
#include <utility>
#include <vector>

// Pure functions on the sample history: no moss, no Vulkan

constexpr size_t ANALYSIS_MIN_SAMPLES = 128;
constexpr double ANALYSIS_MIN_AMPLITUDE = 0.01;   // rms of v / U below this counts as no shedding
constexpr double STROUHAL_MIN = 0.02;             // peak search range
constexpr double STROUHAL_MAX = 1.0;
constexpr size_t PAD_FACTOR = 4;                  // zero padding of the FFT

// In-place radix-2 FFT. The size must be a power of two
inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();

    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2.0 * std::numbers::pi / double(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t start = 0; start < n; start += length) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < length / 2; k++) {
                const std::complex<double> even = a[start + k];
                const std::complex<double> odd = a[start + k + length / 2] * w;
                a[start + k] = even + odd;
                a[start + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}

// Linear resampling of an increasing time series onto n evenly spaced points
inline std::vector<double> resampleUniform(const std::vector<double>& time, const std::vector<double>& value, double dt) {
    const size_t n = value.size();
    std::vector<double> out(n);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        const double target = time.front() + double(i) * dt;
        while (j + 2 < n && time[j + 1] < target) j++;
        const double span = time[j + 1] - time[j];
        const double w = span > 0.0 ? std::clamp((target - time[j]) / span, 0.0, 1.0) : 0.0;
        out[i] = value[j] * (1.0 - w) + value[j + 1] * w;
    }
    return out;
}

// Strouhal number St = f D / U of the transverse velocity at the primary probe.
// inflow in cells/s, diameter in cells, analyseFrom in t U / D
inline ProbeResult analyseProbe(const std::deque<ProbeSample>& samples, f32 analyseFrom, f32 inflow, f32 diameter) {
    ProbeResult result;
    if (!(inflow > 0.0f) || !(diameter > 0.0f)) {
        result.status = ProbeStatus::INVALID;
        return result;
    }

    const double startTime = double(analyseFrom) * diameter / inflow;
    std::vector<double> time, value;
    for (const ProbeSample& s : samples) {
        if (s.time < startTime) continue;
        time.push_back(s.time);
        value.push_back(s.velocity[PROBE_PRIMARY][1]);
    }
    result.samples = u32(value.size());
    if (value.size() < ANALYSIS_MIN_SAMPLES) return result;   // still COLLECTING

    const size_t n = value.size();
    const double dt = (time.back() - time.front()) / double(n - 1);
    result.window = f32((time.back() - time.front()) * inflow / diameter);

    std::vector<double> x = resampleUniform(time, value, dt);

    double mean = 0.0;
    for (double v : x) mean += v;
    mean /= double(n);
    double squares = 0.0;
    for (double& v : x) { v -= mean; squares += v * v; }

    result.amplitude = f32(std::sqrt(squares / double(n)) / inflow);
    if (result.amplitude < ANALYSIS_MIN_AMPLITUDE) {
        result.status = ProbeStatus::NO_SHEDDING;
        return result;
    }

    /* ----------------------------- FFT peak ----------------------------- */
    size_t padded = 1;
    while (padded < PAD_FACTOR * n) padded <<= 1;

    std::vector<std::complex<double>> spectrum(padded);
    for (size_t i = 0; i < n; i++) {
        const double hann = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * double(i) / double(n - 1)));
        spectrum[i] = x[i] * hann;
    }
    fft(spectrum);

    const double strouhalPerBin = diameter / (double(inflow) * double(padded) * dt);
    const size_t first = std::max<size_t>(1, size_t(std::ceil(STROUHAL_MIN / strouhalPerBin)));
    const size_t last = std::min<size_t>(padded / 2 - 1, size_t(std::floor(STROUHAL_MAX / strouhalPerBin)));
    if (first >= last) {
        result.status = ProbeStatus::INVALID;
        return result;
    }

    size_t peak = first;
    for (size_t k = first; k <= last; k++)
        if (std::abs(spectrum[k]) > std::abs(spectrum[peak])) peak = k;

    // Parabola through the peak and its neighbours gives a sub-bin position
    const double a = std::abs(spectrum[peak - 1]);
    const double b = std::abs(spectrum[peak]);
    const double c = std::abs(spectrum[peak + 1]);
    const double denominator = a - 2.0 * b + c;
    const double shift = denominator != 0.0 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0.0;

    result.strouhal = f32((double(peak) + shift) * strouhalPerBin);
    result.frequency = result.strouhal * inflow / diameter;

    /* ------------------------ Zero-crossing period ------------------------ */
    std::vector<double> crossings;
    for (size_t i = 0; i + 1 < n; i++) {
        if (x[i] < 0.0 && x[i + 1] >= 0.0) {
            const double fraction = -x[i] / (x[i + 1] - x[i]);
            crossings.push_back((double(i) + fraction) * dt);
        }
    }
    if (crossings.size() >= 3) {
        result.periods = u32(crossings.size() - 1);
        const double period = (crossings.back() - crossings.front()) / double(result.periods);
        result.strouhalCrossings = f32(double(diameter) / (double(inflow) * period));
    }

    result.status = ProbeStatus::OK;
    return result;
}
