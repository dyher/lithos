# spatial_query_radius()
## NAME
**spatial_query_radius** - query entities within a radius

## SYNOPSIS
~~~cxx
int *spatial_query_radius( int handle, float x, float y, float radius );
~~~

## DESCRIPTION
Return an array of entity IDs within **radius** distance of point (**x**, **y**) in the spatial index **handle**. The query is 2D (Z is ignored). Returns an empty array if no entities are found.

Performance: O(k) where k is the number of cells covered by the radius, plus the entities within those cells.

## EXAMPLE
~~~cxx
int *nearby = spatial_query_radius(spatial, 500.0, 300.0, 100.0);
write("Found " + sizeof(nearby) + " entities nearby.\n");
~~~

## SEE ALSO
[spatial_query_rect()](spatial_query_rect.md), [aoi_get_visible()](aoi_get_visible.md)
