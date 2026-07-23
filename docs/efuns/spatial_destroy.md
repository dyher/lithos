# spatial_destroy()
## NAME
**spatial_destroy** - destroy a spatial index

## SYNOPSIS
~~~cxx
void spatial_destroy( int handle );
~~~

## DESCRIPTION
Destroy the spatial index identified by **handle** and free all associated memory. All entities in the index are removed. Any AOI managers referencing this spatial index should be destroyed first.

## SEE ALSO
[spatial_create()](spatial_create.md), [aoi_destroy()](aoi_destroy.md)
