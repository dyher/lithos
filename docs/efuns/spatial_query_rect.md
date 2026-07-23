# spatial_query_rect()
## NAME
**spatial_query_rect** - query entities within a rectangle

## SYNOPSIS
~~~cxx
int *spatial_query_rect( int handle, mapping bounds );
~~~

## DESCRIPTION
Return an array of entity IDs within the axis-aligned rectangle defined by **bounds**. The mapping supports keys `"min_x"`, `"min_y"`, `"max_x"`, `"max_y"` (all float or int). Missing keys default to 0 for min and 1024 for max.

## EXAMPLE
~~~cxx
int *found = spatial_query_rect(spatial, ([
    "min_x": 100.0, "min_y": 100.0,
    "max_x": 500.0, "max_y": 500.0
]));
~~~

## SEE ALSO
[spatial_query_radius()](spatial_query_radius.md)
