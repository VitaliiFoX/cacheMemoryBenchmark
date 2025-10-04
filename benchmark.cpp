// fast_cachebench.cpp — quick AIDA-like L1/L2/L3/Memory benchmark
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
using namespace std;
//====================================================
// Aligned memory allocation
//====================================================
static void* alloc_aligned(size_t bytes, size_t align) 
{
    void* p = nullptr;
    if (posix_memalign(&p, align, bytes)) return nullptr;
    return p;
}

static void free_aligned(void* p) 
{
    free(p);
}

//====================================================
// Global variables and types
//====================================================
using clk = chrono::steady_clock;
static volatile uint64_t sink64 = 0;

struct Args 
{
    int iters = 3;                  // repetitions per tier
    size_t stride = 64;             // stride for latency test
    size_t l1KB = 32;               // L1 size in KB
    size_t l2KB = 512;              // L2 size in KB
    size_t l3KB = 8192;             // L3 size in KB
    size_t memKB = 131072;          // DRAM (128 MB default)
    bool quick = false;             // quick mode
};

//====================================================
// Helper functions
//====================================================
static void usage(const char* prog) 
{
    cerr << "Usage: " << prog
              << " [--iters N] [--stride B] [--l1KB N] [--l2KB N] [--l3KB N] "
                 "[--memKB N] [--quick]\n";
}

static bool parse(int argc, char** argv, Args& a)
 {
    for (int i = 1; i < argc; ++i) {
        string s(argv[i]);
        auto need = [&](int n) 
        {
            if (i + n >= argc) 
            {
                usage(argv[0]);
                return false;
            }
            return true;
        };

        if (s == "--iters") { if (!need(1)) return false; a.iters = stoi(argv[++i]); }
        else if (s == "--stride") { if (!need(1)) return false; a.stride = stoull(argv[++i]); }
        else if (s == "--l1KB") { if (!need(1)) return false; a.l1KB = stoull(argv[++i]); }
        else if (s == "--l2KB") { if (!need(1)) return false; a.l2KB = stoull(argv[++i]); }
        else if (s == "--l3KB") { if (!need(1)) return false; a.l3KB = stoull(argv[++i]); }
        else if (s == "--memKB") { if (!need(1)) return false; a.memKB = stoull(argv[++i]); }
        else if (s == "--quick") { a.quick = true; }
        else if (s == "-h" || s == "--help") { usage(argv[0]); return false; }
        else 
        {
            cerr << "Unknown arg: " << s << "\n";
            usage(argv[0]);
            return false;
        }
    }

    if (a.stride == 0) a.stride = sizeof(void*);
    if (a.stride % sizeof(void*) != 0) 
    {
        a.stride = ((a.stride + sizeof(void*) - 1) / sizeof(void*)) * sizeof(void*);
    }
    if (a.quick) 
    {
        a.iters = 2;
        a.memKB = min<size_t>(a.memKB, 65536); // 64 MB in quick mode
    }

    return true;
}

//====================================================
// Benchmark functions (read/write/copy/latency)
//====================================================
static double bw_read_gbs(char* buf, size_t bytes, int iters) 
{
    size_t els = bytes / sizeof(double);
    auto* p = reinterpret_cast<double*>(buf);

    // warmup
    double warm = 0;
    for (size_t i = 0; i < els; i += 64 / sizeof(double)) warm += p[i];
    sink64 ^= (uint64_t)warm;

    double t = 0;
    for (int r = 0; r < iters; ++r) 
    {
        auto t0 = clk::now();
        double sum = 0;
        for (size_t i = 0; i < els; ++i) sum += p[i];
        auto t1 = clk::now();
        t += chrono::duration<double>(t1 - t0).count();
        sink64 ^= (uint64_t)sum;
    }
    t /= iters;
    return bytes / t / 1e9;
}

static double bw_write_gbs(char* buf, size_t bytes, int iters) 
{
    size_t els = bytes / sizeof(double);
    auto* p = reinterpret_cast<double*>(buf);

    // warmup
    for (size_t i = 0; i < els; i += 64 / sizeof(double)) p[i] = (double)i;

    double t = 0;
    for (int r = 0; r < iters; ++r) 
    {
        auto t0 = clk::now();
        for (size_t i = 0; i < els; ++i) p[i] = (double)i;
        auto t1 = clk::now();
        t += chrono::duration<double>(t1 - t0).count();
    }
    t /= iters;
    return bytes / t / 1e9;
}

