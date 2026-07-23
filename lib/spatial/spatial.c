// ~/neolith/lib/spatial/spatial.c
#include "spatial.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_INDICES 64
#define CELL_INIT_CAP 8

typedef struct {
    spatial_entity_id_t *ids;
    int count;
    int capacity;
} grid_cell_t;

typedef struct {
    spatial_entity_id_t id;
    float x, y, z;
    int cell_idx;
} entity_entry_t;

typedef struct {
    spatial_type_t type;
    float cell_size;
    int cols, rows;
    grid_cell_t *cells;
    entity_entry_t *entities;
    int entity_count;
    int entity_capacity;
    spatial_entity_id_t *hash_keys;
    int *hash_values;
    int hash_size;
    int hash_count;
    spatial_bounds_t bounds;
} spatial_index_t;

static spatial_index_t *g_indices[MAX_INDICES];
static int g_init = 0;

static void ensure_init(void) {
    if (!g_init) { memset(g_indices, 0, sizeof(g_indices)); g_init = 1; }
}

static int hash_find(spatial_index_t *idx, spatial_entity_id_t id) {
    if (idx->hash_size == 0) return -1;
    uint64_t h = (uint64_t)id % idx->hash_size;
    for (int i = 0; i < idx->hash_size; i++) {
        int s = (int)((h + i) % idx->hash_size);
        if (idx->hash_values[s] == -1) return -1;  /* empty slot */
        if (idx->hash_keys[s] == id) return idx->hash_values[s];
    }
    return -1;
}

static void hash_grow(spatial_index_t *idx) {
    int ns = idx->hash_size ? idx->hash_size * 2 : 256;
    spatial_entity_id_t *nk = calloc(ns, sizeof(spatial_entity_id_t));
    int *nv = malloc(ns * sizeof(int));
    for (int i = 0; i < ns; i++) nv[i] = -1;
    for (int i = 0; i < idx->hash_size; i++) {
        if (idx->hash_keys[i] != 0 || idx->hash_values[i] != -1) {
            uint64_t h = (uint64_t)idx->hash_keys[i] % ns;
            while (nk[h] != 0) h = (h + 1) % ns;
            nk[h] = idx->hash_keys[i];
            nv[h] = idx->hash_values[i];
        }
    }
    free(idx->hash_keys); free(idx->hash_values);
    idx->hash_keys = nk; idx->hash_values = nv; idx->hash_size = ns;
}

static void hash_insert(spatial_index_t *idx, spatial_entity_id_t id, int val) {
    if (idx->hash_count >= idx->hash_size * 3 / 4) hash_grow(idx);
    uint64_t h = (uint64_t)id % idx->hash_size;
    while (idx->hash_keys[h] != 0) h = (h + 1) % idx->hash_size;
    idx->hash_keys[h] = id; idx->hash_values[h] = val; idx->hash_count++;
}

static void hash_remove(spatial_index_t *idx, spatial_entity_id_t id) {
    if (idx->hash_size == 0) return;
    uint64_t h = (uint64_t)id % idx->hash_size;
    for (int i = 0; i < idx->hash_size; i++) {
        int s = (int)((h + i) % idx->hash_size);
        if (idx->hash_keys[s] == id) {
            idx->hash_keys[s] = 0; idx->hash_values[s] = -1; idx->hash_count--; return;
        }
    }
}

static int cell_index(spatial_index_t *idx, float x, float y) {
    int c = (int)((x - idx->bounds.min_x) / idx->cell_size);
    int r = (int)((y - idx->bounds.min_y) / idx->cell_size);
    if (c < 0) c = 0; if (c >= idx->cols) c = idx->cols - 1;
    if (r < 0) r = 0; if (r >= idx->rows) r = idx->rows - 1;
    return r * idx->cols + c;
}

static void cell_add(grid_cell_t *cell, spatial_entity_id_t id) {
    if (cell->count >= cell->capacity) {
        int nc = cell->capacity ? cell->capacity * 2 : CELL_INIT_CAP;
        cell->ids = realloc(cell->ids, nc * sizeof(spatial_entity_id_t));
        cell->capacity = nc;
    }
    cell->ids[cell->count++] = id;
}

static void cell_remove(grid_cell_t *cell, spatial_entity_id_t id) {
    for (int i = 0; i < cell->count; i++) {
        if (cell->ids[i] == id) { cell->ids[i] = cell->ids[--cell->count]; return; }
    }
}

