// =====================================================================================================
// TAPESTRY · src/tests/t_gpu.cu · R2's gate: host and device, bit for bit
//
// §9's R2 asks for "the folds and the sweep as kernels, one block per segment, one thread per cell,
// two colour passes, `int64` atomics for the projection" and "the host-device bit-identity harness
// with the contraction setting pinned", gated on "host and device bit-identical on the integer folds;
// the sweep within its envelope".
//
// THE CLAIM THIS DECIDES. §5 puts the sweep in the BOUNDED tier — "agreement to a stated envelope
// with no verb flips" — and the integer redesign at R1.1 argued it belongs in EXACT AND PORTABLE
// instead. R1.1's receipt claimed only what it had measured: bit-identical across runs on one build.
// This is the other half, and it is the one that can fail: a different compiler, a different
// instruction set, a different execution order, 8,192 threads instead of one loop.
//
// Both sides call `step_cell_at` from field_leaf.h. That is the whole method — not two
// implementations agreeing, which is a claim about two authors' care, but one function compiled
// twice, which is a claim about C++ and about integers.
//
// `-fmad=false` is passed anyway, and is expected to be inert: there is no floating-point multiply
// to contract. Pinning a setting that cannot matter is how you find out it cannot matter.
// =====================================================================================================

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <cuda_runtime.h>
#include "../fold/field.h"

using namespace tapestry;
using namespace tapestry::fold;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}
#define CU(call) do { const cudaError_t e_ = (call); if (e_ != cudaSuccess) { \
    std::printf("FAIL  cuda %s :: %s\n", #call, cudaGetErrorString(e_)); ++g_fail; return; } } while (0)

// ---- the kernels --------------------------------------------------------------------------------
// One block per segment, one thread per cell, two colour passes. No cell reads a same-colour
// neighbour, so the order threads arrive in cannot change the answer — which is what makes this a
// port of the host loop rather than a second algorithm.
__global__ void k_sweep(LatticeView L, int color) {
    const int32_t seg = (int32_t)blockIdx.x;
    const int32_t n = L.d.NCLS * L.d.NSLOT;
    for (int32_t idx = (int32_t)threadIdx.x; idx < n; idx += (int32_t)blockDim.x) {
        const int32_t cls = idx / L.d.NSLOT;
        const int32_t slot = idx % L.d.NSLOT;
        if (((cls + slot) & 1) != color) continue;
        int64_t moved = 0;
        step_cell_at(L, seg, cls, slot, &moved);
    }
}

// The projection, with int64 atomics. Integer accumulation is exact regardless of the order threads
// arrive in; a float atomicAdd is not, and a lattice that differs run to run makes every replay claim
// in the system false.
__global__ void k_project(LatticeDims d, const Cell* cells, int32_t n, uint64_t now_ns,
                          int64_t* load, int64_t* count) {
    const int32_t i = (int32_t)(blockIdx.x * blockDim.x + threadIdx.x);
    if (i >= n) return;
    const Cell c = cells[i];
    if (!contributes_state(c.state)) return;
    const int32_t idx = project_one_raw(d, c.seg, c.cls, c.due_ns, now_ns);
    atomicAdd((unsigned long long*)&load[idx], (unsigned long long)(c.amount_minor * FIX));
    atomicAdd((unsigned long long*)&count[idx], (unsigned long long)FIX);
}

// ---- the fixture ---------------------------------------------------------------------------------
static LatticeDims dims() {
    LatticeDims d; d.NSEG = 8; d.NCLS = 12; d.NSLOT = 16; d.slot_ns = 1000000000ull;
    return d;
}
static const uint64_t T0 = 1757000000000000000ull;

static void build(CellTable* t, std::vector<Cell>* cells, int n) {
    for (int i = 0; i < n; ++i) {
        ColdRec rec; rec.source = "olist"; rec.table = "orders"; rec.key = "o-" + std::to_string(i);
        const uint64_t id = cell_id_of(rec.source, rec.table, rec.key);
        Cell c{};
        c.id = id;
        c.seg = (uint32_t)(i % 8);
        c.cls = (uint32_t)(i % 12);
        c.amount_minor = 100 + (i * 37) % 90000 - 20000;      // negatives too: two's complement atomics
        c.due_ns = (i % 7 == 0) ? 0 : T0 + (uint64_t)(i % 23) * 1000000000ull;
        c.state = (uint8_t)((i % 11 == 0) ? C_CLOSED : (i % 5 == 0 ? C_DISPATCHED : C_OPEN));
        c.flags = (uint8_t)((i % 29 == 0) ? F_WARRANT : 0);
        c.opened_ns = T0;
        Cell& in = t->create(id, rec);
        in = c;
        cells->push_back(c);
    }
}

// =====================================================================================================
// 1 · The projection: int64 atomics against the host's ordered accumulation
// =====================================================================================================
static void t_projection() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 20000);

    Field host; std::string err;
    check(host.init(dims(), Coeffs(), &err), "gpu.proj.init", err);
    host.set_now(T0);
    for (const Cell& c : cells) if (contributes(c)) host.place(c, &err);

    const LatticeDims d = dims();
    const size_t n = (size_t)d.count();
    Cell* d_cells = nullptr; int64_t* d_load = nullptr; int64_t* d_count = nullptr;
    CU(cudaMalloc(&d_cells, cells.size() * sizeof(Cell)));
    CU(cudaMalloc(&d_load, n * sizeof(int64_t)));
    CU(cudaMalloc(&d_count, n * sizeof(int64_t)));
    CU(cudaMemcpy(d_cells, cells.data(), cells.size() * sizeof(Cell), cudaMemcpyHostToDevice));
    CU(cudaMemset(d_load, 0, n * sizeof(int64_t)));
    CU(cudaMemset(d_count, 0, n * sizeof(int64_t)));

    const int threads = 256;
    const int blocks = (int)((cells.size() + threads - 1) / threads);
    k_project<<<blocks, threads>>>(d, d_cells, (int32_t)cells.size(), T0, d_load, d_count);
    CU(cudaGetLastError());
    CU(cudaDeviceSynchronize());

    std::vector<int64_t> gl(n), gc(n);
    CU(cudaMemcpy(gl.data(), d_load, n * sizeof(int64_t), cudaMemcpyDeviceToHost));
    CU(cudaMemcpy(gc.data(), d_count, n * sizeof(int64_t), cudaMemcpyDeviceToHost));

    bool same = true; std::string first;
    for (size_t i = 0; i < n; ++i) {
        if (gl[i] != host.load_at((int32_t)i) || gc[i] != host.count_at((int32_t)i)) {
            same = false;
            first = "cell " + std::to_string(i) + " load host " + std::to_string(host.load_at((int32_t)i)) +
                    " device " + std::to_string(gl[i]);
            break;
        }
    }
    check(same, "gpu.proj.bit_identical", first);

    // The projection ran under 20,000 concurrent atomic adds in an order nobody chose, and matched an
    // ordered host loop exactly. That is the property integer accumulation buys.
    int64_t tot_h = 0, tot_d = 0;
    for (size_t i = 0; i < n; ++i) { tot_h += host.load_at((int32_t)i); tot_d += gl[i]; }
    check(tot_h == tot_d && tot_h != 0, "gpu.proj.conserved_across_the_bus", std::to_string(tot_h));

    CU(cudaFree(d_cells)); CU(cudaFree(d_load)); CU(cudaFree(d_count));
}

