# Incremental Garbage Collection Design

## Status
**Phase**: Design
**Priority**: P1
**Depends on**: Spatial/AOI (completed)

## 1. Background

### Current Memory Management
Neolith uses **reference counting** as its sole memory management strategy:

- Every `array_t`, `mapping_t`, `object_t`, and `svalue_t` carries a reference count.
- When the count drops to zero, the object is immediately deallocated via `free_array()`, `free_mapping()`, or `free_svalue()`.
- A periodic `reclaim_objects()` pass scans all object variables to clean up dangling references to destructed objects.

### Problems for MMORPG Workloads

| Problem | Impact |
|---------|--------|
| **Cyclic reference leaks** | A->B->A chains are never freed; memory grows unbounded over long uptimes |
| **Ref count overhead** | Every assign/copy requires atomic inc/dec; cache line bouncing on multi-core |
| **Cascade free avalanches** | Releasing one large object triggers recursive chain of frees; unpredictable micro-stalls |
| **O(N) reclaim scan** | `reclaim_objects()` scans all objects linearly; degrades with world size |

### Goals

1. Eliminate cyclic reference leaks without full stop-the-world GC pauses.
2. Bound per-frame GC work to < 2ms at 60 FPS target.
3. Maintain backward compatibility with existing LPC code and efun semantics.
4. Provide foundation for generational GC and fiber-based concurrency.

## 2. Architecture Overview

```text
+---------------------------------------------+
|              LPC Runtime                     |
|  assign_svalue / push_refed / free_svalue    |
|         | write barrier                      |
+---------------------------------------------+
|         Tri-Color Incremental Marker         |
|  White -> Gray -> Black                      |
|  Gray set maintained as explicit worklist     |
|  Fixed step budget per heartbeat             |
+---------------------------------------------+
|            Lazy Sweeper                       |
|  Alloc-time white object reclamation          |
|  No separate sweep phase                      |
+---------------------------------------------+
|       Ref Count (retained as fast path)       |
|  Objects with ref=0 freed immediately         |
|  GC only handles cycles + deferred cleanup    |
+---------------------------------------------+
```

### Hybrid Strategy
We **retain reference counting** as the primary fast-path reclamation mechanism. The incremental GC serves two purposes:

1. **Cycle detection**: Periodically identifies and breaks reference cycles that ref counting cannot handle.
2. **Deferred cleanup**: Replaces cascade-free avalanches with bounded incremental work.

This avoids the "pure GC" pitfall of scanning everything every cycle while still solving the fundamental ref-count limitations.

## 3. Tri-Color Marker

### Color Semantics

| Color | Meaning |
|-------|---------|
| **White** | Not yet reached by marker; candidate for collection if unreachable |
| **Gray** | Reached but children not yet scanned; in the worklist |
| **Black** | Fully scanned; guaranteed reachable from roots |

### Data Structure Changes

Add a 2-bit `gc_color` field to heap-allocated structures:

```c
// In array.h, mapping.h, object.h
struct array_s {
    // ... existing fields ...
    uint8_t gc_color;  // 0=white, 1=gray, 2=black
};
```

Root set includes:
- All live `object_t` variable tables
- Evaluation stack (`sp` to `stack_base`)
- Call-out pending arguments
- FFI bridge held references

### Mark Step Budget

Each heartbeat (or configurable tick), the marker processes at most `GC_MARK_STEPS` gray objects:

```c
#define GC_MARK_STEPS 512  // Tune based on profiling

void gc_incremental_mark(void) {
    int steps = GC_MARK_STEPS;
    while (steps > 0 && gray_list != NULL) {
        gc_object_t *obj = gray_list_pop();
        mark_children(obj);  // Pushes new grays
        obj->gc_color = BLACK;
        steps--;
    }
}
```

### Write Barrier

Any mutation that creates a new reference from a black object to a white object must re-gray the source:

```c
static inline void gc_write_barrier(gc_object_t *src, gc_object_t *dst) {
    if (src->gc_color == BLACK && dst->gc_color == WHITE) {
        src->gc_color = GRAY;
        gray_list_push(src);
    }
}
```

Insertion points:
- `assign_svalue()` when target container is black
- `push_refed_array()` / `push_refed_mapping()` into black containers
- Mapping insert/update when map is black

## 4. Lazy Sweep

Instead of a dedicated sweep phase, white objects are reclaimed **at allocation time**:

```c
void *gc_alloc(size_t size) {
    void *p = try_alloc(size);
    if (!p) {
        gc_lazy_sweep(GC_SWEEP_STEPS);
        p = try_alloc(size);
    }
    if (p) ((gc_header_t *)p)->gc_color = WHITE;
    return p;
}
```

Benefits:
- No separate sweep pause
- Sweep work amortized across allocations
- If no allocation pressure, no sweep needed

## 5. Integration with Existing Ref Counting

### Fast Path (unchanged)
When `ref_count` drops to 0, immediate `free_*()` still occurs. This handles the majority of short-lived objects (delta arrays, query results, temporary strings) without any GC involvement.

### Slow Path (new)
Objects participating in cycles will have `ref_count > 0` even when unreachable. The tri-color marker identifies these during incremental marking. After a complete mark cycle, any remaining white objects with `ref_count > 0` are confirmed cyclic garbage and collected.

### Reclaim Objects Compatibility
The existing `reclaim_objects()` pass continues to run but can be made incremental using the same step-budget mechanism. It specifically handles destructed-object cleanup which is orthogonal to cycle collection.

## 6. Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Max per-frame GC pause | < 2ms | p99 over 1-hour stress test |
| Throughput overhead | < 5% | vs. ref-count-only baseline |
| Cycle collection latency | < 100ms | Time from cycle creation to reclamation |
| Memory overhead | < 10% | Per-object header + gray list |

## 7. Migration Plan

| Phase | Scope | Risk |
|-------|-------|------|
| 1a | Add `gc_color` field + write barrier stubs (no-op) | Low: binary compatible |
| 1b | Implement gray list + incremental mark | Medium: correctness critical |
| 1c | Implement lazy sweep + alloc integration | Medium: allocator interaction |
| 1d | Benchmark + tune step budgets | Low: config only |
| 1e | Enable cycle detection mode | Medium: production validation |

## 8. Future Work

- **Generational GC**: Separate young/old spaces; young gen collected frequently with copying collector, old gen uses tri-color incremental.
- **GC Safe Points**: Define safe points for fiber yield; enable cooperative multitasking.
- **JIT Safe Point Map**: Emit GC metadata alongside JIT-compiled code for precise root enumeration.

## References

- [Dijkstra et al., "On-the-fly Garbage Collection" (1978)](https://dl.acm.org/doi/10.1145/359581.359588)
- [Yuasa, "Real-Time Garbage Collection on General-Purpose Machines" (1990)](https://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/Yuasa.pdf)
- DGD lpc-ext GC implementation
- Pike GC source (`gc.c`, `gc.h`)
