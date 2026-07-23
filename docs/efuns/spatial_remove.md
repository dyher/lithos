# spatial_remove()
## NAME
**spatial_remove** - remove an entity from a spatial index

## SYNOPSIS
~~~cxx
void spatial_remove( int handle, int id );
~~~

## DESCRIPTION
Remove the entity **id** from the spatial index **handle**. No error is raised if the entity does not exist.

## SEE ALSO
[spatial_insert()](spatial_insert.md), [spatial_destroy()](spatial_destroy.md)
