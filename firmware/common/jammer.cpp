/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "jammer.hpp"

#include "baseband_api.hpp"
#include "portapack.hpp"
using namespace portapack;

#include "string_format.hpp"
#include "utility.hpp"
#include "transmitter_model.hpp"
#include "sine_table_int8.hpp"

namespace jammer {

// Optimized LFSR for better noise generation
// Uses multiple taps for improved randomness
uint32_t optimized_lfsr(uint32_t& lfsr_state) {
    // Galois LFSR with taps at positions 32, 31, 29, 1 (0-indexed)
    const uint32_t taps = 0xA3000000;  // 32, 31, 29, 1
    uint32_t feedback = lfsr_state & 1;
    lfsr_state >>= 1;
    if (feedback) {
        lfsr_state ^= taps;
    }
    return lfsr_state;
}

// Generate broadband noise using multiple LFSR stages
int8_t generate_broadband_noise(uint32_t& lfsr1, uint32_t& lfsr2) {
    uint32_t noise1 = optimized_lfsr(lfsr1);
    uint32_t noise2 = optimized_lfsr(lfsr2);

    // Combine two LFSR outputs for better spectral properties
    int16_t combined = (int16_t)(noise1 & 0xFF) - 128 + (int16_t)(noise2 & 0xFF) - 128;

    // Clamp to int8 range
    if (combined > 127) return 127;
    if (combined < -128) return -128;
    return (int8_t)combined;
}

// Configure transmitter for maximum power output
void configure_max_power_transmission() {
    // Set maximum TX gain (47 is maximum for PortaPack)
    transmitter_model.set_tx_gain(47);

    // Enable RF amplifier for maximum power
    transmitter_model.set_rf_amp(true);

    // Set maximum baseband bandwidth for widest possible jamming
    transmitter_model.set_baseband_bandwidth(28000000);  // 28MHz

    // Set sampling rate for optimal performance
    transmitter_model.set_sampling_rate(3072000);  // 3.072MHz

    // Enable transmitter
    transmitter_model.enable();
}

// Calculate optimal jamming parameters for given bandwidth
void calculate_optimal_jamming_params(uint32_t bandwidth_hz, uint32_t& sample_rate, uint32_t& noise_period) {
    // Sample rate should be at least 2x the bandwidth for proper Nyquist sampling
    sample_rate = bandwidth_hz * 2;

    // Cap at hardware maximum
    if (sample_rate > 3072000) {
        sample_rate = 3072000;
    }

    // Noise period affects the rate of noise variation
    // Lower period = faster noise changes = better jamming
    noise_period = sample_rate / (bandwidth_hz / 1000);  // Adjust based on bandwidth

    // Ensure minimum period for stability
    if (noise_period < 10) noise_period = 10;
}

// Generate frequency hopping sequence for sweep jamming
uint32_t generate_hop_frequency(uint32_t base_freq, uint32_t bandwidth, uint32_t hop_index, uint32_t total_hops) {
    // Linear frequency sweep
    uint32_t freq_offset = (bandwidth * hop_index) / total_hops;
    return base_freq + freq_offset;
}

// Advanced noise generation for different jamming types
int8_t generate_jamming_sample(JammerType type, uint32_t& lfsr1, uint32_t& lfsr2,
                              uint32_t& phase, uint32_t delta, uint32_t sample_count) {
    switch (type) {
        case JammerType::TYPE_FSK:
            // Fast frequency shift keying with broadband noise
            return generate_broadband_noise(lfsr1, lfsr2);

        case JammerType::TYPE_TONE:
            // Pure tone with frequency modulation
            phase += delta;
            return (int8_t)((sine_table_i8[(phase >> 24) & 0xFF] * generate_broadband_noise(lfsr1, lfsr2)) >> 7);

        case JammerType::TYPE_SWEEP:
            // Frequency sweep with noise modulation
            phase += delta + (optimized_lfsr(lfsr1) & 0xFFFF);
            return (int8_t)((sine_table_i8[(phase >> 24) & 0xFF] * generate_broadband_noise(lfsr1, lfsr2)) >> 7);

        default:
            return generate_broadband_noise(lfsr1, lfsr2);
    }
}

// Initialize jamming parameters for optimal performance
void initialize_jamming_params(uint32_t bandwidth_hz, JammerType type,
                              uint32_t& sample_rate, uint32_t& noise_period,
                              uint32_t& lfsr1, uint32_t& lfsr2) {
    calculate_optimal_jamming_params(bandwidth_hz, sample_rate, noise_period);

    // Initialize LFSR states with different seeds for better randomness
    lfsr1 = 0xDEADBEEF;
    lfsr2 = 0xCAFEBABE;

    // Configure transmitter for maximum power
    configure_max_power_transmission();
}

} /* namespace jammer */
