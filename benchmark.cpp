// fast_cachebench.cpp  — quick AIDA-like L1/L2/L3/Memory benchmark
// Build: g++ -O3 -march=native -std=c++17 fast_cachebench.cpp -o cachebench
// Run (very fast): ./cachebench --quick
// Tunables: --iters N (bandwidth loops), --stride B, --l1KB X --l2KB Y --l3KB Z --memKB M
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static void* alloc_aligned(size_t bytes, size_t align) {
    void* p = nullptr;
    if (posix_memalign(&p, align, bytes)) return nullptr;
    return p;
}
static void  free_aligned(void* p) { free(p); }

using clk = std::chrono::steady_clock;
static volatile uint64_t sink64 = 0;

struct Args {
    int iters = 3;           // bandwidth repetitions per tier (small => fast)
    size_t stride = 64;      // for latency pointer chasing
    size_t l1KB = 32, l2KB = 512, l3KB = 8192, memKB = 131072; // 128MB for DRAM
    bool quick = false;
};
static void usage(const char* p){
    std::cerr << "Usage: " << p
              << " [--iters N] [--stride B] [--l1KB N] [--l2KB N] [--l3KB N] [--memKB N] [--quick]\n";
}
static bool parse(int argc, char** argv, Args& a){
    for (int i=1;i<argc;++i){
        std::string s(argv[i]);
        auto need=[&](int n){ if(i+n>=argc){usage(argv[0]); return false;} return true; };
        if (s=="--iters"){ if(!need(1)) return false; a.iters=std::stoi(argv[++i]); }
        else if (s=="--stride"){ if(!need(1)) return false; a.stride=std::stoull(argv[++i]); }
        else if (s=="--l1KB"){ if(!need(1)) return false; a.l1KB=std::stoull(argv[++i]); }
        else if (s=="--l2KB"){ if(!need(1)) return false; a.l2KB=std::stoull(argv[++i]); }
        else if (s=="--l3KB"){ if(!need(1)) return false; a.l3KB=std::stoull(argv[++i]); }
        else if (s=="--memKB"){ if(!need(1)) return false; a.memKB=std::stoull(argv[++i]); }
        else if (s=="--quick"){ a.quick=true; }
        else if (s=="-h"||s=="--help"){ usage(argv[0]); return false; }
        else { std::cerr<<"Unknown arg: "<<s<<"\n"; usage(argv[0]); return false; }
    }
    if (a.stride==0) a.stride=sizeof(void*);
    if (a.stride % sizeof(void*) != 0) a.stride = ((a.stride + sizeof(void*) - 1)/sizeof(void*)) * sizeof(void*);
    if (a.quick){ a.iters=2; a.memKB = std::min<size_t>(a.memKB, 65536); } // 64MB in quick mode
    return true;
}

