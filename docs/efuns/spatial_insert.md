# spatial_insert()
## NAME
**spatial_insert** - insert an entity into a spatial index

## SYNOPSIS
~~~cxx
void spatial_insert( int handle, int id, float *position );
~~~

## DESCRIPTION
Insert an entity with the given **id** at **position** (array of 3 floats: `{x, y, z}`) into the spatial index **handle**. If an entity with the same **id** already exists, its position is updated.

The Z coordinate is stored but not used for spatial queries (2D indexing).

## EXAMPLE
~~~cxx
spatial_insert(spatial, 1001, ({ 500.0, 300.0, 0.0 }));
~~~

## SEE ALSO
[spatial_move()](spatial_move.md), [spatial_remove()](spatial_remove.md), [spatial_get_pos()](spatial_get_pos.md)
