/*
 * RISC-V Bitmanip Extension Helpers for QEMU.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

target_ulong HELPER(pcnt)(target_ulong rs1)
{
    target_ulong count = 0;
    for (int i = 0; i < TARGET_LONG_BITS; i++) {
        count += (rs1 >> i) & 1;;
    }
    return count;
}
