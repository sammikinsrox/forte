// The AVX2 + FMA build of the engine's hot loops (Kernels.h). Compiled with /arch:AVX2
// (engine/CMakeLists.txt), and only ever called once cpuSupportsAvx2() says the CPU can.
#define FORTE_KERNELS_AVX2 1
#include "Kernels.inc"
