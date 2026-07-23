#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "src/std.h"
#include "src/interpret.h"
#include "lib/lpc/svalue.h"
#include "lib/lpc/mapping.h"
#include "lib/lpc/array.h"
#include "lib/spatial/spatial.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * AOI (Area of Interest) Manager
 * Built on top of spatial index. Provides per-entity visibility
 * tracking with incremental delta (enter/leave) for MMORPG sync.
 * ============================================================ */

#define AOI_MAX_MANAGERS 64
#define AOI_INITIAL_CAP  256

typedef struct {
    spatial_entity_id_t id;
} aoi_id_entry_t;

typedef struct {
    spatial_handle_t  spatial;
    float             view_radius;
    int               active;

    /* Subscriber set: sorted array for binary search */
    aoi_id_entry_t   *subscribers;
    int               sub_count;
    int               sub_cap;

    /* Per-subscriber previous visible set (flat arrays, indexed by subscriber slot) */
    spatial_entity_id_t **prev_visible;   /* [sub_slot] → sorted id array */
    int                  *prev_count;     /* [sub_slot] → count */
    int                  *prev_cap;       /* [sub_slot] → capacity */
} aoi_manager_t;

static aoi_manager_t g_aoi[AOI_MAX_MANAGERS];
static int g_aoi_init = 0;

static void aoi_ensure_init(void) {
    if (!g_aoi_init) {
        memset(g_aoi, 0, sizeof(g_aoi));
        g_aoi_init = 1;
    }
}

static int aoi_alloc(void) {
    aoi_ensure_init();
    for (int i = 0; i < AOI_MAX_MANAGERS; i++) {
        if (!g_aoi[i].active) return i;
    }
    return -1;
}

static aoi_manager_t *aoi_get(int handle) {
    if (handle < 0 || handle >= AOI_MAX_MANAGERS) return NULL;
    if (!g_aoi[handle].active) return NULL;
    return &g_aoi[handle];
}

