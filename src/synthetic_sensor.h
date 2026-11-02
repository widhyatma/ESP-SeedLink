#pragma once

#include <Arduino.h>
#include <math.h>
#if __has_include("config.h")
#include "config.h"
#endif

// ==============================================================================
// MULTI-CHANNEL REALISTIC SEISMIC SENSOR SIMULATOR (FDSN MULTI-RATE: E, B, S)
// ==============================================================================
// Menghasilkan sinyal seismik sintetis yang alami untuk 9 saluran standar FDSN:
// EHZ, EHN, EHE (Extremely Short Period / High Rate @ 100 Hz)
// BHZ, BHN, BHE (Broadband @ 50 Hz)
// SHZ, SHN, SHE (Short Period @ 50 Hz)
//
// Karakteristik Fisika:
// 1. Derau latar (ambient noise) Gaussian terfilter berbeda & independen per channel.
// 2. Osilasi mikroseisme samudra alami (0.15 Hz & 0.23 Hz) dengan relasi komponen 3-sumbu (Z, N, E).
// 3. Respon frekuensi berbeda sesuai band & sample rate (E: ~25Hz, B: ~8Hz, S: ~6Hz).
// 4. Simulasi gempa bumi triaksial realistis:
//    - Komponen Z: Didominasi gelombang P (Primary wave) tiba lebih awal (~0s).
//    - Komponen N & E: Didominasi gelombang S (Shear wave) beramplitudo besar (~3.5s).
// 5. Zero DC Offset (DC Blocker rekursif pada setiap saluran).
// ==============================================================================

struct ChannelSimState {
    char code[4];
    char band;
    char orientation;
    float sampleRate;
    float dt;
    float alpha;
    float lp1;
    float lp2;
    float dcX1;
    float dcY1;
    float oceanPhase1;
    float oceanPhase2;
    uint32_t rngState;
    uint32_t sampleCount;
    uint32_t quakeSampleCount;
};

class SyntheticSeismicSensor {
private:
    // State untuk masing-masing dari 9 saluran FDSN
    ChannelSimState _channels[NUM_CHANNELS];

    // Simulasi gempa bumi global
    bool _isQuakeActive;

    // Fast Pseudo-Random Generator (Xorshift32) per channel
    inline float uniformRandom(uint32_t& rng) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return ((float)(int32_t)rng) / 2147483648.0f;
    }

    // Box-Muller Transform untuk menghasilkan distribusi Gaussian
    inline float gaussianRandom(uint32_t& rng) {
        float u1 = (uniformRandom(rng) + 1.0f) * 0.5f;
        float u2 = (uniformRandom(rng) + 1.0f) * 0.5f;
        if (u1 < 1e-6f) u1 = 1e-6f;
        return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
    }

