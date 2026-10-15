#!/usr/bin/env python3
"""Turn a parity check matrix and syndrome into pcm_data.h for the bare-metal
BP decoder.
 
Input files use the same text format as the bp_cpp tests:
 
  H_matrix.txt   one row per line, entries 0 or 1 separated by spaces
  syndrome.txt   0/1 values separated by whitespace, one per check
 
Typical use:
 
  python3 make_header.py --H tests/rep120/H_matrix.txt \
                         --syndrome tests/rep120/syndrome.txt --p 0.1
 
  python3 make_header.py --example hamming      # the built-in 7-bit example
 
Only the Python standard library is needed.
"""
import argparse
import math
import struct
import sys
 
 
def f32(x):
    """Round a Python float to the nearest single-precision value."""
    return struct.unpack("<f", struct.pack("<f", x))[0]
 
 
def llr_from_p(p):
    """log((1-p)/p) evaluated in single precision, as bp_cpp does."""
    p32 = f32(p)
    one_minus_p = f32(1.0 - p32)      # each step rounded to single precision,
    ratio = f32(one_minus_p / p32)    # exactly as the C++ float arithmetic does
    return f32(math.log(ratio))
 
 
def read_matrix(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            if line.strip():
                rows.append([int(tok) for tok in line.split()])
    return rows
 
 
def read_numbers(path, kind):
    with open(path) as fh:
        return [kind(tok) for tok in fh.read().split()]
 
 
def build_sparse(H):
    """Same construction and edge ordering as create_sparse_pcm() in pcm.cpp."""
    num_checks = len(H)
    num_bits = len(H[0])
 
    csr_ptr = [0]
    csr_idx = []
    edge_of = {}                      # (check, bit) -> CSR edge index
    for i in range(num_checks):
        for j in range(num_bits):
            if H[i][j] != 0:
                edge_of[(i, j)] = len(csr_idx)
                csr_idx.append(j)
        csr_ptr.append(len(csr_idx))
 
    by_column = [[] for _ in range(num_bits)]
    for (i, j) in sorted(edge_of):    # sorted by check, so each column is in check order
        by_column[j].append(edge_of[(i, j)])
 
    csc_ptr = [0]
    csc_edge_idx = []
    for j in range(num_bits):
        csc_edge_idx.extend(by_column[j])
        csc_ptr.append(len(csc_edge_idx))
 
    return csr_ptr, csr_idx, csc_ptr, csc_edge_idx
 
 
def c_array(ctype, name, values, fmt, per_line):
    out = ["static const %s %s[%d] = {" % (ctype, name, len(values))]
    for k in range(0, len(values), per_line):
        chunk = values[k:k + per_line]
        out.append("    " + ", ".join(fmt(v) for v in chunk) + ",")
    out.append("};")
    return "\n".join(out)
 
 
def fmt_float(v):
    if math.isinf(v) or math.isnan(v):
        sys.exit("error: channel LLR is not finite (is a probability 0 or 1?)")
    text = "%.9g" % v                 # 9 significant digits round-trips a float exactly
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"
 
 
EXAMPLES = {
    # [7,4,3] Hamming code, the Hamming743 case from tests/bp_test.cpp
    "hamming": {
        "H": [
            [1, 0, 0, 0, 1, 1, 0],
            [0, 1, 0, 0, 1, 0, 1],
            [0, 0, 1, 0, 0, 1, 1],
            [0, 0, 0, 1, 1, 1, 1],
        ],
        "syndrome": [0, 1, 0, 0],
        "p": 0.1,
        "max_iter": 10,
    },
}
 
 
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--H", help="parity check matrix text file")
    ap.add_argument("--syndrome", help="syndrome text file")
    ap.add_argument("--p", type=float, default=None,
                    help="error probability used for every bit (default 0.1)")
    ap.add_argument("--p-file", help="text file with one error probability per bit")
    ap.add_argument("--llr-file", help="text file with one channel LLR per bit "
                                       "(use instead of --p / --p-file)")
    ap.add_argument("--max-iter", type=int, default=None,
                    help="iteration limit compiled into the header (default 10)")
    ap.add_argument("--example", choices=sorted(EXAMPLES),
                    help="use a built-in example instead of files")
    ap.add_argument("--name", default=None, help="label printed by the program")
    ap.add_argument("-o", "--output", default="pcm_data.h")
    args = ap.parse_args()
 
    if args.example:
        ex = EXAMPLES[args.example]
        H, syndrome = ex["H"], ex["syndrome"]
        name = args.name or args.example
        default_p, default_iter = ex["p"], ex["max_iter"]
    else:
        if not args.H or not args.syndrome:
            ap.error("give --H and --syndrome, or --example")
        H = read_matrix(args.H)
        syndrome = read_numbers(args.syndrome, int)
        name = args.name or args.H
        default_p, default_iter = 0.1, 10
 
    max_iter = args.max_iter if args.max_iter is not None else default_iter
 
    # ---- checks on the inputs -------------------------------------------
    if not H or not H[0]:
        sys.exit("error: the matrix is empty")
    num_checks, num_bits = len(H), len(H[0])
    for r, row in enumerate(H):
        if len(row) != num_bits:
            sys.exit("error: row %d has %d entries, expected %d" % (r, len(row), num_bits))
    if len(syndrome) != num_checks:
        sys.exit("error: syndrome has %d entries but H has %d rows"
                 % (len(syndrome), num_checks))
    if any(s not in (0, 1) for s in syndrome):
        sys.exit("error: syndrome entries must be 0 or 1")
    if max_iter < 1:
        sys.exit("error: --max-iter must be at least 1")
 
    given = [x is not None for x in (args.p, args.p_file, args.llr_file)]
    if sum(given) > 1:
        sys.exit("error: give only one of --p, --p-file, --llr-file")
    if args.llr_file:
        llr = [f32(v) for v in read_numbers(args.llr_file, float)]
        prior_note = "from " + args.llr_file
    elif args.p_file:
        probs = read_numbers(args.p_file, float)
        if any(not (0.0 < q < 1.0) for q in probs):
            sys.exit("error: probabilities must be strictly between 0 and 1")
        llr = [llr_from_p(q) for q in probs]
        prior_note = "from probabilities in " + args.p_file
    else:
        p = args.p if args.p is not None else default_p
        if not (0.0 < p < 1.0):
            sys.exit("error: --p must be strictly between 0 and 1")
        llr = [llr_from_p(p)] * num_bits
        prior_note = "p = %g for every bit" % p
    if len(llr) != num_bits:
        sys.exit("error: %d prior values given but H has %d columns" % (len(llr), num_bits))
 
    csr_ptr, csr_idx, csc_ptr, csc_edge_idx = build_sparse(H)
    num_edges = len(csr_idx)
    if num_edges == 0:
        sys.exit("error: the matrix has no non-zero entries")
 
    row_weights = [csr_ptr[i + 1] - csr_ptr[i] for i in range(num_checks)]
    col_weights = [csc_ptr[j + 1] - csc_ptr[j] for j in range(num_bits)]
 
    # Smallest index type that fits, to save memory on the soft-core.
    largest = max(num_edges, num_bits, num_checks)
    idx_type, idx_bytes = ("uint16_t", 2) if largest <= 0xFFFF else ("uint32_t", 4)
 
    # ---- memory estimate -------------------------------------------------
    # Tables: CSR_ptr, CSR_idx, channel LLR, syndrome. (No CSC table: the
    # serial checkwise decoder only ever walks checks in CSR order.)
    # Working arrays: one check-to-bit message per edge (msgs[]), one
    # running LLR per bit (bp_llrs[]), one hard decision per bit (bp_error[]).
    const_bytes = idx_bytes * ((num_checks + 1) + num_edges) + 4 * num_bits + num_checks
    ram_bytes = 4 * num_edges + 4 * num_bits + num_bits
 
    safe_name = name.replace("\\", "/").replace('"', "'")
    lines = []
    lines.append("/*")
    lines.append(" * pcm_data.h - generated by make_header.py. Do not edit by hand;")
    lines.append(" * re-run the script to change the code, syndrome or priors.")
    lines.append(" *")
    lines.append(" *   source  : %s" % safe_name)
    lines.append(" *   size    : %d checks x %d bits, %d edges" % (num_checks, num_bits, num_edges))
    lines.append(" *   priors  : %s" % prior_note)
    lines.append(" *   memory  : about %d bytes of tables + %d bytes of working arrays"
                 % (const_bytes, ram_bytes))
    lines.append(" */")
    lines.append("#ifndef PCM_DATA_H")
    lines.append("#define PCM_DATA_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append('#define PCM_NAME        "%s"' % safe_name)
    lines.append("#define PCM_NUM_CHECKS  %d" % num_checks)
    lines.append("#define PCM_NUM_BITS    %d" % num_bits)
    lines.append("#define PCM_NUM_EDGES   %d" % num_edges)
    lines.append("#define PCM_MAX_ITER    %d" % max_iter)
    lines.append("")
    lines.append("typedef %s pcm_idx_t;" % idx_type)
    lines.append("")
    lines.append("/* Row (check) view: edges of check c are csr_ptr[c] .. csr_ptr[c+1]-1,")
    lines.append(" * and csr_idx[edge] is the bit that edge connects to. The decoder sweeps")
    lines.append(" * checks in this order only; no column (bit) view is needed. */")
    lines.append(c_array("pcm_idx_t", "pcm_csr_ptr", csr_ptr, str, 16))
    lines.append(c_array("pcm_idx_t", "pcm_csr_idx", csr_idx, str, 16))
    lines.append("")
    lines.append("/* Channel LLR per bit: log((1-p)/p). */")
    lines.append(c_array("float", "pcm_channel_llr", llr, fmt_float, 6))
    lines.append("")
    lines.append("/* Syndrome to decode. */")
    lines.append(c_array("uint8_t", "pcm_syndrome", syndrome, str, 32))
    lines.append("")
    lines.append("#endif /* PCM_DATA_H */")
 
    with open(args.output, "w") as fh:
        fh.write("\n".join(lines) + "\n")
 
    kb = lambda n: "%.1f KB" % (n / 1024.0)
    print("wrote %s" % args.output)
    print("  %d checks, %d bits, %d edges, index type %s"
          % (num_checks, num_bits, num_edges, idx_type))
    print("  row weight %d..%d, column weight %d..%d"
          % (min(row_weights), max(row_weights), min(col_weights), max(col_weights)))
    print("  memory needed for the decoder data: %s tables + %s working arrays = %s"
          % (kb(const_bytes), kb(ram_bytes), kb(const_bytes + ram_bytes)))
    print("  (the decoder program itself adds about 3 KB; the Vitis startup code,")
    print("   print routines and stack come on top of that)")
    if min(row_weights) == 0:
        print("  note: some checks have no bits attached")
    if 1 in row_weights:
        print("  note: some checks have exactly one bit. Such a check constrains")
        print("        nothing on its own, so its message is always 0, as in the")
        print("        C++ version.")
    if min(col_weights) == 0:
        print("  note: some bits are in no check; their LLR stays at the prior")
 
 
if __name__ == "__main__":
    main()