// =====================================================================================================
// 2 · The sweep: the same function, compiled twice, run 8,192 ways
// =====================================================================================================
static void t_sweep() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 20000);

    Field host; std::string err;
    host.init(dims(), Coeffs(), &err);
    host.set_now(T0);
    for (const Cell& c : cells) if (contributes(c)) host.place(c, &err);

    // Give the field some capacity so `src` is not simply the load, and some baseline, so the step
    // exercises every term rather than a degenerate one.
    const LatticeDims d = dims();
    const size_t n = (size_t)d.count();
    for (size_t i = 0; i < n; ++i) host.set_capacity((int32_t)i, (int64_t)((i * 7919) % 500000) * FIX / 97);

    // The device copy starts from exactly the host's arrays.
    std::vector<int64_t> load = host.load_v(), cap = host.capacity_v(), base = host.baseline_v(), dev = host.dev_v();
    std::vector<uint8_t> flags = host.flags_v();

    int64_t *d_load = nullptr, *d_cap = nullptr, *d_base = nullptr, *d_dev = nullptr;
    uint8_t* d_flags = nullptr;
    CU(cudaMalloc(&d_load, n * sizeof(int64_t)));
    CU(cudaMalloc(&d_cap,  n * sizeof(int64_t)));
    CU(cudaMalloc(&d_base, n * sizeof(int64_t)));
    CU(cudaMalloc(&d_dev,  n * sizeof(int64_t)));
    CU(cudaMalloc(&d_flags, n * sizeof(uint8_t)));
    CU(cudaMemcpy(d_load, load.data(), n * sizeof(int64_t), cudaMemcpyHostToDevice));
    CU(cudaMemcpy(d_cap,  cap.data(),  n * sizeof(int64_t), cudaMemcpyHostToDevice));
    CU(cudaMemcpy(d_base, base.data(), n * sizeof(int64_t), cudaMemcpyHostToDevice));
    CU(cudaMemcpy(d_dev,  dev.data(),  n * sizeof(int64_t), cudaMemcpyHostToDevice));
    CU(cudaMemcpy(d_flags, flags.data(), n * sizeof(uint8_t), cudaMemcpyHostToDevice));

    LatticeView gv;
    gv.d = d; gv.c = host.coeffs();
    gv.load = d_load; gv.capacity = d_cap; gv.baseline = d_base; gv.dev = d_dev; gv.flags = d_flags;

    // A fixed iteration count on both sides: this measures the arithmetic, not the stopping rule.
    const int ITERS = 40;
    for (int it = 0; it < ITERS; ++it)
        for (int color = 0; color < 2; ++color) {
            k_sweep<<<d.NSEG, 256>>>(gv, color);
            CU(cudaGetLastError());
        }
    CU(cudaDeviceSynchronize());

    // The host runs the identical schedule through the identical function.
    LatticeView hv = host.view();
    for (int it = 0; it < ITERS; ++it)
        for (int color = 0; color < 2; ++color)
            for (int32_t seg = 0; seg < d.NSEG; ++seg)
                for (int32_t cls = 0; cls < d.NCLS; ++cls)
                    for (int32_t slot = 0; slot < d.NSLOT; ++slot) {
                        if (((cls + slot) & 1) != color) continue;
                        int64_t moved = 0;
                        step_cell_at(hv, seg, cls, slot, &moved);
                    }

    std::vector<int64_t> gdev(n);
    std::vector<uint8_t> gflags(n);
    CU(cudaMemcpy(gdev.data(), d_dev, n * sizeof(int64_t), cudaMemcpyDeviceToHost));
    CU(cudaMemcpy(gflags.data(), d_flags, n * sizeof(uint8_t), cudaMemcpyDeviceToHost));

    size_t differ = 0; std::string first;
    int64_t max_abs_diff = 0, dev_inf = 0;
    for (size_t i = 0; i < n; ++i) {
        const int64_t h = host.dev_v()[i], g = gdev[i];
        const int64_t a = h < 0 ? -h : h;
        if (a > dev_inf) dev_inf = a;
        if (h != g) {
            ++differ;
            const int64_t dd = (h > g) ? (h - g) : (g - h);
            if (dd > max_abs_diff) max_abs_diff = dd;
            if (first.empty())
                first = "cell " + std::to_string(i) + " host " + std::to_string(h) + " device " + std::to_string(g);
        }
    }
    check(differ == 0, "gpu.sweep.bit_identical",
          std::to_string(differ) + " of " + std::to_string(n) + " differ, max " +
          std::to_string(max_abs_diff) + "; " + first);
    check(dev_inf > 0, "gpu.sweep.the_field_actually_moved", std::to_string(dev_inf));

    bool flags_same = true;
    for (size_t i = 0; i < n; ++i) if (gflags[i] != host.flags_v()[i]) { flags_same = false; break; }
    check(flags_same, "gpu.sweep.flags_bit_identical");

    // Lie arm: perturb one device cell by ONE unit and the comparison must catch it. A harness that
    // cannot see a single-unit difference is not measuring bit-identity.
    int64_t poke = gdev[n / 2] + 1;
    CU(cudaMemcpy(d_dev + n / 2, &poke, sizeof(int64_t), cudaMemcpyHostToDevice));
    std::vector<int64_t> again(n);
    CU(cudaMemcpy(again.data(), d_dev, n * sizeof(int64_t), cudaMemcpyDeviceToHost));
    size_t differ2 = 0;
    for (size_t i = 0; i < n; ++i) if (again[i] != host.dev_v()[i]) ++differ2;
    check(differ2 == 1, "gpu.sweep.lie.one_unit_of_difference_is_visible", std::to_string(differ2));

    CU(cudaFree(d_load)); CU(cudaFree(d_cap)); CU(cudaFree(d_base));
    CU(cudaFree(d_dev)); CU(cudaFree(d_flags));
}

int main() {
    std::printf("== TAPESTRY R2 host-device bit-identity ==\n");
    int devs = 0;
    if (cudaGetDeviceCount(&devs) != cudaSuccess || devs == 0) {
        std::printf("no CUDA device; R2's gate cannot be measured here\n");
        return 2;
    }
    cudaDeviceProp p{};
    cudaGetDeviceProperties(&p, 0);
    std::printf("device: %s  sm_%d%d  driver-visible\n", p.name, p.major, p.minor);

    t_projection();
    t_sweep();

    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
