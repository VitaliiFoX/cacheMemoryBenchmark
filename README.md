# 🧠 MacBook Memory Benchmark

Test your **L1**, **L2**, and **L3 cache performance** — as well as **memory bandwidth** — right from your terminal.  
This lightweight benchmark helps you understand how your Mac’s CPU caches handle **Read**, **Write**, and **Copy** operations.

---

## 🚀 Features

- 🔍 Measures **L1 / L2 / L3 cache** and **main memory** performance  
- ⚡ Tests **Read**, **Write**, and **Copy** speeds  
- 💻 Optimized for **Apple Silicon (M1 / M2 / M3)** and **Intel Mac**  
- 📈 Outputs clear performance results with automatic unit scaling  

---

## 🧰 Requirements

- macOS with **g++** (Xcode Command Line Tools)
- C++17 compatible compiler  
- Terminal access  

To install Xcode tools (if needed):

```bash
xcode-select --install
```

---

## 🛠️ Build & Run

Clone or copy the repository, then build and run the benchmark:

```
g++ -O3 -march=native -std=c++17 benchmark.cpp -o benchmark
./benchmark
```
✅ Tip: -O3 enables full optimization; -march=native uses all CPU features available on your Mac.

---

## 📊 Example Output

```
AIDA-like (quick) Cache & Memory Benchmark
Memory    Read 8784.85  MB/s   Write 57156.84 MB/s   Copy 31403.20 MB/s   Latency 165.26 ns
L1        Read 10922.67 MB/s   Write 73691.15 MB/s   Copy 65536.00 MB/s   Latency 1.10   ns
L2        Read 10624.37 MB/s   Write 77672.30 MB/s   Copy 68018.68 MB/s   Latency 6.22   ns
L3        Read 10693.51 MB/s   Write 85670.89 MB/s   Copy 35567.91 MB/s   Latency 75.86  ns
```

---

## 🧪 Use Cases
- Benchmarking after system updates
- Comparing different Mac models
- Evaluating CPU throttling or thermal performance
- Testing effects of memory pressure

---

## 💡 Notes
- Results may vary slightly run to run due to background processes
- Best performance readings are with low system load
- Try closing heavy apps (e.g. Chrome, Xcode) before testing

---

## ❤️ Enjoy & Contribute
Use this tool for fun, testing, or curiosity!
Feel free to open issues or PRs if you want to add new features (like latency tests or threading).
