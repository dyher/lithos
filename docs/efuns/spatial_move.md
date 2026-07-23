# spatial_move()
## NAME
**spatial_move** - update an entity's position in a spatial index

## SYNOPSIS
~~~cxx
void spatial_move( int handle, int id, float *position );
~~~

## DESCRIPTION
Move the entity **id** to a new **position** in the spatial index **handle**. This is functionally equivalent to `spatial_insert()` but semantically indicates an existing entity is being relocated.

## SEE ALSO
[spatial_insert()](spatial_insert.md), [spatial_get_pos()](spatial_get_pos.md)
