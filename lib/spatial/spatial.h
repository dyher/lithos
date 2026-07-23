// ~/neolith/lib/spatial/spatial.h
#ifndef NEOLITH_SPATIAL_H
#define NEOLITH_SPATIAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifdef SPATIAL_EXPORTS
    #define SPATIAL_API __declspec(dllexport)
  #else
    #define SPATIAL_API __declspec(dllimport)
  #endif
#else
  #define SPATIAL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPATIAL_GRID = 0,
    SPATIAL_QUADTREE = 1,
    SPATIAL_HASH_GRID = 2
} spatial_type_t;

typedef struct { float x, y, z; } spatial_point_t;
typedef struct { float min_x, min_y, max_x, max_y; } spatial_bounds_t;
typedef int64_t spatial_entity_id_t;
typedef int32_t spatial_handle_t;
#define SPATIAL_INVALID_HANDLE (-1)

typedef struct {
    spatial_type_t type;
    float cell_size;
    int max_depth;
    int node_capacity;
    spatial_bounds_t bounds;
    int initial_capacity;
} spatial_config_t;

typedef struct {
    spatial_entity_id_t *ids;
    float *distances;
    int count;
    int capacity;
} spatial_query_result_t;

typedef struct {
    int entity_count;
    int node_count;
    size_t memory_bytes;
} spatial_stats_t;

SPATIAL_API spatial_handle_t spatial_create(const spatial_config_t *config);
SPATIAL_API void spatial_destroy(spatial_handle_t handle);
SPATIAL_API int spatial_insert(spatial_handle_t h, spatial_entity_id_t id, float x, float y, float z);
SPATIAL_API int spatial_remove(spatial_handle_t h, spatial_entity_id_t id);
SPATIAL_API int spatial_move(spatial_handle_t h, spatial_entity_id_t id, float x, float y, float z);
SPATIAL_API int spatial_get_pos(spatial_handle_t h, spatial_entity_id_t id, spatial_point_t *out);
SPATIAL_API int spatial_query_radius(spatial_handle_t h, float x, float y, float r, spatial_query_result_t *result);
SPATIAL_API int spatial_query_rect(spatial_handle_t h, const spatial_bounds_t *b, spatial_query_result_t *result);
SPATIAL_API void spatial_free_results(spatial_query_result_t *result);
SPATIAL_API void spatial_get_stats(spatial_handle_t h, spatial_stats_t *stats);

#ifdef __cplusplus
}
#endif
#endif
