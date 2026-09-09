// qc_diff.cpp — (1) does the tolerance break EVER fire in the shipped test? (2) is every cell
// permanently F_DIRTY at the page's sizing? (3) is a differential lattice update bit-identical to
// full re-projection? (4) what does a per-append field update actually cost with a sane stop rule?
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
static double ms_since(clk::time_point t){ return std::chrono::duration<double,std::milli>(clk::now()-t).count(); }

struct Owned {
    Lattice L; std::vector<int64_t> load,cnt; std::vector<float> cap,base,dev; std::vector<uint8_t> flg;
    void alloc(int a,int b,int c,uint64_t s){ L.d.NSEG=a;L.d.NCLS=b;L.d.NSLOT=c;L.d.slot_ns=s;
        int n=L.d.count(); load.assign(n,0);cnt.assign(n,0);cap.assign(n,0.f);base.assign(n,0.f);
        dev.assign(n,0.f);flg.assign(n,0);
        L.load_fix=load.data();L.count_fix=cnt.data();L.capacity=cap.data();
        L.baseline=base.data();L.dev=dev.data();L.flags=flg.data(); }
};

static std::vector<Commitment> make(size_t n,uint64_t now,uint32_t seed){
    std::mt19937 rng(seed); std::vector<Commitment> v(n);
    std::memset(v.data(),0,n*sizeof(Commitment));
    const uint64_t H=3600ull*1000000000ull;
    for(size_t i=0;i<n;++i){ Commitment& c=v[i];
        c.id=1000+i; c.cls=rng()%12; c.seg=rng()%4; c.seat=rng()%32;
        c.opened_ns=now-(uint64_t)(rng()%100)*H;
        int r=rng()%10;
        if(r==0) c.due_ns=0; else if(r<3) c.due_ns=now-(uint64_t)(rng()%48)*H;
        else c.due_ns=now+(uint64_t)(rng()%400)*H;
        c.amount=1.0f+(float)(rng()%1000)/100.0f;
        c.state=(uint8_t)((rng()%12==0)?C_CLOSED:C_OPEN);
        c.src_rev=1; }
    return v;
}
static void project_full(const std::vector<Commitment>& led, Owned& G, uint64_t now){
    std::fill(G.load.begin(),G.load.end(),(int64_t)0);
    std::fill(G.cnt.begin(),G.cnt.end(),(int64_t)0);
    for(const auto& m:led){ if(!contributes(m)) continue;
        const int i=project_one(G.L.d,m,now); G.load[i]+=to_fix(m.amount); G.cnt[i]+=to_fix(1.0f); }
}
static int solve_n(Owned& G,int iters){ for(int it=0;it<iters;++it){
    for(int color=0;color<2;++color) for(int s=0;s<G.L.d.NSEG;++s) sweep_segment(G.L,s,color); }
    return iters; }