static double bw_copy_gbs(char* dst, char* src, size_t bytes, int iters) 
{
    memcpy(dst, src, bytes); // warmup
    double t = 0;

    for (int r = 0; r < iters; ++r) 
    {
        auto t0 = clk::now();
        memcpy(dst, src, bytes);
        auto t1 = clk::now();
        t += chrono::duration<double>(t1 - t0).count();
    }
    t /= iters;
    return bytes / t / 1e9;
}

static double latency_ns(char* buf, size_t bytes, size_t stride) 
{
    size_t nodes = max<size_t>(2, bytes / stride);
    vector<size_t> idx(nodes);
    for (size_t i = 0; i < nodes; ++i) idx[i] = i;

    mt19937_64 rng(1234567);
    shuffle(idx.begin(), idx.end(), rng);

    for (size_t i = 0; i + 1 < nodes; ++i) 
    {
        auto slot = reinterpret_cast<char**>(buf + idx[i] * stride);
        *slot = (buf + idx[i + 1] * stride);
    }
    *reinterpret_cast<char**>(buf + idx.back() * stride) = (buf + idx[0] * stride);

    volatile char** p = reinterpret_cast<volatile char**>(buf + idx[0] * stride);

    // warmup
    for (size_t i = 0; i < 2000; ++i) 
    {
        p = (volatile char**)(*p);
    }

    // derefs
    uint64_t derefs = min<uint64_t>(150000, max<uint64_t>(40000, nodes * 8));
    auto t0 = clk::now();
    for (uint64_t i = 0; i < derefs; ++i) 
    {
        p = (volatile char**)(*p);
    }
    auto t1 = clk::now();

    sink64 ^= (uint64_t)(uintptr_t)p;
    double ns = chrono::duration<double>(t1 - t0).count() * 1e9;
    return ns / (double)derefs;
}

//====================================================
// Result row
//====================================================
struct Row 
{
    double 
    r, 
    w, 
    c, 
    l;
};

static Row benchTier(const char* name,
                     size_t KB,
                     char* b1,
                     char* b2,
                     int iters,
                     size_t stride) 
{
    size_t bytes = KB * 1024ULL;
    Row x;
    x.r = bw_read_gbs(b1, bytes, iters);
    x.w = bw_write_gbs(b1, bytes, iters);
    x.c = bw_copy_gbs(b2, b1, bytes, iters);
    x.l = latency_ns(b1, bytes, stride);
    (void)name;
    return x;
}

static void printRow(const char* label, const Row& x) 
{
    auto fmt = [&](double v) 
    {
        bool gb = v >= 1000.0;
        double vv = gb ? v : v * 1000.0;
        const char* u = gb ? "GB/s" : "MB/s";
        cout << setw(8) << fixed << setprecision(2) << vv << " " << u;
    };

    cout << left << setw(8) << label
              << "  Read ";  fmt(x.r);
    cout << "   Write "; fmt(x.w);
    cout << "   Copy ";  fmt(x.c);
    cout << "   Latency " << setw(6)
              << fixed << setprecision(2) << x.l << " ns\n";
}

//====================================================
// Main
//====================================================
int main(int argc, char** argv) 
{
    Args A;
    if (!parse(argc, argv, A)) return 1;

    const size_t align = 1ULL << 21; // 2 MB
    size_t maxKB = max(max(A.l3KB, A.memKB), max(A.l2KB, A.l1KB));
    size_t totalBytes = maxKB * 1024ULL + align;

    char* buf1 = (char*)alloc_aligned(totalBytes, align);
    char* buf2 = (char*)alloc_aligned(totalBytes, align);
    if (!buf1 || !buf2) 
    {
        cerr << "alloc failed\n";
        return 1;
    }

    // warm touch
    for (size_t off = 0; off < totalBytes; off += 4096) 
    {
        buf1[off] = 1;
        buf2[off] = 2;
    }

    cout << "AIDA-like (quick) Cache & Memory Benchmark\n";

    Row mem = benchTier("Memory", A.memKB, buf1, buf2, A.iters, A.stride);
    Row l1  = benchTier("L1", A.l1KB, buf1, buf2, A.iters, A.stride);
    Row l2  = benchTier("L2", A.l2KB, buf1, buf2, A.iters, A.stride);
    Row l3  = benchTier("L3", A.l3KB, buf1, buf2, A.iters, A.stride);

    printRow("Memory", mem);
    printRow("L1", l1);
    printRow("L2", l2);
    printRow("L3", l3);

    free_aligned(buf1);
    free_aligned(buf2);

    if (sink64 == 0xDEADBEEF) 
    {
        cerr << "sink\n";
    }
    return 0;
}
