# Disclaimer

## No Warranty

This software is provided **"as is"**, without warranty of any kind, express or implied,
including but not limited to the warranties of merchantability, fitness for a particular
purpose, and non-infringement. In no event shall the authors or copyright holders be
liable for any claim, damages, or other liability, whether in an action of contract, tort,
or otherwise, arising from, out of, or in connection with the software or the use or other
dealings in the software.

## Limitation of Liability

TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, IN NO EVENT SHALL THE
AUTHORS, COPYRIGHT HOLDERS, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES WHATSOEVER, INCLUDING
BUT NOT LIMITED TO:

- Loss of profits or revenue
- Loss of data or data corruption
- Loss of business or contracts
- Business interruption
- Procurement of substitute goods or services
- Personal injury or property damage
- Any other pecuniary or non-pecuniary loss

WHETHER ARISING IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE), OR
ANY OTHER LEGAL THEORY, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE POSSIBILITY
OF SUCH DAMAGES, AND EVEN IF A REMEDY SET FORTH HEREIN IS FOUND TO HAVE FAILED
OF ITS ESSENTIAL PURPOSE.

**Note for users in the European Union and other jurisdictions with mandatory
consumer protection laws:** Nothing in this disclaimer limits or excludes
liability that cannot be excluded or limited under applicable mandatory law,
including liability for death or personal injury caused by gross negligence or
wilful misconduct, or liability under applicable product liability legislation.
The above limitations apply only to the extent permitted by the law of the
applicable jurisdiction.

---

## Safety-Critical Applications

**This software is not certified for use in safety-critical systems.**

Filter coefficients, frequency responses, and generated source code produced by this
software are provided for informational and engineering-reference purposes only. They have
**not** been validated for use in:

- Medical devices or diagnostic equipment
- Aerospace, aviation, or navigation systems
- Automotive safety systems
- Industrial control systems where failure could result in injury or loss of life
- Any other application where software failure could have safety consequences

Users are solely responsible for independently verifying the correctness of any filter
design before deployment in a production system. The mathematical results produced by this
software must be validated against independent references and applicable standards.

## Accuracy of Generated Code

The filter coefficients and generated source code (C99, C++20, Rust, Python, MATLAB,
Julia, Verilog, SystemVerilog) are derived from the fidlib filter design library and
reflect its numerical precision. Floating-point arithmetic, quantisation effects, and
fixed-point representations (FPGA output) may introduce rounding errors. Always verify
generated filters against simulation data before use.

## Third-Party Components

This software incorporates third-party components under their respective licences:

| Component | Author / Origin | Licence |
|---|---|---|
| fidlib | Jim Peters, uazu.net | LGPL-2.1-or-later |
| fiview | Jim Peters, uazu.net | GPL-2.0 |
| firun | Jim Peters, uazu.net | GPL-2.0 |
| mkfilter | Dr. A.J. Fisher, University of York | see `vendor/mkfilter/` |
| Dear ImGui | Omar Cornut | MIT |
| GLFW | Various contributors | zlib |
| nlohmann/json | Niels Lohmann | MIT |
| Eigen3 | Various contributors | MPL-2.0 |
| KissFFT | Mark Borgerding | BSD-3-Clause |
| imtile | Jörg Simbrig | GPL-2.0-or-later |

The original fidlib, fiview, and firun source code is copyright © 2002–2004 Jim Peters
and is used here under the terms of its respective licences. All modifications and
extensions are copyright © 2025–2026 Jörg Simbrig and are released under the same licence
terms as the corresponding original component.

## Licence Summary

| Component | Licence |
|---|---|
| fidlib core and extensions | LGPL-2.1-or-later |
| fiview, firun, fidgen, fiview2 | GPL-2.0-or-later |
| Documentation and manuals | CC BY-SA 4.0 |

Full licence texts are provided in `COPYING` (GPL-2.0) and `COPYING_LIB` (LGPL-2.1).
See also `LICENSE.md` for a component-by-component overview.

## Trademark Notice

"fidlib" and "fiview" are names originating with Jim Peters. This project is an
independent modernisation and is not affiliated with or endorsed by the original author.
