// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
//
// test_fiview2_fuzz — randomised stress test for FilterState / recompute_slot.
//
// Systematically iterates all FilterFamily × FilterPassband × order
// combinations, then hammers each with random rate / fc1 / fc2 values
// including edge cases (fc > nyq, fc2 <= fc1, rate extremes).
// ASan + UBSan are active in Debug builds — any crash produces a backtrace.

#include "../fiview2/src/filter_state.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <vector>

using namespace fiview2;

// ── helpers ──────────────────────────────────────────────────────────────────

static const FilterFamily k_families[] = {
    FilterFamily::Butterworth,
    FilterFamily::Bessel,
    FilterFamily::Chebyshev,
    FilterFamily::FIR_Hann,
    FilterFamily::FIR_Hamming,
    FilterFamily::FIR_Blackman,
    FilterFamily::FIR_Bartlett,
    FilterFamily::PeakingEQ,
    FilterFamily::AllpassBiquad,
    FilterFamily::BandpassResonator,
};
[[maybe_unused]] static const char* k_family_names[] = {
    "Butterworth","Bessel","Chebyshev",
    "FIR_Hann","FIR_Hamming","FIR_Blackman","FIR_Bartlett",
    "PeakingEQ","AllpassBiquad","BandpassResonator",
};

static const FilterPassband k_passbands[] = {
    FilterPassband::LP, FilterPassband::HP,
    FilterPassband::BP, FilterPassband::BS,
};
[[maybe_unused]] static const char* k_pb_names[] = { "LP","HP","BP","BS" };

static bool is_fir(FilterFamily f) {
    return f == FilterFamily::FIR_Hann   || f == FilterFamily::FIR_Hamming ||
           f == FilterFamily::FIR_Blackman || f == FilterFamily::FIR_Bartlett;
}
static bool is_special(FilterFamily f) {
    return f == FilterFamily::PeakingEQ    ||
           f == FilterFamily::AllpassBiquad ||
           f == FilterFamily::BandpassResonator;
}

// ── single probe ─────────────────────────────────────────────────────────────

static int g_total = 0;