static double bw_read_gbs(char* buf, size_t bytes, int iters){
    size_t els = bytes/sizeof(double);
    auto* p = reinterpret_cast<double*>(buf);
    // warm
    double warm=0; for(size_t i=0;i<els;i+=64/sizeof(double)) warm+=p[i];
    sink64 ^= (uint64_t)warm;
    double t=0;
    for(int r=0;r<iters;++r){
        auto t0=clk::now();
        double sum=0; for(size_t i=0;i<els;++i) sum += p[i];
        auto t1=clk::now();
        t += std::chrono::duration<double>(t1-t0).count();
        sink64 ^= (uint64_t)sum;
    }
    t/=iters; return bytes/t/1e9;
}
static double bw_write_gbs(char* buf, size_t bytes, int iters){
    size_t els = bytes/sizeof(double);
    auto* p = reinterpret_cast<double*>(buf);
    for(size_t i=0;i<els;i+=64/sizeof(double)) p[i]=(double)i; // warm
    double t=0;
    for(int r=0;r<iters;++r){
        auto t0=clk::now();
        for(size_t i=0;i<els;++i) p[i]=(double)i;
        auto t1=clk::now();
        t += std::chrono::duration<double>(t1-t0).count();
    }
    t/=iters; return bytes/t/1e9;
}
static double bw_copy_gbs(char* dst, char* src, size_t bytes, int iters){
    std::memcpy(dst,src,bytes); // warm
    double t=0;
    for(int r=0;r<iters;++r){
        auto t0=clk::now();
        std::memcpy(dst,src,bytes);
        auto t1=clk::now();
        t += std::chrono::duration<double>(t1-t0).count();
    }
    t/=iters; return bytes/t/1e9;
}
static double latency_ns(char* buf, size_t bytes, size_t stride){
    size_t nodes = std::max<size_t>(2, bytes/stride);
    std::vector<size_t> idx(nodes); for(size_t i=0;i<nodes;++i) idx[i]=i;
    std::mt19937_64 rng(1234567); std::shuffle(idx.begin(), idx.end(), rng);
    for(size_t i=0;i+1<nodes;++i){
        auto slot = reinterpret_cast<char**>(buf + idx[i]*stride);
        *slot = (buf + idx[i+1]*stride);
    }
    *reinterpret_cast<char**>(buf + idx.back()*stride) = (buf + idx[0]*stride);
    volatile char** p = reinterpret_cast<volatile char**>(buf + idx[0]*stride);
    // small warmup
    for(size_t i=0;i<2000;++i){ p = (volatile char**)(*p); }
    // choose derefs capped for speed
    uint64_t derefs = std::min<uint64_t>(150000, std::max<uint64_t>(40000, nodes*8));
    auto t0=clk::now();
    for(uint64_t i=0;i<derefs;++i){ p = (volatile char**)(*p); }
    auto t1=clk::now();
    sink64 ^= (uint64_t)(uintptr_t)p;
    double ns = std::chrono::duration<double>(t1-t0).count()*1e9;
    return ns / (double)derefs;
}

struct Row { double r,w,c,l; };
static Row benchTier(const char* name, size_t KB, char* b1, char* b2, int iters, size_t stride){
    size_t bytes = KB*1024ULL;
    Row x;
    x.r = bw_read_gbs(b1, bytes, iters);
    x.w = bw_write_gbs(b1, bytes, iters);
    x.c = bw_copy_gbs(b2, b1, bytes, iters);
    x.l = latency_ns(b1, bytes, stride);
    (void)name;
    return x;
}

static void printRow(const char* label, const Row& x){
    auto fmt=[&](double v){ bool gb=v>=1000.0; double vv=gb?v:v*1000.0; const char* u=gb?"GB/s":"MB/s";
        std::cout<<std::setw(8)<<std::fixed<<std::setprecision(2)<<vv<<" "<<u; };
    std::cout<<std::left<<std::setw(8)<<label<<"  Read "; fmt(x.r);
    std::cout<<"   Write "; fmt(x.w);
    std::cout<<"   Copy ";  fmt(x.c);
    std::cout<<"   Latency "<<std::setw(6)<<std::fixed<<std::setprecision(2)<<x.l<<" ns\n";
}

int main(int argc, char** argv){
    Args A; if(!parse(argc,argv,A)) return 1;

    const size_t align = 1ULL<<21; // 2MB
    size_t maxKB = std::max(std::max(A.l3KB, A.memKB), std::max(A.l2KB, A.l1KB));
    size_t totalBytes = maxKB*1024ULL + align;
    char* buf1 = (char*)alloc_aligned(totalBytes, align);
    char* buf2 = (char*)alloc_aligned(totalBytes, align);
    if(!buf1 || !buf2){ std::cerr<<"alloc failed\n"; return 1; }

    // touch a bit (fast)
    for(size_t off=0; off<totalBytes; off+=4096) buf1[off]=1, buf2[off]=2;

    std::cout<<"AIDA-like (quick) Cache & Memory Benchmark\n";
    Row mem = benchTier("Memory", A.memKB, buf1, buf2, A.iters, A.stride);
    Row l1  = benchTier("L1", A.l1KB, buf1, buf2, A.iters, A.stride);
    Row l2  = benchTier("L2", A.l2KB, buf1, buf2, A.iters, A.stride);
    Row l3  = benchTier("L3", A.l3KB, buf1, buf2, A.iters, A.stride);

    printRow("Memory", mem);
    printRow("L1", l1);
    printRow("L2", l2);
    printRow("L3", l3);

    free_aligned(buf1); free_aligned(buf2);
    if(sink64==0xDEADBEEF) std::cerr<<"sink\n";
    return 0;
}
