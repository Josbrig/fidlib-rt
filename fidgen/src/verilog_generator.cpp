// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>

#include <fidgen/generator.hpp>
#include <fidgen/version.hpp>

#include <cmath>
#include <ostream>
#include <sstream>
#include <string>

namespace fidgen {

// ─────────────────────────────────────────────────────────────────────────────
// Verilog / SystemVerilog FPGA generator
//
// Fixed-point representation:
//   data_bits   = fpga_bits  (default 24)
//   frac_bits   = fpga_frac  (default: auto from coefficient range)
//   int_bits    = data_bits - frac_bits - 1  (sign bit)
//
// Coefficient quantization:
//   coef_int    = round(coef_double * 2^frac_bits)  → clipped to [-2^(data_bits-1), 2^(data_bits-1)-1]
//
// Architecture: pipeline of biquad stages, latency = n_stages clock cycles.
// ─────────────────────────────────────────────────────────────────────────────

static int auto_frac_bits(const FilterDescriptor& f, int total_bits)
{
    double max_abs = std::fabs(f.gain());
    for (const auto& s : f.stages()) {
        for (double v : s.b) max_abs = std::max(max_abs, std::fabs(v));
        for (double v : s.a) max_abs = std::max(max_abs, std::fabs(v));
    }
    if (max_abs < 1e-15) max_abs = 1.0;
    // int bits needed to represent max_abs (sign + ceil(log2(max_abs+1)))
    int int_part = (max_abs >= 1.0)
        ? static_cast<int>(std::ceil(std::log2(max_abs + 1.0))) + 1  // +1 sign
        : 2;  // at least s.0
    int frac = total_bits - int_part;
    if (frac < 4)  frac = 4;
    if (frac >= total_bits) frac = total_bits - 2;
    return frac;
}

static long long quantize(double v, int frac_bits, int total_bits)
{
    double scale = std::ldexp(1.0, frac_bits);
    long long q = static_cast<long long>(std::round(v * scale));
    long long max_val = (1LL << (total_bits - 1)) - 1;
    long long min_val = -(1LL << (total_bits - 1));
    if (q > max_val) q = max_val;
    if (q < min_val) q = min_val;
    return q;
}

// ─────────────────────────────────────────────────────────────────────────────
// Testbench helper — appended when opts.with_test is set
static void emit_verilog_testbench(std::ostream& out, const std::string& fn,
                                    int W, int frac, int latency, bool is_sv)
{
    const int n_sim = 256 + latency;
    const long long impulse_val = 1LL << frac;
    const char* signed_kw = is_sv ? "logic" : "reg";
    (void)signed_kw;

    out << "\n// ── Testbench (compile with -DSIMULATION or `ifdef SIMULATION) ───────\n"
        << "`ifdef SIMULATION\n"
        << "`timescale 1ns/1ps\n\n"
        << "module " << fn << "_tb;\n"
        << "    reg clk = 0;\n"
        << "    reg rst_n = 0;\n"
        << "    reg  signed [" << (W-1) << ":0] x_in = 0;\n"
        << "    wire signed [" << (W-1) << ":0] y_out;\n\n"
        << "    " << fn << " dut (.clk(clk), .rst_n(rst_n),\n"
        << "               .x_in(x_in), .y_out(y_out));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    integer i;\n"
        << "    initial begin\n"
        << "        rst_n = 0; x_in = 0;\n"
        << "        @(posedge clk); #1;\n"
        << "        rst_n = 1;\n"
        << "        // Impulse: 1.0 in Q" << (W-frac-1) << "." << frac << " = " << impulse_val << "\n"
        << "        x_in = " << W << "'sd" << impulse_val << ";\n"
        << "        @(posedge clk); #1;\n"
        << "        x_in = 0;\n"
        << "        for (i = 0; i < " << n_sim << "; i = i + 1) begin\n"
        << "            @(posedge clk); #1;\n"
        << "            if (i >= " << latency << ") $display(\"%d\", y_out);\n"
        << "        end\n"
        << "        $finish;\n"
        << "    end\n"
        << "endmodule\n"
        << "`endif // SIMULATION\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared emit logic (free function used by both Verilog and SV generators)
static void emit_verilog_impl(std::ostream& out, const FilterDescriptor& f,
                               const GeneratorOptions& opts, bool is_sv);

class VerilogGenerator final : public Generator {
public:
    void generate(std::ostream& out,
                  const FilterDescriptor& f,
                  const GeneratorOptions& opts) const override;
};

static int fir_auto_frac_bits(const FilterDescriptor& f, int total_bits)
{
    double max_abs = std::fabs(f.gain());
    for (double v : f.taps()) max_abs = std::max(max_abs, std::fabs(v));
    if (max_abs < 1e-15) max_abs = 1.0;
    int int_part = (max_abs >= 1.0)
        ? static_cast<int>(std::ceil(std::log2(max_abs + 1.0))) + 1
        : 2;
    int fr = total_bits - int_part;
    if (fr < 4) fr = 4;
    if (fr >= total_bits) fr = total_bits - 2;
    return fr;
}

static void emit_verilog_fir(std::ostream& out, const FilterDescriptor& f,
                              const GeneratorOptions& opts, bool is_sv)
{
    const std::string fn  = f.func_name();
    const int N   = f.n_taps();
    const int D   = N - 1;
    const int W   = opts.fpga_bits;
    const int frac = (opts.fpga_frac < 0) ? fir_auto_frac_bits(f, W) : opts.fpga_frac;
    const int acc  = W * 2 + 2;
    const char* lang = is_sv ? "SystemVerilog" : "Verilog";
    const char* guard_sfx = is_sv ? "_SV" : "_V";

    out << "// SPDX-License-Identifier: GPL-2.0-or-later\n"
        << "// Generated by fidgen " << k_version << " (" << lang << ")\n"
        << "//\n"
        << "// Filter : " << fn << " (FIR, " << N << " taps)\n"
        << "// Spec   : " << f.spec() << "\n"
        << "// Rate   : " << f.rate() << " Hz\n"
        << "// Width  : " << W << " bits, " << frac << " fractional bits\n\n";

    out << "`ifndef " << to_upper(fn) << guard_sfx << "_V\n"
        << "`define " << to_upper(fn) << guard_sfx << "_V\n\n";

    out << "// Quantized FIR taps (Q" << (W - frac - 1) << "." << frac << ")\n";
    auto emit_tap = [&](const std::string& name, double v) {
        long long q = quantize(v, frac, W);
        out << "`define " << to_upper(fn) << "_" << name
            << " " << W << "'sd" << q << "\n";
    };
    for (int k = 0; k < N; ++k)
        emit_tap("H_" + std::to_string(k), f.taps()[static_cast<std::size_t>(k)]);
    emit_tap("GAIN", f.gain());
    out << "\n";

    // Module declaration
    if (is_sv) {
        out << "module " << fn << " (\n"
            << "    input  logic                clk,\n"
            << "    input  logic                rst_n,\n"
            << "    input  logic signed [" << (W-1) << ":0] x_in,\n"
            << "    output logic signed [" << (W-1) << ":0] y_out\n"
            << ");\n\n";
    } else {
        out << "module " << fn << " (\n"
            << "    input  wire                 clk,\n"
            << "    input  wire                 rst_n,\n"
            << "    input  wire signed [" << (W-1) << ":0] x_in,\n"
            << "    output reg  signed [" << (W-1) << ":0] y_out\n"
            << ");\n\n";
    }

    // Delay line as array
    if (D > 0) {
        if (is_sv)
            out << "logic signed [" << (W-1) << ":0] dl [0:" << (D-1) << "];\n\n";
        else
            out << "reg   signed [" << (W-1) << ":0] dl [0:" << (D-1) << "];\n\n";
    }

    // Combinational MAC: unrolled tap products
    out << "// Unrolled MAC (tap products)\n";
    for (int k = 0; k < N; ++k) {
        if (is_sv)
            out << "logic signed [" << (acc-1) << ":0] tap_" << k << ";\n";
        else
            out << "wire  signed [" << (acc-1) << ":0] tap_" << k << ";\n";
    }
    if (is_sv)
        out << "logic signed [" << (acc-1) << ":0] mac_sum;\n\n";
    else
        out << "wire  signed [" << (acc-1) << ":0] mac_sum;\n\n";

    out << "assign tap_0 = $signed(`" << to_upper(fn) << "_H_0) * $signed(x_in);\n";
    for (int k = 1; k < N; ++k) {
        out << "assign tap_" << k << " = $signed(`" << to_upper(fn)
            << "_H_" << k << ") * $signed(dl[" << (k-1) << "]);\n";
    }
    out << "assign mac_sum = (tap_0";
    for (int k = 1; k < N; ++k)
        out << "\n                + tap_" << k;
    out << ") >>> " << frac << ";\n\n";

    // Sequential
    out << "always @(posedge clk or negedge rst_n) begin\n"
        << "    if (!rst_n) begin\n";
    if (D > 0)
        out << "        integer i; for (i = 0; i < " << D << "; i = i+1) dl[i] <= " << W << "'sd0;\n";
    out << "        y_out <= " << W << "'sd0;\n"
        << "    end else begin\n";
    if (D > 0) {
        out << "        integer j;\n"
            << "        for (j = " << (D-1) << "; j > 0; j = j-1) dl[j] <= dl[j-1];\n"
            << "        dl[0] <= x_in;\n";
    }
    out << "        y_out <= ($signed(`" << to_upper(fn) << "_GAIN) * $signed(mac_sum[" << (W-1) << ":0])) >>> " << frac << ";\n"
        << "    end\n"
        << "end\n\n"
        << "endmodule\n\n"
        << "`endif // " << to_upper(fn) << guard_sfx << "_V\n";

    if (opts.with_test)
        emit_verilog_testbench(out, fn, W, frac, /*latency=*/1, is_sv);
}

static void emit_verilog_impl(std::ostream& out, const FilterDescriptor& f,
                               const GeneratorOptions& opts, bool is_sv)
{
    if (f.is_fir()) { emit_verilog_fir(out, f, opts, is_sv); return; }

    const std::string fn = f.func_name();
    const int n   = f.n_stages();
    const int W   = opts.fpga_bits;
    const int frac = (opts.fpga_frac < 0) ? auto_frac_bits(f, W) : opts.fpga_frac;
    const int acc  = W * 2 + 2;   // accumulator width (no overflow on product)

    const char* lang = is_sv ? "SystemVerilog" : "Verilog";
    const char* guard_sfx = is_sv ? "_SV" : "_V";

    out << "// SPDX-License-Identifier: GPL-2.0-or-later\n"
        << "// Generated by fidgen " << k_version << " (" << lang << ")\n"
        << "//\n"
        << "// Filter : " << fn << "\n"
        << "// Spec   : " << f.spec() << "\n"
        << "// Rate   : " << f.rate() << " Hz\n"
        << "// Stages : " << n << "\n"
        << "// Gain   : " << fmt_double(f.gain()) << "\n"
        << "// Width  : " << W << " bits, " << frac << " fractional bits\n"
        << "//\n"
        << "// Pipeline: 1 clock per biquad, latency = " << n << " cycle(s)\n\n";

    out << "`ifndef " << to_upper(fn) << guard_sfx << "_V\n"
        << "`define " << to_upper(fn) << guard_sfx << "_V\n\n";

    // Emit quantized coefficient `define macros
    out << "// Quantized coefficients (Q" << (W - frac - 1) << "." << frac << ")\n";
    auto emit_coef = [&](const std::string& name, double v) {
        long long q = quantize(v, frac, W);
        out << "`define " << to_upper(fn) << "_" << name
            << " " << W << "'sd" << q << "\n";
    };

    emit_coef("GAIN", f.gain());
    for (int i = 0; i < n; ++i) {
        const auto& s = f.stages()[static_cast<std::size_t>(i)];
        std::string pfx = "S" + std::to_string(i) + "_";
        emit_coef(pfx + "B0", s.b[0]);
        emit_coef(pfx + "B1", s.b[1]);
        emit_coef(pfx + "B2", s.b[2]);
        emit_coef(pfx + "A1", s.a[1]);
        emit_coef(pfx + "A2", s.a[2]);
    }
    out << "\n";

    // Module declaration
    if (is_sv) {
        out << "module " << fn << " (\n"
            << "    input  logic                clk,\n"
            << "    input  logic                rst_n,\n"
            << "    input  logic signed [" << (W-1) << ":0] x_in,\n"
            << "    output logic signed [" << (W-1) << ":0] y_out\n"
            << ");\n\n";
    } else {
        out << "module " << fn << " (\n"
            << "    input  wire                 clk,\n"
            << "    input  wire                 rst_n,\n"
            << "    input  wire signed [" << (W-1) << ":0] x_in,\n"
            << "    output reg  signed [" << (W-1) << ":0] y_out\n"
            << ");\n\n";
    }

    // Pipeline stage registers
    for (int i = 0; i < n; ++i) {
        out << "// Stage " << i << " delay registers\n";
        if (is_sv) {
            out << "logic signed [" << (W-1) << ":0] s" << i << "_w0, s" << i << "_w1;\n";
        } else {
            out << "reg   signed [" << (W-1) << ":0] s" << i << "_w0, s" << i << "_w1;\n";
        }
    }
    out << "\n";

    // Pipeline wires for w and y per stage
    for (int i = 0; i < n; ++i) {
        if (is_sv) {
            out << "logic signed [" << (acc-1) << ":0] w" << i << "_acc, y" << i << "_acc;\n"
                << "logic signed [" << (W-1) << ":0]  w" << i << ", y" << i << ";\n";
        } else {
            out << "wire  signed [" << (acc-1) << ":0] w" << i << "_acc, y" << i << "_acc;\n"
                << "wire  signed [" << (W-1) << ":0]  w" << i << ", y" << i << ";\n";
        }
    }
    out << "\n";

    // Combinational assignments for each stage
    for (int i = 0; i < n; ++i) {
        const std::string inp = (i == 0) ? "x_in" : ("y" + std::to_string(i-1));
        out << "// Stage " << i << " computation\n"
            << "assign w" << i << "_acc = ($signed(" << (acc) << "'({{" << (acc-W) << "{" << inp << "[" << (W-1) << "]}}, " << inp << "})) -\n"
            << "    (($signed(`" << to_upper(fn) << "_S" << i << "_A1) * $signed(s" << i << "_w0)) >>> " << frac << ") -\n"
            << "    (($signed(`" << to_upper(fn) << "_S" << i << "_A2) * $signed(s" << i << "_w1)) >>> " << frac << ");\n"
            << "assign w" << i << " = w" << i << "_acc[" << (W-1) << ":0];\n\n"
            << "assign y" << i << "_acc = (($signed(`" << to_upper(fn) << "_S" << i << "_B0) * $signed(w" << i << ")) >>> " << frac << ") +\n"
            << "    (($signed(`" << to_upper(fn) << "_S" << i << "_B1) * $signed(s" << i << "_w0)) >>> " << frac << ") +\n"
            << "    (($signed(`" << to_upper(fn) << "_S" << i << "_B2) * $signed(s" << i << "_w1)) >>> " << frac << ");\n"
            << "assign y" << i << " = y" << i << "_acc[" << (W-1) << ":0];\n\n";
    }

    // Sequential: clock registers + reset
    out << "always @(posedge clk or negedge rst_n) begin\n"
        << "    if (!rst_n) begin\n";
    for (int i = 0; i < n; ++i) {
        out << "        s" << i << "_w0 <= " << W << "'sd0;\n"
            << "        s" << i << "_w1 <= " << W << "'sd0;\n";
    }
    out << "        y_out <= " << W << "'sd0;\n"
        << "    end else begin\n";
    for (int i = 0; i < n; ++i) {
        out << "        s" << i << "_w1 <= s" << i << "_w0;\n"
            << "        s" << i << "_w0 <= w" << i << ";\n";
    }
    if (n > 0)
        out << "        y_out <= (($signed(`" << to_upper(fn) << "_GAIN) * $signed(y" << (n-1) << ")) >>> " << frac << ");\n";
    else
        out << "        y_out <= (($signed(`" << to_upper(fn) << "_GAIN) * $signed(x_in)) >>> " << frac << ");\n";
    out << "    end\n"
        << "end\n\n"
        << "endmodule\n\n"
        << "`endif // " << to_upper(fn) << guard_sfx << "_V\n";

    if (opts.with_test)
        emit_verilog_testbench(out, fn, W, frac, /*latency=*/n, is_sv);
}

void VerilogGenerator::generate(std::ostream& out,
                                 const FilterDescriptor& f,
                                 const GeneratorOptions& opts) const
{
    emit_verilog_impl(out, f, opts, false);
}

// ─────────────────────────────────────────────────────────────────────────────
class SystemVerilogGenerator final : public Generator {
public:
    void generate(std::ostream& out,
                  const FilterDescriptor& f,
                  const GeneratorOptions& opts) const override
    {
        emit_verilog_impl(out, f, opts, true);
    }
};

// ── registration ─────────────────────────────────────────────────────────────
std::unique_ptr<Generator> make_verilog_generator() {
    return std::make_unique<VerilogGenerator>();
}

std::unique_ptr<Generator> make_systemverilog_generator() {
    return std::make_unique<SystemVerilogGenerator>();
}

} // namespace fidgen
