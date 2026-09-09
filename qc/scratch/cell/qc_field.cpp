// qc_field.cpp — QC-2: cost of full re-projection + sweep per append (blueprint §2.4, OQ8),
// and what the field does when time passes with no append (§2.4 "maintained on every append").
// Build (MSVC): cl /nologo /O2 /std:c++17 /EHsc qc_field.cpp /Fe:qc_field.exe
#include "osv_core.cuh"
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace osv;
using clk = std::chrono::steady_clock;
static double ms_since(clk::time_point t){
    return std::chrono::duration<double, std::milli>(clk::now() - t).count();
}

struct Owned {
    Lattice L;
    std::vector<int64_t> load, cnt;
    std::vector<float>   cap, base, dev;
    std::vector<uint8_t> flg;
    void alloc(int nseg,int ncls,int nslot,uint64_t slot_ns){
        L.d.NSEG=nseg; L.d.NCLS=ncls; L.d.NSLOT=nslot; L.d.slot_ns=slot_ns;
        const int n=L.d.count();
        load.assign(n,0); cnt.assign(n,0); cap.assign(n,0.f); base.assign(n,0.f);
        dev.assign(n,0.f); flg.assign(n,0);
        L.load_fix=load.data(); L.count_fix=cnt.data(); L.capacity=cap.data();
        L.baseline=base.data(); L.dev=dev.data(); L.flags=flg.data();
    }
    void clear_acc(){ std::fill(load.begin(),load.end(),(int64_t)0); std::fill(cnt.begin(),cnt.end(),(int64_t)0); }
};

// the estate's sizing: 50,000 open commitments
static std::vector<Commitment> make(size_t n, uint64_t now, uint32_t seed){
    std::mt19937 rng(seed);
    std::vector<Commitment> v(n);
    std::memset(v.data(),0,n*sizeof(Commitment));
    for(size_t i=0;i<n;++i){
        Commitment& c=v[i];
        c.id=1000+i; c.cls=rng()%12; c.seg=rng()%4; c.seat=rng()%32;
        c.opened_ns = now - (uint64_t)(rng()%100)*3600ull*1000000000ull;
        const int r=rng()%10;
        if(r==0) c.due_ns=0;
        else if(r<3) c.due_ns = now - (uint64_t)(rng()%48)*3600ull*1000000000ull;
        else c.due_ns = now + (uint64_t)(rng()%400)*3600ull*1000000000ull;
        c.amount = 1.0f + (float)(rng()%1000)/100.0f;
        c.state  = (uint8_t)((rng()%12==0)?C_CLOSED:C_OPEN);
        c.flags  = 0;   // no warrants: warrants are boundary conditions and skip the sweep
        c.src_rev=1;
    }
    return v;
}

static void project(const std::vector<Commitment>& led, Owned& G, uint64_t now){
    G.clear_acc();
    for(const auto& m : led){
        if(!contributes(m)) continue;
        const int i = project_one(G.L.d, m, now);
        G.load[i] += to_fix(m.amount);
        G.cnt[i]  += to_fix(1.0f);
    }
}

// sweep to tolerance; returns iteration count
static int solve(Owned& G, int cap_iters){
    int it=0;
    for(; it<cap_iters; ++it){
        float mx=0.f;
        for(int color=0;color<2;++color)
            for(int s=0;s<G.L.d.NSEG;++s){ const float m=sweep_segment(G.L,s,color); if(m>mx) mx=m; }
        if(mx <= G.L.tol) return it+1;
    }
    return it;
}

