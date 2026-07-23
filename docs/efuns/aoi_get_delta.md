# aoi_get_delta()
## NAME
**aoi_get_delta** - get incremental visibility changes

## SYNOPSIS
~~~cxx
mapping aoi_get_delta( int handle, int entity_id );
~~~

## DESCRIPTION
Compute and return the visibility delta for **entity_id** since the last call. Returns a mapping with two keys:

| Key | Type | Description |
|-----|------|-------------|
| `"enter"` | int * | Entity IDs that entered the view radius |
| `"leave"` | int * | Entity IDs that left the view radius |

On the first call after `aoi_subscribe()`, `"enter"` contains all currently visible entities and `"leave"` is empty. Subsequent calls return only the changes. Returns 0 if the entity is not subscribed.

## EXAMPLE
~~~cxx
mixed delta = aoi_get_delta(aoi, player_id);
if (mapp(delta)) {
    foreach (int id in delta["enter"])
        tell_object(player, "You see " + id + " enter.\n");
    foreach (int id in delta["leave"])
        tell_object(player, id + " has left your view.\n");
}
~~~

## SEE ALSO
[aoi_get_visible()](aoi_get_visible.md), [aoi_subscribe()](aoi_subscribe.md)