SPATIAL_API spatial_handle_t spatial_create(const spatial_config_t *cfg) {
    ensure_init();
    for (int i = 0; i < MAX_INDICES; i++) {
        if (!g_indices[i]) {
            spatial_index_t *idx = calloc(1, sizeof(spatial_index_t));
            idx->type = cfg->type;
            idx->cell_size = cfg->cell_size > 0 ? cfg->cell_size : 32.0f;
            idx->bounds = cfg->bounds;
            idx->cols = (int)ceilf((cfg->bounds.max_x - cfg->bounds.min_x) / idx->cell_size);
            idx->rows = (int)ceilf((cfg->bounds.max_y - cfg->bounds.min_y) / idx->cell_size);
            if (idx->cols < 1) idx->cols = 1;
            if (idx->rows < 1) idx->rows = 1;
            idx->cells = calloc(idx->cols * idx->rows, sizeof(grid_cell_t));
            int cap = cfg->initial_capacity > 0 ? cfg->initial_capacity : 1024;
            idx->entities = malloc(cap * sizeof(entity_entry_t));
            idx->entity_capacity = cap;
            g_indices[i] = idx;
            return (spatial_handle_t)i;
        }
    }
    return SPATIAL_INVALID_HANDLE;
}

SPATIAL_API void spatial_destroy(spatial_handle_t h) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h]) return;
    spatial_index_t *idx = g_indices[h];
    for (int i = 0; i < idx->cols * idx->rows; i++) free(idx->cells[i].ids);
    free(idx->cells); free(idx->entities);
    free(idx->hash_keys); free(idx->hash_values);
    free(idx); g_indices[h] = NULL;
}

SPATIAL_API int spatial_insert(spatial_handle_t h, spatial_entity_id_t id, float x, float y, float z) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h]) return 0;
    spatial_index_t *idx = g_indices[h];
    if (hash_find(idx, id) >= 0) return spatial_move(h, id, x, y, z);
    if (idx->entity_count >= idx->entity_capacity) {
        idx->entity_capacity *= 2;
        idx->entities = realloc(idx->entities, idx->entity_capacity * sizeof(entity_entry_t));
    }
    int ei = idx->entity_count++;
    idx->entities[ei].id = id; idx->entities[ei].x = x;
    idx->entities[ei].y = y; idx->entities[ei].z = z;
    int ci = cell_index(idx, x, y);
    idx->entities[ei].cell_idx = ci;
    cell_add(&idx->cells[ci], id);
    hash_insert(idx, id, ei);
    return 1;
}

SPATIAL_API int spatial_remove(spatial_handle_t h, spatial_entity_id_t id) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h]) return 0;
    spatial_index_t *idx = g_indices[h];
    int ei = hash_find(idx, id);
    if (ei < 0) return 0;
    cell_remove(&idx->cells[idx->entities[ei].cell_idx], id);
    hash_remove(idx, id);
    int last = --idx->entity_count;
    if (ei < last) {
        idx->entities[ei] = idx->entities[last];
        hash_remove(idx, idx->entities[ei].id);
        hash_insert(idx, idx->entities[ei].id, ei);
    }
    return 1;
}

SPATIAL_API int spatial_move(spatial_handle_t h, spatial_entity_id_t id, float x, float y, float z) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h]) return 0;
    spatial_index_t *idx = g_indices[h];
    int ei = hash_find(idx, id);
    if (ei < 0) return 0;
    int oc = idx->entities[ei].cell_idx;
    int nc = cell_index(idx, x, y);
    if (oc != nc) { cell_remove(&idx->cells[oc], id); cell_add(&idx->cells[nc], id); }
    idx->entities[ei].x = x; idx->entities[ei].y = y;
    idx->entities[ei].z = z; idx->entities[ei].cell_idx = nc;
    return 1;
}

SPATIAL_API int spatial_get_pos(spatial_handle_t h, spatial_entity_id_t id, spatial_point_t *out) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h] || !out) return 0;
    int ei = hash_find(g_indices[h], id);
    if (ei < 0) return 0;
    out->x = g_indices[h]->entities[ei].x;
    out->y = g_indices[h]->entities[ei].y;
    out->z = g_indices[h]->entities[ei].z;
    return 1;
}

