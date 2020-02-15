/*
 * RISC-V translation routines for the RVB Standard Extension.
 */

static bool trans_pcnt(DisasContext *ctx, arg_pcnt *a) {
    if (a->rd != 0) {
        TCGv t0 = tcg_temp_new();
        gen_get_gpr(t0, a->rs1);
        gen_helper_pcnt(cpu_gpr[a->rd], t0);
        tcg_temp_free(t0);
    }
    return true;
}