/* Binary search in sorted subscriber array, returns slot index or -1 */
static int aoi_find_sub(aoi_manager_t *m, spatial_entity_id_t id) {
    int lo = 0, hi = m->sub_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (m->subscribers[mid].id == id) return mid;
        else if (m->subscribers[mid].id < id) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/* Insert subscriber maintaining sorted order */
static int aoi_add_sub(aoi_manager_t *m, spatial_entity_id_t id) {
    if (m->sub_count >= m->sub_cap) {
        int newcap = m->sub_cap ? m->sub_cap * 2 : AOI_INITIAL_CAP;
        m->subscribers = realloc(m->subscribers, newcap * sizeof(aoi_id_entry_t));
        m->prev_visible = realloc(m->prev_visible, newcap * sizeof(spatial_entity_id_t *));
        m->prev_count = realloc(m->prev_count, newcap * sizeof(int));
        m->prev_cap = realloc(m->prev_cap, newcap * sizeof(int));
        m->sub_cap = newcap;
    }

    /* Find insertion point */
    int pos = m->sub_count;
    for (int i = 0; i < m->sub_count; i++) {
        if (m->subscribers[i].id > id) { pos = i; break; }
        if (m->subscribers[i].id == id) return i; /* already exists */
    }

    /* Shift right */
    if (pos < m->sub_count) {
        memmove(&m->subscribers[pos + 1], &m->subscribers[pos],
                (m->sub_count - pos) * sizeof(aoi_id_entry_t));
        memmove(&m->prev_visible[pos + 1], &m->prev_visible[pos],
                (m->sub_count - pos) * sizeof(spatial_entity_id_t *));
        memmove(&m->prev_count[pos + 1], &m->prev_count[pos],
                (m->sub_count - pos) * sizeof(int));
        memmove(&m->prev_cap[pos + 1], &m->prev_cap[pos],
                (m->sub_count - pos) * sizeof(int));
    }

    m->subscribers[pos].id = id;
    m->prev_visible[pos] = NULL;
    m->prev_count[pos] = 0;
    m->prev_cap[pos] = 0;
    m->sub_count++;
    return pos;
}

static void aoi_remove_sub(aoi_manager_t *m, int slot) {
    free(m->prev_visible[slot]);
    if (slot < m->sub_count - 1) {
        memmove(&m->subscribers[slot], &m->subscribers[slot + 1],
                (m->sub_count - slot - 1) * sizeof(aoi_id_entry_t));
        memmove(&m->prev_visible[slot], &m->prev_visible[slot + 1],
                (m->sub_count - slot - 1) * sizeof(spatial_entity_id_t *));
        memmove(&m->prev_count[slot], &m->prev_count[slot + 1],
                (m->sub_count - slot - 1) * sizeof(int));
        memmove(&m->prev_cap[slot], &m->prev_cap[slot + 1],
                (m->sub_count - slot - 1) * sizeof(int));
    }
    m->sub_count--;
}

/* Compare two sorted id arrays, produce enter/leave lists */
static void aoi_compute_delta(
    const spatial_entity_id_t *old_ids, int old_count,
    const spatial_entity_id_t *new_ids, int new_count,
    spatial_entity_id_t **enter_out, int *enter_count,
    spatial_entity_id_t **leave_out, int *leave_count)
{
    /* Allocate worst case */
    *enter_out = malloc(new_count * sizeof(spatial_entity_id_t));
    *leave_out = malloc(old_count * sizeof(spatial_entity_id_t));
    *enter_count = 0;
    *leave_count = 0;

    int oi = 0, ni = 0;
    while (oi < old_count && ni < new_count) {
        if (old_ids[oi] == new_ids[ni]) { oi++; ni++; }
        else if (old_ids[oi] < new_ids[ni]) { (*leave_out)[(*leave_count)++] = old_ids[oi++]; }
        else { (*enter_out)[(*enter_count)++] = new_ids[ni++]; }
    }
    while (oi < old_count) { (*leave_out)[(*leave_count)++] = old_ids[oi++]; }
    while (ni < new_count) { (*enter_out)[(*enter_count)++] = new_ids[ni++]; }
}

/* Sort helper for query results */
static int id_cmp(const void *a, const void *b) {
    spatial_entity_id_t ia = *(const spatial_entity_id_t *)a;
    spatial_entity_id_t ib = *(const spatial_entity_id_t *)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

/* ============================================================
 * Efun implementations
 * ============================================================ */

#ifdef F_AOI_CREATE
void f_aoi_create(void)
{
    spatial_handle_t sh = (spatial_handle_t)(sp - 1)->u.number;
    float radius = (float)sp->u.real;

    int slot = aoi_alloc();
    if (slot < 0) { pop_n_elems(2); push_number(-1); return; }

    aoi_manager_t *m = &g_aoi[slot];
    m->spatial = sh;
    m->view_radius = radius;
    m->active = 1;
    m->subscribers = NULL;
    m->sub_count = 0;
    m->sub_cap = 0;
    m->prev_visible = NULL;
    m->prev_count = NULL;
    m->prev_cap = NULL;

    pop_n_elems(2);
    push_number(slot);
}
#endif

#ifdef F_AOI_DESTROY
void f_aoi_destroy(void)
{
    int handle = sp->u.number;
    aoi_manager_t *m = aoi_get(handle);
    if (m) {
        for (int i = 0; i < m->sub_count; i++) free(m->prev_visible[i]);
        free(m->subscribers);
        free(m->prev_visible);
        free(m->prev_count);
        free(m->prev_cap);
        memset(m, 0, sizeof(aoi_manager_t));
    }
    pop_stack();
}
#endif

#ifdef F_AOI_SUBSCRIBE
void f_aoi_subscribe(void)
{
    int handle = (sp - 1)->u.number;
    spatial_entity_id_t eid = (spatial_entity_id_t)sp->u.number;
    aoi_manager_t *m = aoi_get(handle);
    int result = 0;
    if (m) {
        int slot = aoi_add_sub(m, eid);
        result = (slot >= 0) ? 1 : 0;
    }
    pop_n_elems(2);
    push_number(result);
}
#endif

#ifdef F_AOI_UNSUBSCRIBE
void f_aoi_unsubscribe(void)
{
    int handle = (sp - 1)->u.number;
    spatial_entity_id_t eid = (spatial_entity_id_t)sp->u.number;
    aoi_manager_t *m = aoi_get(handle);
    int result = 0;
    if (m) {
        int slot = aoi_find_sub(m, eid);
        if (slot >= 0) { aoi_remove_sub(m, slot); result = 1; }
    }
    pop_n_elems(2);
    push_number(result);
}
#endif

#ifdef F_AOI_GET_VISIBLE
void f_aoi_get_visible(void)
{
    int handle = (sp - 1)->u.number;
    spatial_entity_id_t eid = (spatial_entity_id_t)sp->u.number;
    aoi_manager_t *m = aoi_get(handle);

    pop_n_elems(2);

    if (!m) { push_number(0); return; }

    /* Get entity position */
    spatial_point_t pt;
    if (!spatial_get_pos(m->spatial, eid, &pt)) { push_number(0); return; }

    /* Query radius */
    spatial_query_result_t res;
    memset(&res, 0, sizeof(res));
    spatial_query_radius(m->spatial, pt.x, pt.y, m->view_radius, &res);

    /* Build LPC array */
    array_t *arr = allocate_array(res.count);
    for (int i = 0; i < res.count; i++) {
        arr->item[i].type = T_NUMBER;
        arr->item[i].u.number = (long)res.ids[i];
    }
    spatial_free_results(&res);
    push_array(arr);
}
#endif

#ifdef F_AOI_GET_DELTA
void f_aoi_get_delta(void)
{
    int handle = (sp - 1)->u.number;
    spatial_entity_id_t eid = (spatial_entity_id_t)sp->u.number;
    aoi_manager_t *m = aoi_get(handle);

    pop_n_elems(2);

    if (!m) { push_number(0); return; }

    int slot = aoi_find_sub(m, eid);
    if (slot < 0) { push_number(0); return; }

    /* Get current position and query */
    spatial_point_t pt;
    if (!spatial_get_pos(m->spatial, eid, &pt)) { push_number(0); return; }

    spatial_query_result_t res;
    memset(&res, 0, sizeof(res));
    spatial_query_radius(m->spatial, pt.x, pt.y, m->view_radius, &res);

    /* Sort new results for merge comparison */
    qsort(res.ids, res.count, sizeof(spatial_entity_id_t), id_cmp);

    /* Compute delta against previous */
    spatial_entity_id_t *enter_ids, *leave_ids;
    int enter_count, leave_count;
    aoi_compute_delta(
        m->prev_visible[slot], m->prev_count[slot],
        res.ids, res.count,
        &enter_ids, &enter_count,
        &leave_ids, &leave_count);

    /* Update previous visible set */
    if (res.count > m->prev_cap[slot]) {
        m->prev_visible[slot] = realloc(m->prev_visible[slot],
                                         res.count * sizeof(spatial_entity_id_t));
        m->prev_cap[slot] = res.count;
    }
    memcpy(m->prev_visible[slot], res.ids, res.count * sizeof(spatial_entity_id_t));
    m->prev_count[slot] = res.count;
    spatial_free_results(&res);

    /* Build result mapping using shared_string keys (matches f_memory_summary pattern) */
    mapping_t *map = allocate_mapping(2);
    svalue_t key;
    svalue_t *entry;

    SET_SVALUE_SHARED_STRING(&key, make_shared_string("enter", NULL));
    entry = find_for_insert(map, &key, 0);
    if (entry && entry->type == T_NUMBER) {
        array_t *enter_arr = allocate_array(enter_count);
        for (int i = 0; i < enter_count; i++) {
            enter_arr->item[i].type = T_NUMBER;
            enter_arr->item[i].u.number = (long)enter_ids[i];
        }
        entry->type = T_ARRAY;
        entry->u.arr = enter_arr;
    }

    SET_SVALUE_SHARED_STRING(&key, make_shared_string("leave", NULL));
    entry = find_for_insert(map, &key, 0);
    if (entry && entry->type == T_NUMBER) {
        array_t *leave_arr = allocate_array(leave_count);
        for (int i = 0; i < leave_count; i++) {
            leave_arr->item[i].type = T_NUMBER;
            leave_arr->item[i].u.number = (long)leave_ids[i];
        }
        entry->type = T_ARRAY;
        entry->u.arr = leave_arr;
    }

    free(enter_ids);
    free(leave_ids);

    push_refed_mapping(map);

}
#endif
