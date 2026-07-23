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

#ifdef F_SPATIAL_CREATE
void f_spatial_create(void)
{
    mapping_t *m = sp->u.map;
    svalue_t *val;
    const char *type_str = "grid";
    float cell_size = 32.0f;
    float min_x = 0, min_y = 0, max_x = 1024, max_y = 1024;

    val = find_string_in_mapping(m, "type");
    if (val && val->type == T_STRING) type_str = SVALUE_STRPTR(val);

    val = find_string_in_mapping(m, "cell_size");
    if (val) { if (val->type == T_REAL) cell_size = (float)val->u.real; else if (val->type == T_NUMBER) cell_size = (float)val->u.number; }

    val = find_string_in_mapping(m, "min_x");
    if (val) { if (val->type == T_REAL) min_x = (float)val->u.real; else if (val->type == T_NUMBER) min_x = (float)val->u.number; }

    val = find_string_in_mapping(m, "min_y");
    if (val) { if (val->type == T_REAL) min_y = (float)val->u.real; else if (val->type == T_NUMBER) min_y = (float)val->u.number; }

    val = find_string_in_mapping(m, "max_x");
    if (val) { if (val->type == T_REAL) max_x = (float)val->u.real; else if (val->type == T_NUMBER) max_x = (float)val->u.number; }

    val = find_string_in_mapping(m, "max_y");
    if (val) { if (val->type == T_REAL) max_y = (float)val->u.real; else if (val->type == T_NUMBER) max_y = (float)val->u.number; }

    spatial_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (strcmp(type_str, "quadtree") == 0) cfg.type = SPATIAL_QUADTREE;
    else if (strcmp(type_str, "hash_grid") == 0) cfg.type = SPATIAL_HASH_GRID;
    else cfg.type = SPATIAL_GRID;
    cfg.cell_size = cell_size;
    cfg.bounds.min_x = min_x; cfg.bounds.min_y = min_y;
    cfg.bounds.max_x = max_x; cfg.bounds.max_y = max_y;
    cfg.initial_capacity = 1024;

    spatial_handle_t h = spatial_create(&cfg);
    pop_stack();
    push_number((long)h);
}
#endif

#ifdef F_SPATIAL_DESTROY
void f_spatial_destroy(void)
{
    spatial_handle_t h = (spatial_handle_t)sp->u.number;
    spatial_destroy(h);
    pop_stack();
}
#endif

#ifdef F_SPATIAL_INSERT
void f_spatial_insert(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 2)->u.number;
    spatial_entity_id_t id = (spatial_entity_id_t)(sp - 1)->u.number;
    float x = 0, y = 0, z = 0;

    if (sp->type == T_ARRAY && sp->u.arr && sp->u.arr->size >= 2) {
        array_t *a = sp->u.arr;
        if (a->item[0].type == T_REAL) x = (float)a->item[0].u.real;
        else if (a->item[0].type == T_NUMBER) x = (float)a->item[0].u.number;
        if (a->item[1].type == T_REAL) y = (float)a->item[1].u.real;
        else if (a->item[1].type == T_NUMBER) y = (float)a->item[1].u.number;
        if (a->size >= 3) {
            if (a->item[2].type == T_REAL) z = (float)a->item[2].u.real;
            else if (a->item[2].type == T_NUMBER) z = (float)a->item[2].u.number;
        }
    }

    int result = spatial_insert(h, id, x, y, z);
    pop_n_elems(3);
    push_number(result);
}
#endif

#ifdef F_SPATIAL_REMOVE
void f_spatial_remove(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 1)->u.number;
    spatial_entity_id_t id = (spatial_entity_id_t)sp->u.number;
    int result = spatial_remove(h, id);
    pop_n_elems(2);
    push_number(result);
}
#endif

