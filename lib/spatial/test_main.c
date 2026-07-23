#include "spatial.h"
#include <stdio.h>
#include <assert.h>
int main(void) {
    spatial_config_t cfg = {0};
    cfg.type = SPATIAL_GRID; cfg.cell_size = 32.0f;
    cfg.bounds = (spatial_bounds_t){0,0,1024,1024};
    cfg.initial_capacity = 256;
    spatial_handle_t idx = spatial_create(&cfg);
    assert(idx != SPATIAL_INVALID_HANDLE);
    for (int i = 0; i < 100; i++)
        assert(spatial_insert(idx, i, (float)(i*10), (float)((i*7)%1000), 0));
    spatial_query_result_t res = {0};
    int n = spatial_query_radius(idx, 500, 500, 100, &res);
    printf("radius query: %d results\n", n);
    spatial_free_results(&res);
    assert(spatial_move(idx, 50, 500, 500, 0));
    spatial_point_t p;
    assert(spatial_get_pos(idx, 50, &p));
    printf("entity 50 at (%.0f,%.0f)\n", p.x, p.y);
    assert(spatial_remove(idx, 50));
    spatial_stats_t st;
    spatial_get_stats(idx, &st);
    printf("stats: entities=%d cells=%d mem=%zu\n", st.entity_count, st.node_count, st.memory_bytes);
    spatial_destroy(idx);
    printf("ALL TESTS PASSED\n");
    return 0;
}
