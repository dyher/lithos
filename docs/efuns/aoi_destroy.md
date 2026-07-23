# aoi_destroy()
## NAME
**aoi_destroy** - destroy an AOI manager

## SYNOPSIS
~~~cxx
void aoi_destroy( int handle );
~~~

## DESCRIPTION
Destroy the AOI manager **handle** and free all subscription state. The underlying spatial index is not affected.

## SEE ALSO
[aoi_create()](aoi_create.md), [spatial_destroy()](spatial_destroy.md)
