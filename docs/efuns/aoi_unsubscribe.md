# aoi_unsubscribe()
## NAME
**aoi_unsubscribe** - unsubscribe an entity from AOI updates

## SYNOPSIS
~~~cxx
void aoi_unsubscribe( int handle, int entity_id );
~~~

## DESCRIPTION
Remove **entity_id** from the AOI manager **handle**. No further deltas will be computed for this entity. No error is raised if the entity was not subscribed.

## SEE ALSO
[aoi_subscribe()](aoi_subscribe.md)
