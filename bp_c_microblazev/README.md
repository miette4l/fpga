# Bare-metal BP decoder for MicroBlaze V
 
A plain C version of the min-sum BP decoder in `bp_cpp`, with no operating
system, no files, no heap and no C++ library. The parity check matrix is
compiled in from a header.
 
The decoder uses the same serial (layered) schedule as `BP_Decoder::run_bp()`:
one sweep over checks per iteration, in CSR order, each check reading and
updating the shared bit beliefs in place so later checks in the same sweep
already see earlier checks' new messages. This is not the two-phase flooding
schedule most textbook BP write-ups show.
 
## Files
 
| File | What it is |
|---|---|
| `bp.c`, `bp.h` | The decoder. Same arithmetic, in the same order (one serial sweep over checks per iteration), as `BP_Decoder` in `bp_cpp`. |
| `main.c` | Decodes once, prints the result and a checksum, then times repeated decodes. |
| `pcm_data.h` | Generated. Holds the matrix, priors and syndrome. Ships with the [7,4,3] Hamming test case. |
| `make_header.py` | Generates `pcm_data.h` from `H_matrix.txt` and `syndrome.txt`. |
 
## 1. Check it on your laptop first
 
```
gcc -O2 -DBP_HOST -o bp_host main.c bp.c
./bp_host
```
 
With the Hamming example that ships in `pcm_data.h` you should see:
 
```
converged: 1
iterations: 1
error weight: 1
first 7 e_BP values: 0 1 0 0 0 0 0
first 7 final LLRs: 4.394 0.000 2.197 4.394 4.394 6.592 4.394
checksum: 0x9553fe29
```
 
These are the values `tests/bp_test.cpp` expects for `Hamming743`.
 
## 2. Put your own code in
 
Point the script at your test data (paths here assume `bp_cpp` sits next to
this folder):
 
```
python3 make_header.py --H ../bp_cpp/tests/rep120/H_matrix.txt \
                       --syndrome ../bp_cpp/tests/rep120/syndrome.txt \
                       --p 0.1 --max-iter 10
```
 
It writes `pcm_data.h` and prints how much memory the decoder data needs.
Rebuild `bp_host`, run it, and note the `checksum` line.
 
Other options: `--p-file` (one probability per bit), `--llr-file` (one channel
LLR per bit), `--example hamming`, `-o` (output path).
 
## 3. Run it on the MicroBlaze V
 
1. In Vitis, create the platform from your `.xsa`, then an empty C application
   on it. If you start from the Hello World template, delete its `helloworld.c`.
2. Copy `bp.c`, `bp.h`, `main.c` and `pcm_data.h` into the application's `src`
   folder. No compiler flags are needed.
3. Build and run. Output goes to the UART.
The `checksum` line on the board should match the one from your laptop. It
covers every output bit, so a match means the board computed exactly the same
result. Only the `timing` lines are expected to differ.
 
## Memory
 
With 16-bit indices (any code up to 65,535 edges) the decoder data needs about
6 bytes per edge, 9 bytes per bit and 3 bytes per check. The decoder program
adds about 3 KB. (The serial schedule only ever walks checks in CSR order, so
unlike a flooding decoder there is no column/bit view to store, and only one
working array per edge instead of two.) `make_header.py` prints the exact
figure for your code, so set the MicroBlaze V local memory size from that
with room to spare for the Vitis startup code and print routines.
 
## Timing
 
Time is read from the RISC-V `time` counter, which on MicroBlaze V counts
processor clock cycles. No timer block is needed in the block design. Cycles
are converted to microseconds using `XPAR_CPU_CORE_CLOCK_FREQ_HZ`; if your
Vitis version does not define that, add `-DBP_CPU_HZ=<your clock in Hz>`.
`BP_TIMING_RUNS` (default 10) sets how many decodes are averaged.
 
## Floating point
 
The decoder uses single-precision `float`. Enable floating point in the
MicroBlaze V settings, or every operation goes through slow software routines.
Results are the same either way.
 
## What has and has not been tested
 
`bp.c` was rewritten to use the serial schedule above; it previously used a
two-phase flooding schedule, which is a different algorithm and does not
reproduce `bp_cpp`'s numbers in general. Re-verify anything below that was
only checked against the old flooding version.
 
Tested on Linux, against this serial version:
 
- `Hamming743` and `RepCode120` from `tests/bp_test.cpp` (built and run from
  `bp_cpp`): final LLRs matched the C++ decoder's expected values exactly,
  including across `RepCode120`'s two BP iterations.
 
Not yet re-tested since the schedule changed: larger codes (random sparse
matrices, the [[144,12,12]] bivariate bicycle code, a matrix with 69,000
edges), the board build (the `xil_printf` and `rdtime` path) on an emulated
RISC-V processor, a real MicroBlaze V, and the Vitis build against AMD's real
headers. Re-run the full `tests/bp_test.cpp` suite against `bp.c` before
relying on a code larger than the two above.
 
Known differences from the C++:
 
- `--p` and `--p-file` convert probabilities to LLRs in the script. For
  p = 0.1 this is identical to the C++. For about 0.3% of arbitrary
  probabilities it differs in the last bit of a float, because maths
  libraries round `log` slightly differently. Hard decisions were unaffected
  in every case tested. Use `--llr-file` if you need exact control.
- `bp.c` does all BP arithmetic in `float`; `bp_cpp`'s decoder accumulates
  internally in `double`. The two test cases above matched to float
  precision, but exact bit-for-bit agreement with the C++ isn't guaranteed
  for every matrix and syndrome.