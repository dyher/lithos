# 增量垃圾回收設計文件

## 狀態
**階段**: 設計中
**優先級**: P1
**依賴**: Spatial/AOI（已完成）

## 1. 背景

### 當前記憶體管理機制
Neolith 使用**引用計數（Reference Counting）**作為唯一的記憶體管理策略：

- 每個 `array_t`、`mapping_t`、`object_t` 和 `svalue_t` 都帶有引用計數。
- 當計數歸零時，透過 `free_array()`、`free_mapping()` 或 `free_svalue()` 立即釋放。
- 週期性的 `reclaim_objects()` 遍歷所有物件變數，清理指向已銷毀物件的懸空引用。

### MMORPG 負載下的問題

| 問題 | 影響 |
|------|------|
| **循環引用洩漏** | A->B->A 鏈永遠不會被釋放；長時間運行記憶體持續增長 |
| **引用計數開銷** | 每次賦值/複製都需要原子增減；多核下快取行彈跳 |
| **級聯釋放雪崩** | 釋放一個大物件觸發遞迴鏈式釋放；不可預測的微停頓 |
| **O(N) 回收掃描** | `reclaim_objects()` 線性掃描所有物件；隨世界規模退化 |

### 目標

1. 消除循環引用洩漏，且不產生全域停止（stop-the-world）GC 暫停。
2. 將每幀 GC 工作量控制在 2ms 以內（60 FPS 目標）。
3. 保持與現有 LPC 程式碼和 efun 語義的向後相容性。
4. 為分代 GC 和基於 fiber 的併發提供基礎。

## 2. 架構概覽

```text
+---------------------------------------------+
|              LPC 執行時環境                   |
|  assign_svalue / push_refed / free_svalue    |
|         | 寫入屏障                            |
+---------------------------------------------+
|         三色增量標記器                         |
|  白 -> 灰 -> 黑                               |
|  灰色集以顯式工作清單維護                       |
|  每個心跳固定步數預算                           |
+---------------------------------------------+
|            延遲清掃器                          |
|  分配時按需回收白色物件                          |
|  無獨立清掃階段                                 |
+---------------------------------------------+
|       引用計數（保留作為快速路徑）               |
|  ref=0 的物件立即釋放                           |
|  GC 僅處理循環 + 延遲清理                       |
+---------------------------------------------+
```

### 混合策略
我們**保留引用計數**作為主要的快速路徑回收機制。增量 GC 承擔兩個職責：

1. **循環偵測**：週期性識別並打破引用計數無法處理的引用循環。
2. **延遲清理**：用有界的增量工作取代級聯釋放雪崩。

這避免了「純 GC」每個週期都要掃描一切的陷阱，同時解決了引用計數的根本限制。

## 3. 三色標記器

### 顏色語義

| 顏色 | 含義 |
|------|------|
| **白（White）** | 尚未被標記器到達；若不可達則為回收候選 |
| **灰（Gray）** | 已到達但子節點尚未掃描；在工作清單中 |
| **黑（Black）** | 已完全掃描；保證從根集合可達 |

### 資料結構變更

在堆分配的結構體中添加 2-bit `gc_color` 欄位：

```c
// 在 array.h, mapping.h, object.h 中
struct array_s {
    // ... 現有欄位 ...
    uint8_t gc_color;  // 0=白, 1=灰, 2=黑
};
```

根集合包括：
- 所有存活 `object_t` 的變數表
- 求值堆疊（`sp` 到 `stack_base`）
- call-out 待處理參數
- FFI 橋接持有的引用

### 標記步數預算

每個心跳（或可配置的 tick），標記器最多處理 `GC_MARK_STEPS` 個灰色物件：

```c
#define GC_MARK_STEPS 512  // 根據效能分析調優

void gc_incremental_mark(void) {
    int steps = GC_MARK_STEPS;
    while (steps > 0 && gray_list != NULL) {
        gc_object_t *obj = gray_list_pop();
        mark_children(obj);  // 推入新的灰色物件
        obj->gc_color = BLACK;
        steps--;
    }
}
```

### 寫入屏障

任何從黑色物件到白色物件的新引用建立，都必須將來源重新標為灰色：

```c
static inline void gc_write_barrier(gc_object_t *src, gc_object_t *dst) {
    if (src->gc_color == BLACK && dst->gc_color == WHITE) {
        src->gc_color = GRAY;
        gray_list_push(src);
    }
}
```

插入點：
- `assign_svalue()` 當目標容器為黑色時
- `push_refed_array()` / `push_refed_mapping()` 推入黑色容器時
- Mapping 插入/更新當 map 為黑色時

## 4. 延遲清掃

不設獨立的清掃階段，白色物件在**分配時**按需回收：

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

優勢：
- 無獨立清掃暫停
- 清掃工作攤銷到各次分配中
- 若無分配壓力，則無需清掃

## 5. 與現有引用計數的整合

### 快速路徑（不變）
當 `ref_count` 歸零時，仍然立即執行 `free_*()`。這處理了大多數短生命週期物件（delta 陣列、查詢結果、臨時字串），完全不涉及 GC。

### 慢速路徑（新增）
參與循環的物件即使不可達，其 `ref_count` 仍大於 0。三色標記器在增量標記過程中識別這些物件。完成一個完整的標記週期後，任何剩餘的白色物件且 `ref_count > 0` 即確認為循環垃圾並予以回收。

### Reclaim Objects 相容性
現有的 `reclaim_objects()` 遍歷繼續運行，但可使用相同的步數預算機制改為增量執行。它專門處理已銷毀物件的清理，與循環回收互不干擾。

## 6. 效能目標

| 指標 | 目標 | 測量方式 |
|------|------|----------|
| 最大每幀 GC 暫停 | < 2ms | 1 小時壓力測試 p99 |
| 吞吐量開銷 | < 5% | 對比純引用計數基準 |
| 循環回收延遲 | < 100ms | 從循環建立到回收的時間 |
| 記憶體開銷 | < 10% | 每物件標頭 + 灰色清單 |

## 7. 遷移計畫

| 階段 | 範圍 | 風險 |
|------|------|------|
| 1a | 添加 `gc_color` 欄位 + 寫入屏障存根（no-op） | 低：二進位相容 |
| 1b | 實作灰色清單 + 增量標記 | 中：正確性關鍵 |
| 1c | 實作延遲清掃 + 分配器整合 | 中：分配器交互作用 |
| 1d | 基準測試 + 調優步數預算 | 低：僅配置調整 |
| 1e | 啟用循環偵測模式 | 中：生產環境驗證 |

## 8. 未來工作

- **分代 GC**：分離年輕代/老年代空間；年輕代頻繁使用複製收集器回收，老年代使用三色增量收集器。
- **GC 安全點**：定義 fiber yield 的安全點；啟用協作式多工。
- **JIT 安全點映射**：在 JIT 編譯程式碼旁發出 GC 元數據，用於精確根枚舉。

## 參考文獻

- [Dijkstra et al., "On-the-fly Garbage Collection" (1978)](https://dl.acm.org/doi/10.1145/359581.359588)
- [Yuasa, "Real-Time Garbage Collection on General-Purpose Machines" (1990)](https://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/Yuasa.pdf)
- DGD lpc-ext GC 實作
- Pike GC 原始碼（`gc.c`, `gc.h`）
