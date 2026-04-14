/*
 * QEMU RISC-V Smmpt (Memory Protection Table)
 *
 * Copyright (c) 2024 Alibaba Group. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "riscv_smmpt.h"
#include "pmp.h"
#include "system/memory.h"

/* Non-leaf/Leaf MPTE common fields. */
FIELD(MPTE, V, 0, 1)
FIELD(MPTE, L, 1, 1)

/* Non-leaf MPTE common fields. */
FIELD(MPTE_NON_LEAF, RSV, 2, 8)

/* Smmpt32 non-leaf MPTE fields. */
FIELD(MPTE_NON_LEAF_32, PPN, 10, 22)

/* Smmpt[43|52|64] non-leaf MPTE fields. */
FIELD(MPTE_NON_LEAF_64, PPN, 10, 44)
FIELD(MPTE_NON_LEAF_64, RSV, 54, 10)

/* Leaf MPTE common fields. */
FIELD(MPTE_LEAF, N, 2, 1)
FIELD(MPTE_LEAF, RSV, 3, 5)

/* Smmpt34 non-NAPOT leaf MPTE fields. */
FIELD(MPTE_LEAF_32, PERMS, 8, 24)

/* Smmpt[43|52|64] non-NAPOT leaf MPTE fields. */
FIELD(MPTE_LEAF_64, PERMS, 8, 48)
FIELD(MPTE_LEAF_64, RSV, 56, 8)

/* NAPOT leaf MPTE common fields. */
FIELD(MPTE_NAPOT_LEAF, PERMS, 8, 3)
FIELD(MPTE_NAPOT_LEAF, RSV, 11, 1)
FIELD(MPTE_NAPOT_LEAF, G, 12, 4)

/* Smmpt34 NAPOT leaf fields. */
FIELD(MPTE_NAPOT_LEAF_32, RSV, 16, 16)

/* Smmpt[43|52|64] NAPOT leaf fields. */
FIELD(MPTE_NAPOT_LEAF_64, RSV, 16, 48)

typedef uint64_t load_entry_fn(AddressSpace *, hwaddr,
                               MemTxAttrs, MemTxResult *);

static uint64_t load_entry_32(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldl(as, addr, attrs, result);
}

static uint64_t load_entry_64(AddressSpace *as, hwaddr addr,
                              MemTxAttrs attrs, MemTxResult *result)
{
    return address_space_ldq(as, addr, attrs, result);
}

static inline bool mpte_is_leaf(uint64_t mpte)
{
    return FIELD_EX8(mpte, MPTE, L);
}

static inline bool mpte_is_valid(uint64_t mpte)
{
    return FIELD_EX8(mpte, MPTE, V);
}

static inline bool mpte_is_napot(uint64_t mpte)
{
    return mpte_is_leaf(mpte) ? FIELD_EX8(mpte, MPTE_LEAF, N) : false;
}

static inline uint8_t mpte_get_napot_g(CPURISCVState *env,
                                       uint64_t mpte)
{
    return FIELD_EX64(mpte, MPTE_NAPOT_LEAF, G);
}

static uint64_t mpte_get_rsv(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    bool leaf = mpte_is_leaf(mpte);
    bool napot = mpte_is_napot(mpte);
    uint64_t rsv = 0;

    if (!leaf) {
        rsv |= FIELD_EX64(mpte, MPTE_NON_LEAF, RSV);

        if (mxl == MXL_RV64) {
            rsv |= FIELD_EX64(mpte, MPTE_NON_LEAF_64, RSV);
        }
    } else {
        rsv |= FIELD_EX64(mpte, MPTE_LEAF, RSV);

        if (napot) {
            rsv |= FIELD_EX64(mpte, MPTE_NAPOT_LEAF, RSV);
            rsv |= (mxl == MXL_RV32) ? FIELD_EX64(mpte, MPTE_NAPOT_LEAF_32, RSV) :
                                       FIELD_EX64(mpte, MPTE_NAPOT_LEAF_64, RSV);
        } else if (mxl == MXL_RV64) {
            rsv |= FIELD_EX64(mpte, MPTE_LEAF_64, RSV);
        }
    }

    return rsv;
}

static uint64_t mpte_get_perms(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);

    if (mpte_is_napot(mpte)) {
        return FIELD_EX64(mpte, MPTE_NAPOT_LEAF, PERMS);
    }

    return (mxl == MXL_RV32) ? FIELD_EX64(mpte, MPTE_LEAF_32, PERMS) :
                               FIELD_EX64(mpte, MPTE_LEAF_64, PERMS);
}

static bool mpte_napot_check_g(CPURISCVState *env, uint64_t mpte)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);
    uint8_t g = mpte_get_napot_g(env, mpte);

    return (mxl == MXL_RV32) ? (g == 6) : (g == 4);
}

