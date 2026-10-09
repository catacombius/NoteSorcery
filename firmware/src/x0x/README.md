# X0X DSP

These files come from [X0X](https://github.com/charlesvestal/fm1-x0x) 0.10.3-beta by Charles Vestal, a
GPL-3.0-only groovebox firmware for the FM-1. They are used as they are, apart from the changes their own
headers name.

| File | What | Origin |
|---|---|---|
| `bass303.c`, `bass303.h` | the TB-303 voice of the ACID engine (`../eng_acid.c`) | X0X, ported from [schwung-303](https://github.com/charlesvestal/schwung-303): Open303 by Robin Schmidt (MIT, `LICENSES/MIT-Open303.txt`), the Devilfish ranges after jc303, the RAT drive after dm-Rat (GPL-3.0) |
| `fastmath.h` | single-precision maths without libm | X0X |
| `x0x_param.h` | the parameter descriptors bass303 uses | X0X |

Float code runs on the FM-1's single-precision FPU (`tools/build.py` FPU flags). The firmware's ISR does not
save FPU registers, so float code runs only inside the audio interrupt, never in the main loop.