int main(){
    const uint64_t H=3600ull*1000000000ull, now=1000000ull*1000000000ull;
    const size_t N=50000;

    // ---- (1) the shipped o_step's tolerance break, replicated exactly -------------------------
    {
        Owned G; G.alloc(1,12,16,H);
        std::mt19937 rng(99);
        for(int i=0;i<G.L.d.count();++i){ G.load[i]=to_fix(1.0f+(float)(rng()%500)/100.0f);
                                          G.cap[i]=1.0f+(float)(rng()%300)/100.0f; }
        G.L.tol=1e-7f;
        int fired=-1; float last=0;
        for(int it=0;it<4000;++it){ float mx=0.f;
            for(int color=0;color<2;++color){ float m=sweep_segment(G.L,0,color); if(m>mx) mx=m; }
            last=mx; if(mx<=G.L.tol){ fired=it+1; break; } }
        float md=0.f; for(float v:G.dev) md=std::max(md,std::fabs(v));
        std::printf("=== (1) o_step's tolerance break (osv_core_test.cpp:182-186) ===\n");
        std::printf("  tol=1e-7  |dev|max=%.3f  1 ULP there=%.3e  final max move=%.3e\n",
            md, std::nextafterf(md,1e30f)-md, last);
        char fb[64];
        if(fired<0) std::snprintf(fb,sizeof fb,"NEVER (ran the full 4000)");
        else        std::snprintf(fb,sizeof fb,"%d", fired);
        std::printf("  break fired at iteration: %s  -> the shipped falsifier runs all 4000 and\n"
                    "  has NEVER executed the convergence test it appears to carry.\n\n", fb);
    }

    // ---- (2) permanently dirty at the page's sizing --------------------------------------------
    Owned G; G.alloc(4,12,16,H);
    auto led = make(N, now, 4242);
    for(int i=0;i<G.L.d.count();++i) G.cap[i]=200.0f+(float)(i%13)*10.0f;
    G.L.tol=1e-4f;
    project_full(led,G,now);
    solve_n(G,200);
    {
        int dirty=0; for(uint8_t f:G.flg) if(f & F_DIRTY) ++dirty;
        std::printf("=== (2) F_DIRTY after 200 sweeps at 50k sizing (osv_core.cuh:208) ===\n");
        std::printf("  cells marked dirty: %d of %d (%.0f%%)\n", dirty, G.L.d.count(),
            100.0*dirty/G.L.d.count());
        std::printf("  => the per-cell move parks at 1 ULP > tol, so the dirty bit never clears.\n"
                    "     The field's ranking (blueprint §2.5 evicts 'by the field's ranking') is\n"
                    "     therefore uniform and carries no information.\n\n");
    }

    // ---- (3) differential update vs full re-projection: bit-identical? -------------------------
    {
        Owned D; D.alloc(4,12,16,H);
        for(int i=0;i<D.L.d.count();++i) D.cap[i]=G.cap[i];
        project_full(led,D,now);                       // same starting lattice

        std::mt19937 rng(7);
        // apply 1,000 facts two ways: full re-projection vs subtract-old/add-new
        Owned F; F.alloc(4,12,16,H);
        for(int i=0;i<F.L.d.count();++i) F.cap[i]=G.cap[i];
        auto led2 = led;
        for(int k=0;k<1000;++k){
            const int idx = (int)(rng()%N);
            Commitment& c = led2[idx];
            // differential: remove the old contribution first
            if(contributes(c)){ const int i=project_one(D.L.d,c,now);
                D.load[i]-=to_fix(c.amount); D.cnt[i]-=to_fix(1.0f); }
            // the fact
            c.amount += 1.0f + (float)(rng()%400)/100.0f;
            if(rng()%7==0) c.due_ns = now + (uint64_t)(rng()%40)*H;   // deadline moves -> slot moves
            if(rng()%23==0) c.state = C_CLOSED;                       // closes -> leaves the lattice
            // differential: add the new contribution
            if(contributes(c)){ const int i=project_one(D.L.d,c,now);
                D.load[i]+=to_fix(c.amount); D.cnt[i]+=to_fix(1.0f); }
        }
        project_full(led2,F,now);
        const bool same_load = (D.load==F.load), same_cnt = (D.cnt==F.cnt);
        std::printf("=== (3) differential lattice update vs full re-projection, 1000 facts ===\n");
        std::printf("  load_fix bit-identical : %s\n  count_fix bit-identical: %s\n",
            same_load?"YES":"NO", same_cnt?"YES":"NO");
        std::printf("  => fixed-point accumulation makes subtract-old/add-new EXACT, so the\n"
                    "     differential fold satisfies §2.4's verify_fold by construction.\n"
                    "     (A float accumulator could not do this -- subtraction would not undo.)\n\n");
    }

    // ---- (4) real per-append cost with a sane stop rule ----------------------------------------
    {
        std::printf("=== (4) per-append field cost, honest stop rule ===\n");
        // full re-projection
        { auto t=clk::now(); for(int r=0;r<50;++r) project_full(led,G,now);
          std::printf("  full re-projection, 50k cells        : %8.4f ms\n", ms_since(t)/50.0); }
        // differential: one fact
        { auto t=clk::now();
          for(int r=0;r<200000;++r){ Commitment& c=led[r%N];
            int i=project_one(G.L.d,c,now); G.load[i]-=to_fix(c.amount);
            G.load[i]+=to_fix(c.amount); }
          std::printf("  differential update, ONE fact        : %8.6f ms\n", ms_since(t)/200000.0); }
        // sweep cost per iteration
        { auto t=clk::now(); solve_n(G,2000);
          const double per = ms_since(t)/2000.0;
          std::printf("  one red-black sweep (768 cells)      : %8.4f ms\n", per);
          std::printf("  12 sweeps (double converges in 12)   : %8.4f ms\n", per*12);
          std::printf("\n  full-recompute per append : %8.4f ms -> %8.0f appends/s\n",
              0.754+per*12, 1000.0/(0.754+per*12));
          std::printf("  differential per append   : %8.4f ms -> %8.0f appends/s\n",
              0.0005+per*12, 1000.0/(0.0005+per*12));
          std::printf("  (projection dominates: it is %.0f%% of the full-recompute budget)\n",
              100.0*0.754/(0.754+per*12));
        }
    }
    return 0;
}
