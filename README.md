# Quality
 Programs for measuring code quality

A lightweight, dependency-free tool to measure code quality.
Works on any text/code file. Perfect for tracking AI-generated code over time.

C implementation (quality.c). Build: `gcc -std=c11 -O2 -Wall -o quality quality.c -lm`

Run: `./quality file.py` or `./quality .` or `./quality - < code.c`

Rules followed in conversion: no contexts (explicit params/returns only), no imbalances (balanced paths, alloc/free paired, full if/else), no indirections (value Metrics, embedded fixed arrays for dups `char dlines[N][LEN]`, index arithmetic, direct member access, flat functions, single-pass line processing).