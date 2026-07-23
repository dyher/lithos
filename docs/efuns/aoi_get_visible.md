# aoi_get_visible()
## NAME
**aoi_get_visible** - get all currently visible entities

## SYNOPSIS
~~~cxx
int *aoi_get_visible( int handle, int entity_id );
~~~

## DESCRIPTION
Return an array of all entity IDs currently within the view radius of **entity_id**. Unlike `aoi_get_delta()`, this returns the full set regardless of previous calls. Returns 0 if the entity is not subscribed.

## SEE ALSO
[aoi_get_delta()](aoi_get_delta.md), [spatial_query_radius()](spatial_query_radius.md)
