The Fixed Point Arithmetic implementation

I have adopted an approach of 16.16 fixed point integer arithmetic to ensure deterministic behaviour across all compilers as well as hardwares and optimizations, to ensure the DDA algorithm faces the least of cycle wastages which may cause unexpected behaviour.

What is 16.16 fixed point representation?
Think of it as a representation where the actual storage units for any Real Number are standard integers, with an implicit decimal placed in between (That we have implicitly placed there by scaling), so that we can leverage the deterministic behaviours of the int32_t operations, and also preserve fractional precesion upto some limits.
in a 16.16 representation, the higher 16 bits represent the standard integral part, whereas the lower order 16 bits represent the fractional precesion.
for the integral part, the range is (Since we are using signed 2's complement), -32768 to +32767.

for the fractional part, the resolution is: 1/65536 ≈ 0.000015259

Scaling factor, S is 2^16.

to convert a real value, A to the fixed point representation a, we can have the following conversion:

a = A X S
equivalent in bit-wise manipulation, to A << 16.

The math for fixed_mul implementation:
Q_real = (a/S) × (b/S) = (a × b) / S
Correct: Q_fixed = ((int64_t)a × b) >> 16

the int64_t prevents wrap around, since the multiplication of 2 int32_t units leads to a 32.32 intermediate, which after the 16 bit right-shift drops the excess fractional bits returning the result to 16.16 fixed point notation.

the math implementation for fixed_div is:
Q_real = (a/S) / (b/S) = a / b
Q_fixed = Q_real × S = (a × S) / b = (a << 16) / b

for fixed_sqrt implementation, the implementation translates to a loop of imul,sub and shr, which converges deterministically in 15-25 cycles.

Also, for fixed_divs we also have to check for b==0 and clamp the result to INT32_MIN or INT32_MAX to prevent any undefined behaviours messing up with our logic.

Analysis of the original DOOM Source code for the fixed point Arithmetic
the link for the implementation of fixed point arithmetic in the original DOOM source code can be found at:
1.) [text](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/m_fixed.c)
2.) [text](https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/m_fixed.h)

Here is a brief discussion about it:
I will only discuss the features from the original implementation which i would like to change in my implementation based on architectural changes since the original source code was written.

in m_fixed.h:

`typedef int fixed_t;`
int was guaranteed 32-bit on DOS/Win32 compilers (Borland C, Watcom).
After C99+ mandating of explict width-types, The int size can break across ARM64 or other architectures, so the safest approach would be to modify it as:

`typedef int32_t fixed_t;`

to keep it explicitly 32 bit.

Also, function definations in the header can be updated as `static inline` to enable compiler-inlining and auto-vectorization.

in m_fixed.c:

in fixedMul implementation:

`fixed_t FixedMul(fixed_t a, fixed_t b) {
    return ((long long) a * (long long) b) >> FRACBITS;
}`

for `long long`, it was a non standard GCC extension. We can ensure perfect 64 bit widths by typecasting directly with int64_t.



__________________________________________________________________________________________________________

Branched Vs Branchless implementation: Why does it matter?
Modern superscalar CPUs fetch/decode/execute instructions in overlapping waves (14–20+ stages). A conditional branch (if, ? :) forces the fetch unit to guess the next path.It Stores all the possible branches (in advance, before even calculating the result of the branches) in a buffer with the starting address of each of these branches.It tries to guess through a branch and feeds it to the pipeline so that the pipeline is never empty at the execution phase, however if there are mis-predicted branches, it has to flush the entire pipeline and then load the correct branch. this causes a heavy whooping penalty of 15-22 cycles, something i cannot afford when the DDA algorithm calculates stuff through the entire pixel buffer(It could cause a heavy FPS drop as well).

Hence, all the standard fixed point arithmetic operation implementations have been kept branchless to ensure no cpu cycle penalties due to mis-predicted branches, and also to try and keep everything as linear as possible in the assembly phase.

I carried out a benchmarking test to test out the actual difference in branched vs branchless implementation of some sort of math on my own machine, and see the difference.
Below are the results I got:

Iterations: 50000000
Branched:    4.031 ns/op (201.54 ms total)
Branchless:  1.885 ns/op (94.27 ms total)
Sink: -11149343 (prevents dead-code elimination)

 Performance counter stats for './branch_benchmark':

    1,83,44,56,093      cycles:u                                                              
    4,32,35,41,961      instructions:u                                                        
       2,66,73,813      branch-misses:u                                                       
      95,00,32,815      branches:u                                                            
            539.65 msec cpu-clock:u                                                           

       0.540555972 seconds time elapsed

       0.521466000 seconds user
       0.017970000 seconds sys

We can clearly notice the optimization, which is more than 50% in runtime per operation.
Anyone interested can check out the benchmarking script in this directory.

The benchmarking script is in `branched_vs_branchless_perf.c`

The commands to compile it (Make sure you follow this since you dont want gcc to automatically convert your branched stated to branchless, or re-order execution steps which may yield non-desirable results for the benchmark):

`gcc -O2 -fno-if-conversion -o branch_benchmark branched_vs_branchless_perf.c -lrt`

The command to actually test out the performance with all the metrics included above is:

`perf stat -e cycles,instructions,branch-misses,branches,cpu-clock ./branch_benchmark`

__________________________________________________________________________________________________________

Section on X87 Vs SSE Benchmarking and comparision coming soon!


