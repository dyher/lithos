# spatial_get_pos()
## NAME
**spatial_get_pos** - get an entity's position

## SYNOPSIS
~~~cxx
float *spatial_get_pos( int handle, int id );
~~~

## DESCRIPTION
Return the position of entity **id** as a float array `{x, y, z}`, or 0 if the entity does not exist in the spatial index **handle**.

## SEE ALSO
[spatial_insert()](spatial_insert.md), [spatial_move()](spatial_move.md)
