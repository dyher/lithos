# aoi_create()
## NAME
**aoi_create** - create an Area of Interest manager

## SYNOPSIS
~~~cxx
int aoi_create( int spatial_handle, float view_radius );
~~~

## DESCRIPTION
Create a new AOI manager attached to the spatial index **spatial_handle** with the given **view_radius**. Returns a non-negative integer handle on success, or -1 on failure.

The AOI manager tracks subscriptions and computes incremental visibility deltas for each subscriber.

## SEE ALSO
[aoi_destroy()](aoi_destroy.md), [aoi_subscribe()](aoi_subscribe.md), [spatial_create()](spatial_create.md)
