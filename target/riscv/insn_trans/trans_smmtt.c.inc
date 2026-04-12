/*
 * RISC-V translation routines for the Smmtt Standard Instruction Set.
 *
 * Copyright (c) 2026 Alibaba Group
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_SMSDID(ctx) do {          \
    if (!ctx->cfg_ptr->ext_smsdid) {      \
        return false;                     \
    }                                     \
} while (0)

#ifndef CONFIG_USER_ONLY
static inline bool do_tlb_flush_all(DisasContext *ctx)
{
    if (ctx->priv != PRV_M) {
        return false;
    }
    decode_save_opc(ctx, 0);
    gen_helper_tlb_flush_all(tcg_env);
    return true;
}
#endif

static inline bool do_mfence_spa(DisasContext *ctx)
{
#ifndef CONFIG_USER_ONLY
    REQUIRE_SMSDID(ctx);
    return do_tlb_flush_all(ctx);
#endif
    return false;
}

static bool do_minval_spa(DisasContext *ctx)
{
#ifndef CONFIG_USER_ONLY
    REQUIRE_SVINVAL(ctx);
    REQUIRE_SMSDID(ctx);
    return do_tlb_flush_all(ctx);
#endif
    return false;
}

static bool trans_mfence_spa(DisasContext *ctx, arg_mfence_spa *a)
{
    return do_mfence_spa(ctx);
}

static bool trans_minval_spa(DisasContext *ctx, arg_minval_spa *a)
{
    return do_minval_spa(ctx);
}
