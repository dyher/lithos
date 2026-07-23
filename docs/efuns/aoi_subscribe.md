# aoi_subscribe()
## NAME
**aoi_subscribe** - subscribe an entity to AOI updates

## SYNOPSIS
~~~cxx
void aoi_subscribe( int handle, int entity_id );
~~~

## DESCRIPTION
Subscribe **entity_id** to receive visibility deltas from the AOI manager **handle**. The entity must already exist in the associated spatial index. Call `aoi_get_delta()` to retrieve the initial set of visible entities.

## SEE ALSO
[aoi_unsubscribe()](aoi_unsubscribe.md), [aoi_get_delta()](aoi_get_delta.md)