public:
    SyntheticSeismicSensor()
        : _isQuakeActive(false)
    {
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            strncpy(_channels[i].code, AVAILABLE_CHANNELS[i].code, 4);
            _channels[i].band = AVAILABLE_CHANNELS[i].band;
            _channels[i].orientation = AVAILABLE_CHANNELS[i].orientation;
            _channels[i].sampleRate = (float)AVAILABLE_CHANNELS[i].sampleRate;
            _channels[i].dt = 1.0f / _channels[i].sampleRate;
            _channels[i].sampleCount = 0;
            _channels[i].quakeSampleCount = 0;
            
            // Alpha filter lowpass berdasarkan karakter frekuensi instrumen & sample rate
            if (_channels[i].band == 'E') {
                _channels[i].alpha = 0.55f; // High frequency (~25 Hz cutoff @ 100 Hz)
            } else if (_channels[i].band == 'B') {
                _channels[i].alpha = 0.40f; // Standard Broadband (~8 Hz cutoff @ 50 Hz)
            } else { // 'S'
                _channels[i].alpha = 0.35f; // Short Period geophone (~6 Hz cutoff @ 50 Hz)
            }

            _channels[i].lp1 = 0.0f;
            _channels[i].lp2 = 0.0f;
            _channels[i].dcX1 = 0.0f;
            _channels[i].dcY1 = 0.0f;
            _channels[i].oceanPhase1 = (float)i * 0.523f; // Beda fase antar channel
            _channels[i].oceanPhase2 = (float)i * 0.814f;
            // Seed RNG unik per channel agar noise tidak berkorelasi palsu
            _channels[i].rngState = 0x9E3779B9 + (uint32_t)(i * 0x6A09E667UL);
        }
    }

    void reset() {
        _isQuakeActive = false;
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            _channels[i].lp1 = 0.0f;
            _channels[i].lp2 = 0.0f;
            _channels[i].dcX1 = 0.0f;
            _channels[i].dcY1 = 0.0f;
            _channels[i].sampleCount = 0;
            _channels[i].quakeSampleCount = 0;
        }
    }

    // Membaca sampel untuk satu channel tertentu (0 s/d NUM_CHANNELS - 1)
    int32_t readChannel(size_t chIdx) {
        if (chIdx >= NUM_CHANNELS) return 0;
        ChannelSimState& ch = _channels[chIdx];
        ch.sampleCount++;

        // 1. Independent Gaussian Noise
        float white = gaussianRandom(ch.rngState) * (float)SEISMIC_NOISE_STD_DEV;

        // 2. 2-Pole IIR Lowpass Filter
        ch.lp1 += ch.alpha * (white - ch.lp1);
        ch.lp2 += ch.alpha * (ch.lp1 - ch.lp2);

        // 3. Gelombang mikroseisme samudra (0.15 Hz dan 0.23 Hz)
        ch.oceanPhase1 += 2.0f * (float)M_PI * 0.15f * ch.dt;
        ch.oceanPhase2 += 2.0f * (float)M_PI * 0.23f * ch.dt;
        if (ch.oceanPhase1 > 2.0f * (float)M_PI) ch.oceanPhase1 -= 2.0f * (float)M_PI;
        if (ch.oceanPhase2 > 2.0f * (float)M_PI) ch.oceanPhase2 -= 2.0f * (float)M_PI;

        float ocean = 0.0f;
        if (ch.orientation == 'Z') {
            ocean = 25.0f * sinf(ch.oceanPhase1) + 18.0f * sinf(ch.oceanPhase2);
        } else if (ch.orientation == 'N') {
            ocean = 28.0f * cosf(ch.oceanPhase1) + 20.0f * sinf(ch.oceanPhase2);
        } else { // 'E'
            ocean = 26.0f * sinf(ch.oceanPhase1 + 0.785f) + 19.0f * cosf(ch.oceanPhase2);
        }

        // 4. Simulasi Gempa Triaksial Realistis
        float quakeSignal = 0.0f;
        if (_isQuakeActive) {
            float tau = (float)ch.quakeSampleCount * ch.dt; // Waktu sejak onset (detik)
            ch.quakeSampleCount++;

            // P-wave phase (0 - 3.5 detik): dominan pada sumbu Vertical Z
            if (tau < 3.5f) {
                float pAmp = (QUAKE_MAX_AMPLITUDE * 0.40f);
                if (ch.orientation == 'Z') {
                    quakeSignal = pAmp * sinf(2.0f * (float)M_PI * 5.5f * tau);
                } else {
                    quakeSignal = (pAmp * 0.25f) * sinf(2.0f * (float)M_PI * 6.0f * tau + 0.5f);
                }
                quakeSignal += gaussianRandom(ch.rngState) * 40.0f;
            }
            // S-wave phase (3.5 - 9.0 detik): gelombang geser (shear) dominan pada horizontal N & E
            else if (tau < 9.0f) {
                float sTau = tau - 3.5f;
                float sAmp = (float)QUAKE_MAX_AMPLITUDE;
                if (ch.orientation == 'Z') {
                    quakeSignal = (sAmp * 0.35f) * sinf(2.0f * (float)M_PI * 2.2f * sTau);
                } else if (ch.orientation == 'N') {
                    quakeSignal = sAmp * sinf(2.0f * (float)M_PI * 2.0f * sTau);
                } else { // 'E'
                    quakeSignal = (sAmp * 0.90f) * sinf(2.0f * (float)M_PI * 1.8f * sTau + 1.2f);
                }
                quakeSignal += gaussianRandom(ch.rngState) * 60.0f;
            }
            // Surface & Coda wave decay (9.0 - 25.0 detik): peluruhan eksponensial halus
            else if (tau < 25.0f) {
                float codaTau = tau - 9.0f;
                float decay = expf(-codaTau * 0.22f);
                float codaAmp = (QUAKE_MAX_AMPLITUDE * 0.65f * decay);
                quakeSignal = codaAmp * sinf(2.0f * (float)M_PI * 1.0f * codaTau) + 
                              gaussianRandom(ch.rngState) * (25.0f * decay);
            } else {
                if (chIdx == 0) {
                    _isQuakeActive = false;
                    Serial.println("[SENSOR] <<< Simulasi Gempa Triaksial Selesai. Kembali tenang.");
                }
            }
        }

        float rawCombined = ch.lp2 + ocean + quakeSignal;

        // 5. DC Blocker Highpass Filter (~0.05 Hz)
        float dcBlocked = rawCombined - ch.dcX1 + 0.995f * ch.dcY1;
        ch.dcX1 = rawCombined;
        ch.dcY1 = dcBlocked;

        return (int32_t)dcBlocked;
    }

    // Pemicu gempa manual via Serial Monitor
    void triggerQuake() {
        _isQuakeActive = true;
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            _channels[i].quakeSampleCount = 0;
        }
        Serial.println("[SENSOR] >>> Simulasi Gempa Bumi Triaksial Dipicu (Z, N, E)! P-wave lalu S-wave.");
    }

    bool isQuakeActive() const {
        return _isQuakeActive;
    }

    size_t getChannelCount() const {
        return NUM_CHANNELS;
    }
};