static void probe(FilterFamily fam, FilterPassband pb, int order,
                  double rate, double fc1, double fc2,
                  double ripple, double q, double gain)
{
    FilterParams p;
    p.family    = fam;
    p.passband  = pb;
    p.order     = order;
    p.rate      = rate;
    p.fc1       = fc1;
    p.fc2       = fc2;
    p.ripple_db = ripple;
    p.q_factor  = q;
    p.gain_db   = gain;

    FilterState st;
    st.params() = p;
    st.update();   // must not crash regardless of params

    ++g_total;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    const unsigned seed = (argc > 1) ? (unsigned)std::atoi(argv[1])
                                     : (unsigned)std::time(nullptr);
    std::printf("seed=%u\n", seed);
    std::mt19937 rng(seed);

    // Representative sample rates
    static const double k_rates[] = {
        100.0, 250.0, 1000.0, 8000.0, 22050.0, 44100.0, 96000.0, 192000.0
    };
    static const int k_n_rates = (int)(sizeof(k_rates)/sizeof(k_rates[0]));

    int pass = 0;

    // ── Phase 1: systematic sweep ─────────────────────────────────────────
    std::printf("Phase 1: systematic sweep\n");

    for (auto fam : k_families) {
        int max_order = (fam == FilterFamily::Bessel) ? 10 : 20;
        for (int order = 1; order <= max_order; ++order) {
            for (auto pb : k_passbands) {
                // FIR and special types ignore passband — only test LP to avoid redundancy
                if ((is_fir(fam) || is_special(fam)) && pb != FilterPassband::LP)
                    continue;

                for (int ri = 0; ri < k_n_rates; ++ri) {
                    double rate = k_rates[ri];
                    double nyq  = rate * 0.5;

                    // Valid fc1 at 10%, 50%, 90% of Nyquist
                    for (double fc1_frac : {0.1, 0.5, 0.9}) {
                        double fc1 = nyq * fc1_frac;
                        double fc2 = std::min(fc1 * 2.0, nyq * 0.99);

                        probe(fam, pb, order, rate, fc1, fc2, -1.0, 1.0, 6.0);
                        ++pass;
                    }

                    // Edge: fc1 exactly at nyq (should error gracefully, not crash)
                    probe(fam, pb, order, rate, nyq,       nyq * 1.5, -1.0, 1.0, 6.0);
                    // Edge: fc1 > nyq
                    probe(fam, pb, order, rate, nyq * 1.5, nyq * 2.0, -1.0, 1.0, 6.0);
                    // Edge: fc2 <= fc1 for BP/BS
                    probe(fam, pb, order, rate, nyq * 0.5, nyq * 0.3, -1.0, 1.0, 6.0);
                    // Edge: fc1 = 0
                    probe(fam, pb, order, rate, 0.0, nyq * 0.5, -1.0, 1.0, 6.0);
                    pass += 4;
                }
            }
        }
    }
    std::printf("  systematic: %d probes — OK\n", pass);

    // ── Phase 2: random storm ─────────────────────────────────────────────
    std::printf("Phase 2: random storm (10000 iterations)\n");

    std::uniform_int_distribution<int> pick_fam(0, 9);
    std::uniform_int_distribution<int> pick_pb(0, 3);
    std::uniform_real_distribution<double> pick_rate(50.0, 250000.0);
    std::uniform_real_distribution<double> pick_frac(0.0, 1.2);  // deliberately exceed nyq
    std::uniform_real_distribution<double> pick_ripple(-60.0, -0.001);
    std::uniform_real_distribution<double> pick_q(0.001, 200.0);
    std::uniform_real_distribution<double> pick_gain(-60.0, 60.0);
    std::uniform_int_distribution<int> pick_order_gen(1, 20);

    int random_pass = 0;
    for (int i = 0; i < 10000; ++i) {
        FilterFamily fam = k_families[pick_fam(rng)];
        FilterPassband pb = k_passbands[pick_pb(rng)];
        int max_ord = (fam == FilterFamily::Bessel) ? 10 : 20;
        std::uniform_int_distribution<int> pick_order(1, max_ord);
        int order = pick_order(rng);

        double rate = pick_rate(rng);
        double nyq  = rate * 0.5;
        double fc1  = nyq * pick_frac(rng);
        double fc2  = nyq * pick_frac(rng);

        probe(fam, pb, order, rate, fc1, fc2,
              pick_ripple(rng), pick_q(rng), pick_gain(rng));
        ++random_pass;

        if ((i + 1) % 1000 == 0)
            std::printf("  %d / 10000\n", i + 1);
    }
    std::printf("  random: %d probes — OK\n", random_pass);

    // ── Phase 3: previously crashing combinations ─────────────────────────
    std::printf("Phase 3: known crash repros\n");

    // 8k -> 250: rate 8000 then 250, fc1=1000 (above nyq=125)
    probe(FilterFamily::Butterworth, FilterPassband::LP, 4,  8000.0, 1000.0, 2000.0, -1.0, 1.0, 6.0);
    probe(FilterFamily::Butterworth, FilterPassband::LP, 4,   250.0, 1000.0, 2000.0, -1.0, 1.0, 6.0);
    // Butterworth BP order 20, fc1 above fc2
    probe(FilterFamily::Butterworth, FilterPassband::BP, 20, 44100.0, 2500.0,  500.0, -1.0, 1.0, 6.0);
    // Bessel BP order 11 (above max)
    probe(FilterFamily::Bessel,      FilterPassband::BP, 11, 44100.0,  500.0, 2000.0, -1.0, 1.0, 6.0);
    // Bessel BP order 20
    probe(FilterFamily::Bessel,      FilterPassband::BP, 20, 44100.0,  500.0, 2000.0, -1.0, 1.0, 6.0);
    std::printf("  repros: OK\n");

    std::printf("\nPASS  total=%d probes survived without crash\n", g_total);
    return 0;
}