int main(){
    const uint64_t HOUR = 3600ull*1000000000ull;
    const uint64_t now  = 1000000ull*1000000000ull;
    const size_t   N    = 50000;                     // the page's sizing
    auto led = make(N, now, 4242);

    // the lattice the tests use: 4 seg x 12 cls x 16 slots, one-hour slots
    Owned G; G.alloc(4,12,16,HOUR);
    for(int i=0;i<G.L.d.count();++i) G.cap[i] = 200.0f + (float)(i%13)*10.0f;
    G.L.tol = 1e-4f;                                  // the header's default tol

    std::printf("=== QC-2 FIELD COST (blueprint §2.4, open question 8) ===\n");
    std::printf("ledger = %zu commitments | lattice = %d x %d x %d = %d cells | slot = 1 h | tol = %g\n\n",
        N, G.L.d.NSEG, G.L.d.NCLS, G.L.d.NSLOT, G.L.d.count(), G.L.tol);

    // ---- 1. projection cost -------------------------------------------------------------------
    { auto t=clk::now(); for(int r=0;r<20;++r) project(led,G,now);
      const double per = ms_since(t)/20.0;
      std::printf("[1] full re-projection of %zu cells        : %8.3f ms\n", N, per); }

    // ---- 2. cold solve (dev = 0) --------------------------------------------------------------
    double cold_ms=0; int cold_it=0;
    { std::fill(G.dev.begin(),G.dev.end(),0.f); std::fill(G.flg.begin(),G.flg.end(),(uint8_t)0);
      auto t=clk::now(); cold_it=solve(G,20000); cold_ms=ms_since(t);
      std::printf("[2] COLD sweep to tol from dev=0           : %8.3f ms  (%d iters)\n", cold_ms, cold_it); }
    std::vector<float> converged = G.dev;

    // ---- 3. warm re-solve after ONE cell's fact changes ---------------------------------------
    double warm_ms=0; int warm_it=0;
    { led[123].amount += 500.0f;
      project(led,G,now);
      auto t=clk::now(); warm_it=solve(G,20000); warm_ms=ms_since(t);
      std::printf("[3] WARM re-solve after 1 fact             : %8.3f ms  (%d iters)\n", warm_ms, warm_it);
      led[123].amount -= 500.0f; }

    const double append_ms = 0.0;  // placeholder
    { auto t=clk::now();
      for(int r=0;r<10;++r){ project(led,G,now); solve(G,20000); }
      const double per = ms_since(t)/10.0;
      std::printf("[4] project + warm solve, per append       : %8.3f ms  -> %8.1f appends/s\n",
          per, 1000.0/per);
      std::printf("    organizational rate (a few/s)          : %s\n",
          per < 100.0 ? "COMFORTABLE" : "TOO SLOW");
      std::printf("    replay rate (thousands/s)              : %s (ceiling %.0f/s, need >=1000)\n",
          (1000.0/per) >= 1000.0 ? "OK" : "*** BLOCKED ***", 1000.0/per);
      (void)append_ms; }

    // ---- 5. what a TICK costs: now advances one slot width, no fact at all ---------------------
    {
        // how many cells change lattice slot when one hour passes with zero appends?
        int moved=0, into_slot0=0;
        for(const auto& m : led){
            if(!contributes(m)) continue;
            const int a = slot_of(G.L.d, now, m.due_ns);
            const int b = slot_of(G.L.d, now+HOUR, m.due_ns);
            if(a!=b){ ++moved; if(b==0) ++into_slot0; }
        }
        std::printf("\n[5] ONE HOUR passes with NO append (§2.4 says the field is maintained\n"
                    "    'on every append', so nothing happens):\n");
        std::printf("    commitments whose lattice slot is now WRONG : %d of %zu (%.1f%%)\n",
            moved, N, 100.0*moved/(double)N);
        std::printf("    of those, newly OVERDUE (should be slot 0, maximum pressure): %d\n", into_slot0);

        // now actually advance and re-solve from the stale warm start
        project(led,G,now+HOUR);
        auto t=clk::now(); const int it=solve(G,20000); const double tick_ms=ms_since(t);
        double maxd=0; for(size_t i=0;i<G.dev.size();++i) maxd=std::max(maxd,(double)std::fabs(G.dev[i]-converged[i]));
        std::printf("    re-solve after the slot shift  : %8.3f ms (%d iters, vs %d warm-after-1-fact)\n",
            tick_ms, it, warm_it);
        std::printf("    max |dev_after - dev_before|   : %.4f   <- the field the gate reads MOVED\n", maxd);
        std::printf("    => a tick is NOT cheap: it is a %d-iteration re-solve, ~%.0f%% of a cold solve.\n",
            it, 100.0*it/(double)cold_it);
    }

    // ---- 6. 24 idle hours: how far the field drifts if no tick entry exists --------------------
    {
        int worst=0;
        for(int h=1;h<=24;++h){
            int moved=0;
            for(const auto& m : led){
                if(!contributes(m)) continue;
                if(slot_of(G.L.d, now, m.due_ns) != slot_of(G.L.d, now+(uint64_t)h*HOUR, m.due_ns)) ++moved;
            }
            if(h==1||h==4||h==8||h==16||h==24)
                std::printf("    +%2dh idle : %5d of %zu commitments mis-slotted (%.1f%%)\n",
                    h, moved, N, 100.0*moved/(double)N);
            worst=std::max(worst,moved);
        }
        std::printf("    NSLOT=16 with 1-h slots => the whole lattice horizon is 16 h. A weekend\n"
                    "    with no append leaves every slot index saturated and slot 0 never fires.\n");
    }

    // ---- 7. determinism of the sweep: is 'now' on the tape? ------------------------------------
    {
        Owned A,B; A.alloc(4,12,16,HOUR); B.alloc(4,12,16,HOUR);
        for(int i=0;i<A.L.d.count();++i){ A.cap[i]=200.0f+(float)(i%13)*10.0f; B.cap[i]=A.cap[i]; }
        A.L.tol=B.L.tol=1e-4f;
        project(led,A,now);      solve(A,20000);
        project(led,B,now+60ull*1000000000ull);  solve(B,20000);   // 60 seconds later
        const bool same = std::memcmp(A.dev.data(),B.dev.data(),A.dev.size()*sizeof(float))==0;
        std::printf("\n[7] same tape, 'now' 60 s apart -> identical field: %s\n",
            same ? "yes" : "NO -- the field fold is a function of (tape, now)");
        std::printf("    => unless 'now' is itself a tape value, two cold rebuilds differ and\n"
                    "       blueprint falsifier 1 (replay bit-identical) CANNOT pass.\n");
    }
    std::printf("\n");
    return 0;
}
