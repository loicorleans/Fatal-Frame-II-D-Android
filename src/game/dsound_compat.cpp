// DSOUND stubs for Fatal Frame 2.
//
// default.xbe SHA-1 6cb979016ddca280f5b50ee255b28a4728b83fc2 (XDK 5849). Addresses below are only valid for this
// default.xbe. The log line "dsound: exports running their original code: ..." lists the exports that still run
// console code on the host's DSOUND objects (zero filled), which crashes with a null pointer + small offset
// (B007A: guest 0xB4, B0099: guest 0x18). A stub here replaces the recompiled function (strong definition over the
// weak one), returns S_OK and pops the stdcall arguments. The argument counts are the "ret N" of each function.
#include "sound/sound.h"

using namespace kernel;

X86_FUNC(sub_0014700C) { Return(ctx, 0, 1); }  // B00A3
X86_FUNC(sub_00147FAC) { Return(ctx, 0, 2); }  // B0076
X86_FUNC(sub_00147FEC) { Return(ctx, 0, 4); }  // B005A
X86_FUNC(sub_00148028) { Return(ctx, 0, 2); }  // B0057
X86_FUNC(sub_00148085) { Return(ctx, 0, 2); }  // B0099, crashed at sub_00147387+0x44 (fault address guest 0x18)
X86_FUNC(sub_0014808F) { Return(ctx, 0, 4); }  // B0080
X86_FUNC(sub_00148BDB) { Return(ctx, 0, 3); }  // B007A, crashed at sub_001486A4+0x34 (fault address guest 0xB4)
X86_FUNC(sub_00148BFF) { Return(ctx, 0, 3); }  // B0093
X86_FUNC(sub_00148C17) { Return(ctx, 0, 3); }  // B0094
X86_FUNC(sub_00148C2F) { Return(ctx, 0, 3); }  // B0097
X86_FUNC(sub_00148C34) { Return(ctx, 0, 5); }  // B009A
X86_FUNC(sub_00148C5D) { Return(ctx, 0, 5); }  // B009D
X86_FUNC(sub_00148C86) { Return(ctx, 0, 3); }  // B009C
