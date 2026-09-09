// =====================================================================================================
// TAPESTRY · src/fold/field_leaf.h · the field's arithmetic, compiled host and device from ONE source
//
// §12 keeps `osv_core.cuh`'s leaves and the reason they were written that way: "every function the
// field advances by is OSV_HD, so `g++ -x c++` and `nvcc` emit the same arithmetic. Not 'the same
// equations' as a promise — the same translation unit as a property."
//
// This file is that translation unit for TAPESTRY. Nothing here allocates, nothing here holds a
// container, and nothing here touches a float. The host `Field` in field.h and the kernels in
// field_gpu.cu both call `step_cell_at` — so a host-device disagreement would have to come from the
// compilers disagreeing about integer arithmetic, which is the one thing they cannot do.
//
// That is the whole design of R2's gate. A "bit-identical" claim made about two implementations is a
// claim about two authors' care; made about one function compiled twice, it is a claim about C++.
// =====================================================================================================
#pragma once

#include <cstdint>

#if defined(__CUDACC__)
#  define TAP_HD __host__ __device__
#else
#  define TAP_HD
#endif

namespace tapestry {
namespace fold {

// The lattice's numeric unit: one minor unit (a cent) times 2^16.
#define TAP_FIX_BITS 16
inline constexpr int     FIX_BITS = TAP_FIX_BITS;
inline constexpr int64_t FIX      = (int64_t)1 << TAP_FIX_BITS;

// ---- totality, on both sides -----------------------------------------------------------------------
// Checked before the operation, because signed overflow is undefined behaviour and so cannot be
// detected after it. A fold that wraps is a fold that lies, on a card as much as on a core.
TAP_HD inline bool add_checked(int64_t a, int64_t b, int64_t* out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b; return true;
}
TAP_HD inline bool mul_checked(int64_t a, int64_t b, int64_t* out) {
    if (a > 0) {
        if (b > 0)      { if (a > INT64_MAX / b) return false; }
        else if (b < 0) { if (b < INT64_MIN / a) return false; }
    } else if (a < 0) {
        if (b > 0)      { if (a < INT64_MIN / b) return false; }
        else if (b < 0) { if (b < INT64_MAX / a) return false; }
    }
    *out = a * b; return true;
}

// ---- dimensions ------------------------------------------------------------------------------------
struct LatticeDims {
    int32_t  NSEG = 0, NCLS = 0, NSLOT = 0;
    uint64_t slot_ns = 0;
    TAP_HD int32_t count() const { return NSEG * NCLS * NSLOT; }
    TAP_HD bool ok() const { return NSEG > 0 && NCLS > 0 && NSLOT > 1 && slot_ns > 0; }
};

TAP_HD inline int32_t cell_index(const LatticeDims& d, int32_t seg, int32_t cls, int32_t slot) {
    return (seg * d.NCLS + cls) * d.NSLOT + slot;
}

// Overdue is not negative time, it is maximum pressure, so it lands in slot 0. No deadline lands in
// the far bucket.
TAP_HD inline int32_t slot_of(const LatticeDims& d, uint64_t now_ns, uint64_t due_ns) {
    if (due_ns == 0) return d.NSLOT - 1;
    if (due_ns <= now_ns) return 0;
    const uint64_t ahead = (due_ns - now_ns) / d.slot_ns;
    return ahead >= (uint64_t)d.NSLOT ? d.NSLOT - 1 : (int32_t)ahead;
}

// ---- the relaxation coefficients --------------------------------------------------------------------
// Dyadic rationals over one power-of-two denominator. That constraint is what makes the step integer
// arithmetic with a single truncating division — and truncation toward zero is the same instruction on
// both sides of the bus.
struct Coeffs {
    int64_t  den    = 256;
    int64_t  omega  = 256;
    int64_t  decay  = 256;
    int64_t  k_cls  = 64;
    int64_t  k_slot = 64;
    int64_t  tol_num = 1, tol_den = 100000;
    uint32_t max_iters = 128;
    int32_t  baseline_shift = 6;
    TAP_HD bool ok() const {
        if (den <= 0 || (den & (den - 1)) != 0) return false;
        if (omega <= 0 || decay <= 0 || k_cls < 0 || k_slot < 0) return false;
        if (tol_den <= 0 || tol_num < 0) return false;
        if (max_iters == 0 || baseline_shift < 0 || baseline_shift > 40) return false;
        return true;
    }
};

// The projection's two predicates, taking the fields they need rather than the whole `Cell`, so this
// header stays free of the containers cells.h carries and both sides still share one implementation.
TAP_HD inline int32_t project_one_raw(const LatticeDims& d, uint32_t seg, uint32_t cls,
                                      uint64_t due_ns, uint64_t now_ns) {
    const int32_t s = (int32_t)(seg % (uint32_t)d.NSEG);
    const int32_t c = (int32_t)(cls % (uint32_t)d.NCLS);
    return cell_index(d, s, c, slot_of(d, now_ns, due_ns));
}
// A cell contributes while it is outstanding: open, held, or dispatched. Closed obligations are
// history; dispatched ones still occupy the seat discharging them, so they still carry load.
TAP_HD inline bool contributes_state(uint8_t state) { return state <= 2; }

// A raw view of the lattice: what a kernel can be handed, and what the host class hands its own
// arrays to, so both reach `step_cell_at` by the same door.
struct LatticeView {
    LatticeDims d;
    Coeffs      c;
    int64_t*    load     = nullptr;
    int64_t*    capacity = nullptr;
    int64_t*    baseline = nullptr;
    int64_t*    dev      = nullptr;
    uint8_t*    flags    = nullptr;
};

// The warrant bit, mirrored from cells.h so the leaf needs no include.
#define TAP_F_WARRANT (1u << 0)
#define TAP_F_DIRTY   (1u << 6)

// ---- the step ----------------------------------------------------------------------------------------
//   src   = load − capacity − baseline                    (exact, fixed point)
//   lapN  = Σ k_num·(dev[n] − dev[i])                     (over the denominator D)
//   diagN = decay_num + Σ k_num                           (over D)
//   rN    = src·D + lapN − decay_num·dev[i]               (over D)
//   dev  += omega_num·rN / (D·diagN)                      one truncating division
//
// The diagonal is decay PLUS the incident couplings, not decay alone: dividing by decay is the classic
// way to make an unrelaxed sweep diverge on the interior, and it looks right while it does.
TAP_HD inline bool step_cell_at(const LatticeView& L, int32_t seg, int32_t cls, int32_t slot,
                                int64_t* moved) {
    const int32_t i = cell_index(L.d, seg, cls, slot);
    *moved = 0;
    if (L.flags[i] & TAP_F_WARRANT) return true;          // a boundary condition is never relaxed

    const int64_t d0 = L.dev[i];
    int64_t lapN = 0, diagN = L.c.decay;

    // Written out rather than looped: a kernel wants the branches, and the four neighbours are the
    // stencil, not a collection.
    if (cls > 0) {
        int64_t t = 0;
        if (!mul_checked(L.c.k_cls, L.dev[cell_index(L.d, seg, cls - 1, slot)] - d0, &t)) return false;
        if (!add_checked(lapN, t, &lapN)) return false;
        diagN += L.c.k_cls;
    }
    if (cls + 1 < L.d.NCLS) {
        int64_t t = 0;
        if (!mul_checked(L.c.k_cls, L.dev[cell_index(L.d, seg, cls + 1, slot)] - d0, &t)) return false;
        if (!add_checked(lapN, t, &lapN)) return false;
        diagN += L.c.k_cls;
    }
    if (slot > 0) {
        int64_t t = 0;
        if (!mul_checked(L.c.k_slot, L.dev[cell_index(L.d, seg, cls, slot - 1)] - d0, &t)) return false;
        if (!add_checked(lapN, t, &lapN)) return false;
        diagN += L.c.k_slot;
    }
    if (slot + 1 < L.d.NSLOT) {
        int64_t t = 0;
        if (!mul_checked(L.c.k_slot, L.dev[cell_index(L.d, seg, cls, slot + 1)] - d0, &t)) return false;
        if (!add_checked(lapN, t, &lapN)) return false;
        diagN += L.c.k_slot;
    }

    const int64_t src = L.load[i] - L.capacity[i] - L.baseline[i];
    int64_t rN = 0, t2 = 0;
    if (!mul_checked(src, L.c.den, &rN)) return false;
    if (!add_checked(rN, lapN, &rN)) return false;
    if (!mul_checked(L.c.decay, d0, &t2)) return false;
    if (!add_checked(rN, -t2, &rN)) return false;

    int64_t num = 0, den = 0;
    if (!mul_checked(L.c.omega, rN, &num)) return false;
    if (!mul_checked(L.c.den, diagN, &den)) return false;
    if (den == 0) return false;
    const int64_t delta = num / den;                      // truncation toward zero, one rule, both sides
    int64_t nd = 0;
    if (!add_checked(d0, delta, &nd)) return false;
    L.dev[i] = nd;

    const int64_t m = delta < 0 ? -delta : delta;
    *moved = m;
    if (m > 0) L.flags[i] = (uint8_t)(L.flags[i] | TAP_F_DIRTY);
    else       L.flags[i] = (uint8_t)(L.flags[i] & ~TAP_F_DIRTY);
    return true;
}

} // namespace fold
} // namespace tapestry