SPATIAL_API int spatial_query_radius(spatial_handle_t h, float x, float y, float r, spatial_query_result_t *res) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h] || !res) return 0;
    spatial_index_t *idx = g_indices[h];
    res->count = 0;
    if (res->capacity <= 0) {
        res->capacity = 64;
        res->ids = malloc(res->capacity * sizeof(spatial_entity_id_t));
        res->distances = malloc(res->capacity * sizeof(float));
    }
    float r2 = r * r;
    int c0 = (int)((x - r - idx->bounds.min_x) / idx->cell_size);
    int c1 = (int)((x + r - idx->bounds.min_x) / idx->cell_size);
    int r0 = (int)((y - r - idx->bounds.min_y) / idx->cell_size);
    int r1 = (int)((y + r - idx->bounds.min_y) / idx->cell_size);
    if (c0 < 0) c0 = 0; if (c1 >= idx->cols) c1 = idx->cols - 1;
    if (r0 < 0) r0 = 0; if (r1 >= idx->rows) r1 = idx->rows - 1;
    for (int row = r0; row <= r1; row++) {
        for (int col = c0; col <= c1; col++) {
            grid_cell_t *cell = &idx->cells[row * idx->cols + col];
            for (int i = 0; i < cell->count; i++) {
                int ei = hash_find(idx, cell->ids[i]);
                if (ei < 0) continue;
                float dx = idx->entities[ei].x - x;
                float dy = idx->entities[ei].y - y;
                float d2 = dx * dx + dy * dy;
                if (d2 <= r2) {
                    if (res->count >= res->capacity) {
                        res->capacity *= 2;
                        res->ids = realloc(res->ids, res->capacity * sizeof(spatial_entity_id_t));
                        res->distances = realloc(res->distances, res->capacity * sizeof(float));
                    }
                    res->ids[res->count] = cell->ids[i];
                    res->distances[res->count] = sqrtf(d2);
                    res->count++;
                }
            }
        }
    }
    return res->count;
}

SPATIAL_API int spatial_query_rect(spatial_handle_t h, const spatial_bounds_t *b, spatial_query_result_t *res) {
    if (h < 0 || h >= MAX_INDICES || !g_indices[h] || !res || !b) return 0;
    spatial_index_t *idx = g_indices[h];
    res->count = 0;
    if (res->capacity <= 0) {
        res->capacity = 64;
        res->ids = malloc(res->capacity * sizeof(spatial_entity_id_t));
        res->distances = NULL;
    }
    int c0 = (int)((b->min_x - idx->bounds.min_x) / idx->cell_size);
    int c1 = (int)((b->max_x - idx->bounds.min_x) / idx->cell_size);
    int r0 = (int)((b->min_y - idx->bounds.min_y) / idx->cell_size);
    int r1 = (int)((b->max_y - idx->bounds.min_y) / idx->cell_size);
    if (c0 < 0) c0 = 0; if (c1 >= idx->cols) c1 = idx->cols - 1;
    if (r0 < 0) r0 = 0; if (r1 >= idx->rows) r1 = idx->rows - 1;
    for (int row = r0; row <= r1; row++) {
        for (int col = c0; col <= c1; col++) {
            grid_cell_t *cell = &idx->cells[row * idx->cols + col];
            for (int i = 0; i < cell->count; i++) {
                int ei = hash_find(idx, cell->ids[i]);
                if (ei < 0) continue;
                float ex = idx->entities[ei].x, ey = idx->entities[ei].y;
                if (ex >= b->min_x && ex <= b->max_x && ey >= b->min_y && ey <= b->max_y) {
                    if (res->count >= res->capacity) {
                        res->capacity *= 2;
                        res->ids = realloc(res->ids, res->capacity * sizeof(spatial_entity_id_t));
                    }
                    res->ids[res->count++] = cell->ids[i];
                }
            }
        }
    }
    return res->count;
}

SPATIAL_API void spatial_free_results(spatial_query_result_t *res) {
    if (!res) return;
    free(res->ids); free(res->distances);
    res->ids = NULL; res->distances = NULL;
    res->count = 0; res->capacity = 0;
}

SPATIAL_API void spatial_get_stats(spatial_handle_t h, spatial_stats_t *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    if (h < 0 || h >= MAX_INDICES || !g_indices[h]) return;
    spatial_index_t *idx = g_indices[h];
    st->entity_count = idx->entity_count;
    for (int i = 0; i < idx->cols * idx->rows; i++)
        if (idx->cells[i].count > 0) st->node_count++;
    st->memory_bytes = sizeof(spatial_index_t)
        + idx->cols * idx->rows * sizeof(grid_cell_t)
        + idx->entity_capacity * sizeof(entity_entry_t)
        + idx->hash_size * (sizeof(spatial_entity_id_t) + sizeof(int));
    for (int i = 0; i < idx->cols * idx->rows; i++)
        st->memory_bytes += idx->cells[i].capacity * sizeof(spatial_entity_id_t);
}
