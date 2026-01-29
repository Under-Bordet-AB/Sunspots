In Linux, you don't actually need a specific "display" syscall. Instead, the kernel provides a virtual window into the system's soul through the **`/proc`** and **`/sys`** filesystems. When you "read" these files, the kernel generates real-time data and hands it to you.

However, if you want direct C functions (which wrap syscalls) to gather health stats, here are the heavy hitters:

### 1. The `sysinfo()` Syscall (The All-in-One)

This is the most direct way to get a high-level health snapshot. It fills a struct with uptime, RAM usage, and "Load Average" (a measurement of CPU pressure).

```c
#include <sys/sysinfo.h>

struct sysinfo si;
if (sysinfo(&si) == 0) {
    long total_ram = si.totalram * si.mem_unit;
    long free_ram = si.freeram * si.mem_unit;
    // Load average (1, 5, and 15 minute intervals)
    double load = si.loads[0] / 65536.0; 
}

```

---

### 2. The `/proc/stat` File (For CPU Usage)

There is no "Get_CPU_Percent()" syscall because CPU usage is a calculation of **time** over an **interval**. To get it, you read `/proc/stat`, wait a second, and read it again.

The first line (starting with `cpu`) gives you the cumulative time the CPU has spent in different states:

* **user:** Normal processes.
* **system:** Kernel processes.
* **idle:** Doing nothing.

**The Calculation:** 

---

### 3. The `getrusage()` Syscall (For Process Health)

If you want to know how much RAM and CPU **your workers** are using specifically (instead of the whole computer), use `getrusage()`.

```c
#include <sys/resource.h>

struct rusage usage;
getrusage(RUSAGE_CHILDREN, &usage);
// usage.ru_maxrss -> Maximum Resident Set Size (RAM used in kilobytes)

```

This is perfect for your supervisor! It tells you if one of your children is leaking memory or "spinning" the CPU.

---

### 4. Stat Summary Table

If you want to build a JSON health object, these are the sources you'll pull from:

| Metric | Source | Frequency |
| --- | --- | --- |
| **RAM Usage** | `sysinfo()` or `/proc/meminfo` | Every 1-5s |
| **CPU Usage** | `/proc/stat` | Delta over 1s |
| **Disk I/O** | `/proc/diskstats` | Delta over 5s |
| **Network** | `/proc/net/dev` | Delta over 1s |
| **Temperature** | `/sys/class/thermal/` | Every 10s |

---

### Pro-Tip: The `/proc/[pid]/` Directory

Since you have the `PID` of your workers in your `watch_table`, you can look at specific health stats for each one:

* `/proc/[PID]/stat`: CPU usage for just that process.
* `/proc/[PID]/status`: Human-readable RAM and thread info.
* `/proc/[PID]/io`: How much data that specific worker is writing to disk.

**Would you like a code snippet that reads `/proc/[PID]/stat` to calculate the CPU percentage of one of your workers so you can include it in your JSON health report?**