static uint64_t mpte_get_ppn(CPURISCVState *env, uint64_t mpte, int pn)
{
    RISCVMXL mxl = riscv_cpu_mxl(env);

    return (mxl == MXL_RV32) ? FIELD_EX64(mpte, MPTE_NON_LEAF_32, PPN) :
                               FIELD_EX64(mpte, MPTE_NON_LEAF_64, PPN);
}

/* Caller should assert i before call this interface */
static int mpt_get_pn(hwaddr addr, int i, mpt_mode_t mode)
{
    if (mode == SMMPT34) {
        return i == 0
            ? extract64(addr, 15, 10)
            : extract64(addr, 25, 9);
    } else {
        int offset = 16 + i * 9;
        if ((mode == SMMPT64) && (i == 4)) {
            return extract64(addr, offset, 12);
        } else {
            return extract64(addr, offset, 9);
        }
    }
}

/*
 * Caller should assert i before call this interface.
 * This should be called for non-NAPOT leaf MPTE.
 */
static int mpt_get_pi(hwaddr addr, int i, mpt_mode_t mode)
{
    if (mode == SMMPT34) {
        return i == 0
            ? extract64(addr, 12, 3)
            : extract64(addr, 22, 3);
    } else {
        int offset = 16 + i * 9;
        return extract64(addr, offset - 4, 4);
    }
}

static bool smmpt_lookup(CPURISCVState *env, hwaddr addr, mpt_mode_t mode,
                         int *allowed_access,
                         MMUAccessType access_type)
{
    MemTxResult res;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    CPUState *cs = env_cpu(env);
    hwaddr mpte_addr, base = (hwaddr)env->mptppn << PGSHIFT;
    load_entry_fn *load_entry;
    uint32_t mptesize, levels, xwr;
    int pn, pi, pmp_prot, pmp_ret;
    uint64_t mpte, perms;
    bool napot;

    switch (mode) {
    case SMMPT34:
        load_entry = &load_entry_32; levels = 2; mptesize = 4; break;
    case SMMPT43:
        load_entry = &load_entry_64; levels = 3; mptesize = 8; break;
        break;
    case SMMPT52:
        load_entry = &load_entry_64; levels = 4; mptesize = 8; break;
    case SMMPT64:
        load_entry = &load_entry_64; levels = 5; mptesize = 8; break;
    case SMMPTBARE:
        *allowed_access = (PAGE_READ | PAGE_WRITE | PAGE_EXEC);
        return true;
    default:
        g_assert_not_reached();
        break;
    }

    for (int i = levels - 1; i >= 0 ; i--) {
        /* 1. Get pn[i] as the mpt index */
        pn = mpt_get_pn(addr, i, mode);

        /* 2. Get mpte address and get mpte */
        mpte_addr = base + pn * mptesize;
        pmp_ret = get_physical_address_pmp(env, &pmp_prot, mpte_addr,
                                           mptesize, MMU_DATA_LOAD, PRV_M);
        if (pmp_ret != TRANSLATE_SUCCESS) {
            return false;
        }
        mpte = load_entry(cs->as, mpte_addr, attrs, &res);

        /* 3. Check valid bit and reserve bits of mpte */
        if (!mpte_is_valid(mpte) || mpte_get_rsv(env, mpte)) {
            return false;
        }

        /* 4. Process non-leaf node */
        if (!mpte_is_leaf(mpte)) {
            if (i == 0) {
                return false;
            }

            base = mpte_get_ppn(env, mpte, pn) << PGSHIFT;
            continue;
        }

        /* 5. Process leaf node */
        napot = mpte_is_napot(mpte);

        if (napot && !mpte_napot_check_g(env, mpte)) {
            return false;
        }

        pi = napot ? 0 : mpt_get_pi(addr, i, mode);
        perms = mpte_get_perms(env, mpte);
        xwr = (perms >> (pi * 3)) & 0x7;
        *allowed_access = xwr;

        switch (xwr) {
        case PAGE_READ:
            return access_type == MMU_DATA_LOAD;
        case PAGE_EXEC:
            return access_type == MMU_INST_FETCH;
        case (PAGE_READ | PAGE_EXEC):
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_INST_FETCH);
        case (PAGE_READ | PAGE_WRITE):
            return (access_type == MMU_DATA_LOAD ||
                    access_type == MMU_DATA_STORE);
        case (PAGE_READ | PAGE_WRITE | PAGE_EXEC):
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool smmpt_check_access(CPURISCVState *env, hwaddr addr,
                        int *allowed_access, MMUAccessType access_type)
{
    bool mpt_has_access;
    mpt_mode_t mode = env->mptmode;

    mpt_has_access = smmpt_lookup(env, addr, mode,
                                  allowed_access, access_type);
    return mpt_has_access;
}
