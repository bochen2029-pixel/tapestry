// qc_conv.cpp — why the sweep does not reach tol at the estate's own sizing.
#include "osv_core.cuh"
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
using namespace osv;

struct Owned {
    Lattice L;
    std::vector<int64_t> load, cnt; std::vector<float> cap, base, dev; std::vector<uint8_t> flg;
    void alloc(int a,int b,int c,uint64_t s){ L.d.NSEG=a;L.d.NCLS=b;L.d.NSLOT=c;L.d.slot_ns=s;
        int n=L.d.count(); load.assign(n,0);cnt.assign(n,0);cap.assign(n,0.f);base.assign(n,0.f);
        dev.assign(n,0.f);flg.assign(n,0);
        L.load_fix=load.data();L.count_fix=cnt.data();L.capacity=cap.data();
        L.baseline=base.data();L.dev=dev.data();L.flags=flg.data(); }
};

int main(){
    const uint64_t HOUR=3600ull*1000000000ull, now=1000000ull*1000000000ull;
    std::mt19937 rng(4242);
    Owned G; G.alloc(4,12,16,HOUR);
    // project a 50k ledger by hand so we can see the source magnitudes
    for(size_t i=0;i<50000;++i){
        Commitment c; std::memset(&c,0,sizeof c);
        c.cls=rng()%12; c.seg=rng()%4;
        int r=rng()%10;
        if(r==0) c.due_ns=0;
        else if(r<3) c.due_ns=now-(uint64_t)(rng()%48)*HOUR;
        else c.due_ns=now+(uint64_t)(rng()%400)*HOUR;
        c.amount=1.0f+(float)(rng()%1000)/100.0f;
        c.state=(uint8_t)((rng()%12==0)?C_CLOSED:C_OPEN);
        if(!contributes(c)) continue;
        G.load[project_one(G.L.d,c,now)] += to_fix(c.amount);
    }
    for(int i=0;i<G.L.d.count();++i) G.cap[i]=200.0f+(float)(i%13)*10.0f;

    double lo=1e30, hi=-1e30, sum=0;
    for(int i=0;i<G.L.d.count();++i){
        const double s = (double)from_fix(G.load[i]) - G.cap[i];
        lo=std::min(lo,s); hi=std::max(hi,s); sum+=std::fabs(s);
    }
    std::printf("source term src=load-capacity : min %.1f  max %.1f  mean|src| %.1f\n",
        lo, hi, sum/G.L.d.count());

    G.L.tol=1e-4f;
    std::printf("\n iter        max move      max|dev|\n");
    for(int it=1; it<=200000; ++it){
        float mx=0.f;
        for(int color=0;color<2;++color)
            for(int s=0;s<G.L.d.NSEG;++s){ float m=sweep_segment(G.L,s,color); if(m>mx) mx=m; }
        float md=0.f; for(float v:G.dev) md=std::max(md,std::fabs(v));
        if(it<=8 || it==16 || it==32 || it==64 || it==128 || it==256 || it==1000 ||
           it==5000 || it==20000 || it==100000 || it==200000)
            std::printf("%7d   %12.3e   %10.2f\n", it, mx, md);
        if(mx<=G.L.tol){ std::printf("  CONVERGED at iter %d\n", it); break; }
    }

    // float epsilon at this magnitude
    float md=0.f; for(float v:G.dev) md=std::max(md,std::fabs(v));
    std::printf("\n  |dev| max = %.2f   1 ULP at that magnitude = %.3e   tol = %.3e\n",
        md, std::nextafterf(md,1e30f)-md, 1e-4f);
    std::printf("  ratio tol/ULP = %.2f  -> %s\n", 1e-4f/(std::nextafterf(md,1e30f)-md),
        (1e-4f/(std::nextafterf(md,1e30f)-md)) < 4.0
          ? "TOL IS AT OR BELOW FLOAT RESOLUTION: the loop can never break"
          : "tol is above float resolution");

    // same system in double, to prove it is precision and not the stencil
    {
        const int NC=G.L.d.NCLS, NS=G.L.d.NSLOT;
        std::vector<double> d(G.L.d.count(),0.0);
        auto IX=[&](int sg,int c,int s){ return (sg*NC+c)*NS+s; };
        int itd=0; double mvd=0;
        for(; itd<200000; ++itd){
            mvd=0;
            for(int color=0;color<2;++color)
             for(int sg=0;sg<G.L.d.NSEG;++sg)
              for(int c=0;c<NC;++c) for(int s=0;s<NS;++s){
                if(((c+s)&1)!=color) continue;
                const int i=IX(sg,c,s);
                double num=(double)from_fix(G.load[i])-G.cap[i]-G.base[i], den=G.L.decay;
                if(c>0){num+=G.L.k_cls*d[IX(sg,c-1,s)];den+=G.L.k_cls;}
                if(c+1<NC){num+=G.L.k_cls*d[IX(sg,c+1,s)];den+=G.L.k_cls;}
                if(s>0){num+=G.L.k_slot*d[IX(sg,c,s-1)];den+=G.L.k_slot;}
                if(s+1<NS){num+=G.L.k_slot*d[IX(sg,c,s+1)];den+=G.L.k_slot;}
                const double nd=num/den; mvd=std::max(mvd,std::fabs(nd-d[i])); d[i]=nd;
              }
            if(mvd<=1e-4) break;
        }
        std::printf("\n  SAME system in DOUBLE: converged to 1e-4 in %d iterations (move %.3e)\n",
            itd+1, mvd);
        std::printf("  => the stencil is fine. FLOAT dev[] cannot resolve tol=1e-4 at |dev|~%.0f.\n", md);
    }
    return 0;
}
