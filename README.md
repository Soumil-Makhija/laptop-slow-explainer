# laptop-slow-explainer

A small, read-only macOS command-line tool that explains a short performance
sample using OS counters. It shows CPU contributors, large process memory
footprints, swap-out activity, and processes moving data to/from storage.
It offers things to investigate; it never kills processes or changes settings.

This is the first bounded checkpoint. **Live collection supports macOS only.**
Linux can build and test the analysis engine and run the synthetic demo.
DTrace/eBPF stack profiling and disk-latency attribution remain future work.

## Run

Requires macOS Command Line Tools (`cc`, `make`); Python 3 is used for tests only.
No application dependencies, daemon, accounts, network calls or sudo.

```sh
make
./build/slow-explain --seconds 3
./build/slow-explain --seconds 5 --json > report.json
./build/slow-explain --demo
./build/slow-explain --demo --json
```

See the committed [synthetic example report](examples/demo.txt). It has known
answers: 90% system busy, 300% of one core for `video-export`, 1 MiB swapped out,
and 32 MiB/s reported writes for `backup-worker`. No real process list is included.

Intervals are integer seconds from 1 to 30 (default 3). Collection adds some
overhead beyond that wait. Exit codes: 0 report produced, 1 collection/output
failure, 2 invalid options. A valid report may have incomplete process coverage;
inspect the coverage fields. macOS may deny access to protected processes or a
process may exit during collection. Restrictions are reported, never bypassed.
Some app sandboxes prevent process enumeration entirely; use a normal Terminal.

**Reports contain process names and PIDs.** They stay on stdout unless you save
them. The tool never reads command arguments, file contents, or network traffic,
and never uploads a report. Review a live report before sharing it. The committed
demo and evaluation results use synthetic names only.

## Read the evidence

- **System CPU busy** compares two Mach CPU-tick samples, across all logical
  CPUs. At least 85% busy triggers a possible contention observation, a transparent
  heuristic rather than proof that the CPU caused a slowdown. Low aggregate CPU
  can hide a single-thread bottleneck, a burst between samples, or blocked work.
- **Process CPU** uses the change in user + system CPU nanoseconds, divided by
  that process's own sample interval. Mach timebase conversion is applied to the
  resource counters before analysis (essential on Apple Silicon). 100% means one fully occupied logical CPU;
  multithreaded processes can exceed 100%. This differs from system busy percent.
- **Memory** ranks end-of-sample physical footprints (`ri_phys_footprint`). A big
  footprint is a candidate to inspect, not a memory leak diagnosis or proof that
  it caused swapping. Footprints are not added up into "RAM used". Compression
  is a system point-in-time measurement and does not by itself prove pressure.
- **Swap-outs** use the increase in the kernel's swap-out page counter and the
  actual host page size (including 16 KiB Apple Silicon pages). New swap-outs
  suggest memory pressure may contribute. Zero new swap-outs does not rule out
  pressure; there is no direct pressure-level sensor or swap-in analysis here.
- **Disk** ranks per-process read/write MiB/s from disk accounting deltas.
  These are activity counters, not device utilization, I/O latency, queue depth,
  or proof of disk pressure. Cache behavior and filesystem/kernel accounting
  affect attribution. Zero means no bytes reported; the kernel can also return
  zero when per-task I/O accounting is unavailable. The tool explicitly leaves
  disk bottleneck status unknown.

CPU, memory, and disk lists are ranked independently so the largest memory user
cannot disappear just because its CPU use is low. Text shows up to five in each
list; JSON contains all successfully sampled end-of-interval processes. New or
reused PIDs have unknown rates until a matching earlier lifetime exists. Exited
processes are absent from the final list, so short-lived work can be missed.

The sample is sequential, not an atomic snapshot. Each process gets a monotonic
timestamp for its rate calculation. PID lifetime matching, double-checking
identity during collection, counter-reset detection, bounded process lists, and
explicit unknown values keep common sampling failures from becoming false spikes.
The process limit is 4096 per snapshot; a filled list is marked truncated. Counts
of unavailable samples include permissions, exits and identity races across both
snapshots and can count the same inaccessible process twice.

## Architecture and primary references

`collect_macos.c` reads native APIs; `analyze.c` computes deltas and renders text
or JSON; `main.c` handles bounded sampling and a deterministic synthetic example.
Everything runs in one process with bounded arrays. Analysis uses no subprocesses
or language model. Explanations are explicit rules tied to displayed measurements.
JSON labels its source (`synthetic` or `macos-native`), includes coverage and
nullable rates, and always labels `disk_pressure` as `unknown`. Suggestions are
reviewable text; they are never commands that the tool executes.

The collector uses Mach `host_statistics(HOST_CPU_LOAD_INFO)`,
`host_statistics64(HOST_VM_INFO64)`, `host_page_size`, read-only sysctls, and
libproc's `proc_listpids`, `proc_pidinfo`, and `proc_pid_rusage(RUSAGE_INFO_V2)`.
Some libproc interfaces are SDK-private and can change across macOS versions.

Primary API definitions and semantics:

- [Apple Mach CPU counter definitions](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/host_info.h)
- [Apple VM statistics fields and units](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/vm_statistics.h)
- [Apple process resource accounting fields](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/resource.h)
- [Apple task resource accounting implementation](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/task.c)
- [Apple resource-counter timebase conversion tests](https://github.com/apple-oss-distributions/xnu/blob/main/tests/recount/recount_perf_tests.c)

## Verify

```sh
make test
make sanitize
# macOS only: launches one bounded CPU worker; reports pass/fail without names
python3 tests/live_smoke.py
```

Tests check known-answer counter deltas, multithreaded CPU units, PID reuse,
counter resets, unknown data, sample bounds, output escaping and CLI failures.
AddressSanitizer and UndefinedBehaviorSanitizer exercise the same analysis tests.
CI runs the core/CLI checks on macOS and Linux, plus native collection on macOS.

## Next checkpoints

- Optional explicit DTrace capture with clear privilege failures and no automatic
  escalation; add latency/stack evidence before attributing storage stalls.
- Linux collector with PSI and, separately, an optional eBPF profiling adapter.
- Longer sample windows, short-lived process tracking and additional evidence
  for single-thread stalls, thermal throttling, and GPU workload.

There is no destructive remediation, hidden profiler escalation, persistent
monitoring, file-level attribution, or claim to diagnose every kind of slowdown.