#ifdef F_SPATIAL_MOVE
void f_spatial_move(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 2)->u.number;
    spatial_entity_id_t id = (spatial_entity_id_t)(sp - 1)->u.number;
    float x = 0, y = 0, z = 0;

    if (sp->type == T_ARRAY && sp->u.arr && sp->u.arr->size >= 2) {
        array_t *a = sp->u.arr;
        if (a->item[0].type == T_REAL) x = (float)a->item[0].u.real;
        else if (a->item[0].type == T_NUMBER) x = (float)a->item[0].u.number;
        if (a->item[1].type == T_REAL) y = (float)a->item[1].u.real;
        else if (a->item[1].type == T_NUMBER) y = (float)a->item[1].u.number;
        if (a->size >= 3) {
            if (a->item[2].type == T_REAL) z = (float)a->item[2].u.real;
            else if (a->item[2].type == T_NUMBER) z = (float)a->item[2].u.number;
        }
    }

    int result = spatial_move(h, id, x, y, z);
    pop_n_elems(3);
    push_number(result);
}
#endif

#ifdef F_SPATIAL_GET_POS
void f_spatial_get_pos(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 1)->u.number;
    spatial_entity_id_t id = (spatial_entity_id_t)sp->u.number;
    spatial_point_t pt;
    int ok = spatial_get_pos(h, id, &pt);
    pop_n_elems(2);
    if (ok) {
        array_t *arr = allocate_array(3);
        arr->item[0].type = T_REAL; arr->item[0].u.real = (double)pt.x;
        arr->item[1].type = T_REAL; arr->item[1].u.real = (double)pt.y;
        arr->item[2].type = T_REAL; arr->item[2].u.real = (double)pt.z;
        push_array(arr);
    } else {
        push_number(0);
    }
}
#endif

#ifdef F_SPATIAL_QUERY_RADIUS
void f_spatial_query_radius(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 3)->u.number;
    float x = (float)(sp - 2)->u.real;
    float y = (float)(sp - 1)->u.real;
    float r = (float)sp->u.real;

    spatial_query_result_t res;
    memset(&res, 0, sizeof(res));
    spatial_query_radius(h, x, y, r, &res);

    pop_n_elems(4);
    array_t *arr = allocate_array(res.count);
    for (int i = 0; i < res.count; i++) {
        arr->item[i].type = T_NUMBER;
        arr->item[i].u.number = (long)res.ids[i];
    }
    spatial_free_results(&res);
    push_array(arr);
}
#endif

#ifdef F_SPATIAL_QUERY_RECT
void f_spatial_query_rect(void)
{
    spatial_handle_t h = (spatial_handle_t)(sp - 1)->u.number;
    mapping_t *m = sp->u.map;
    svalue_t *val;
    spatial_bounds_t b;
    memset(&b, 0, sizeof(b));
    b.max_x = 1024; b.max_y = 1024;

    val = find_string_in_mapping(m, "min_x");
    if (val) { if (val->type == T_REAL) b.min_x = (float)val->u.real; else if (val->type == T_NUMBER) b.min_x = (float)val->u.number; }

    val = find_string_in_mapping(m, "min_y");
    if (val) { if (val->type == T_REAL) b.min_y = (float)val->u.real; else if (val->type == T_NUMBER) b.min_y = (float)val->u.number; }

    val = find_string_in_mapping(m, "max_x");
    if (val) { if (val->type == T_REAL) b.max_x = (float)val->u.real; else if (val->type == T_NUMBER) b.max_x = (float)val->u.number; }

    val = find_string_in_mapping(m, "max_y");
    if (val) { if (val->type == T_REAL) b.max_y = (float)val->u.real; else if (val->type == T_NUMBER) b.max_y = (float)val->u.number; }

    spatial_query_result_t res;
    memset(&res, 0, sizeof(res));
    spatial_query_rect(h, &b, &res);

    pop_n_elems(2);
    array_t *arr = allocate_array(res.count);
    for (int i = 0; i < res.count; i++) {
        arr->item[i].type = T_NUMBER;
        arr->item[i].u.number = (long)res.ids[i];
    }
    spatial_free_results(&res);
    push_array(arr);
}
#endif

#ifdef F_SPATIAL_STATS
void f_spatial_stats(void)
{
    spatial_handle_t h = (spatial_handle_t)sp->u.number;
    spatial_stats_t st;
    spatial_get_stats(h, &st);
    pop_stack();

    mapping_t *map = allocate_mapping(3);
    add_mapping_pair(map, "entity_count", st.entity_count);
    add_mapping_pair(map, "node_count", st.node_count);
    add_mapping_pair(map, "memory_bytes", (int)st.memory_bytes);

    push_mapping(map);
}
#endif
