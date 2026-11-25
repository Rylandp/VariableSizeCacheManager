# Variable Size Cache Manager

A trace-driven cache simulator written in C that combines variable-size memory allocation with configurable cache replacement policies. The project models a fixed-size memory pool, manages cache entries of different sizes, and supports multithreaded request processing.

## Features

- **Variable-size memory allocation** using a fixed-size memory pool.
- **First Fit and Best Fit** allocation strategies.
- **Block splitting** to use oversized free blocks for smaller allocations.
- **Block coalescing** to merge adjacent free blocks and reduce external fragmentation.
- **FIFO and Clock** cache eviction policies.
- **Hash-table lookup** using a 64-bit FNV-1a hash.
- **Trace-driven workloads** with `GET` and `PUT` requests.
- **Multithreaded request dispatch** using POSIX threads.
- **Event logging and statistics** for hits, misses, evictions, rejected requests, and final occupancy.

## How It Works

The cache manager maintains a fixed-size memory pool and stores variable-sized cache entries inside it. Each pool block has an embedded header containing its payload capacity, allocation state, and a pointer to the next block. The block list is maintained in memory-address order.

When a `PUT` request arrives, the manager attempts to allocate a block using the selected allocation policy. If no suitable free block exists, it evicts cache entries according to the selected eviction policy and retries the allocation. When an entry is removed or updated, its block is freed and adjacent free blocks are coalesced.

A `GET` request looks up the key in the cache's hash table. A successful lookup records a hit and sets the entry's reference bit, which is used by the Clock eviction policy.

### Memory Allocation Policies

| Policy | Behavior |
| --- | --- |
| `first` | First Fit: selects the first free block large enough for the request. |
| `best` | Best Fit: selects the smallest free block that can satisfy the request. |

When a selected block is larger than the request, the allocator splits it if the remainder is large enough to hold another block header and a minimum payload. Small remainders are absorbed into the allocation.

When a block is freed, the allocator merges it with a free successor and then with a free predecessor when either is available.

### Eviction Policies

| Policy | Behavior |
| --- | --- |
| `fifo` | First In, First Out: selects the oldest active cache entry. A `GET` does not change insertion order. |
| `clock` | Clock replacement: examines entries in a circular order. Referenced entries have their reference bit cleared and are given another chance; the first entry encountered with a clear bit is selected. |

The cache uses a hash table for key lookup and a separate doubly linked list to maintain replacement-policy order.

## Build

### Requirements

- GCC or a compatible C compiler
- GNU Make
- POSIX threads
- Python 3 (for the test suite)

From the repository root, build the program with:

```bash
cd cachelab
make
```

This produces the `cachelab` executable in the `cachelab/` directory.

## Run

The command-line interface is:

```bash
./cachelab <pool_size> <trace_file> <num_clients> -a first|best -e fifo|clock
```

Arguments:

| Argument | Description |
| --- | --- |
| `pool_size` | Memory-pool size in bytes, including block headers. |
| `trace_file` | Path to a workload trace file. |
| `num_clients` | Number of worker threads (1–8). |
| `-a first\|best` | Allocation policy. |
| `-e fifo\|clock` | Eviction policy. |

For example, from the `cachelab/` directory:

```bash
./cachelab 1024 test_tr/basic.trace 1 -a first -e fifo
```

To run the same trace with Best Fit allocation and Clock eviction:

```bash
./cachelab 1024 test_tr/basic.trace 4 -a best -e clock
```

The supplied workload traces are in `cachelab/test_tr/`.

## Trace File Format

A trace is a sequence of one request per line:

```text
PUT <key> <size>
GET <key>
```

- `PUT` inserts a key with the requested payload size in bytes. A `PUT` for a key that already exists updates that entry.
- `GET` looks up a key and records a hit or miss.
- Keys must contain 1–64 characters from `A–Z`, `a–z`, `0–9`, `.`, `_`, and `-`.
- `PUT` sizes must be positive decimal integers.
- Blank lines and lines beginning with `#` are ignored.

Example:

```text
# Basic cache workload
PUT key1 100
GET key1
GET missing
PUT key2 200
PUT key1 150
GET key1
```

A request larger than the pool's maximum possible payload capacity is rejected. Updating an existing key with such an oversized request leaves the existing entry intact.

## Output

The program prints an event stream describing cache operations. Events include:

- `PUT` — a write request was processed.
- `HIT` / `MISS` — the result of a lookup.
- `UPDATE` — an existing key is being replaced by a new value.
- `ALLOC` — a pool block was allocated.
- `EVICT` — a cache entry was removed to make room.
- `COALESCE` — adjacent free blocks were merged.
- `REJECT` — a request could not be accepted because it exceeds the pool's maximum payload capacity.

Each event includes the request sequence and client ID. Allocation and eviction events also report relevant memory addresses (as offsets into the pool) and sizes.

At completion, a `STATS` line reports aggregate hits, misses, evictions, rejects, and final requested-byte occupancy relative to the pool size.

## Tests

Run the complete test suite from `cachelab/`:

```bash
make check
```

This runs:

1. **Pool checks** for allocation, splitting, freeing, coalescing, alignment, and First Fit / Best Fit placement.
2. **Cache checks** across both allocation policies and both eviction policies, including updates, oversized requests, trace validation, multithreaded workloads, and stress traces.

To run only the pool tests:

```bash
make pool-check
```

To remove generated object files, executables, and the compiled pool test:

```bash
make clean
```

## Implementation Notes

- `pool.c` implements the memory pool, block allocation, splitting, freeing, and coalescing.
- `cache.c` handles `GET` and `PUT` requests and coordinates cache eviction.
- `index.c` implements the FNV-1a hash index and the doubly linked replacement-policy list.
- `trace.c` parses and validates workload trace files.
- `main.c` handles command-line arguments, worker creation, request dispatch, synchronization, and final statistics.
- `cachelab.h` defines the core data structures, constants, enums, and function interfaces.

Worker threads obtain requests from a shared dispatch queue. Cache operations and event output are protected by mutexes, so shared cache state is handled safely; the request handlers execute while those locks are held.

## Repository Layout

```text
.
├── README.md
└── cachelab/
    ├── Makefile
    ├── cachelab.h
    ├── main.c
    ├── cache.c
    ├── index.c
    ├── pool.c
    ├── trace.c
    ├── test_tr/
    └── tests/
        ├── check.py
        └── pool_check.c
```

## Author

**Ryland Porter**

[GitHub](https://github.com/Rylandp)
