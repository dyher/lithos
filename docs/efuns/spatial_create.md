# spatial_create()
## NAME
**spatial_create** - create a spatial index

## SYNOPSIS
~~~cxx
int spatial_create( mapping config );
~~~

## DESCRIPTION
Create a new spatial index with the given configuration. Returns a non-negative integer handle on success, or -1 on failure.

The **config** mapping supports the following keys:

| Key | Type | Description |
|-----|------|-------------|
| `"type"` | string | Index type. Currently only `"grid"` is supported. |
| `"cell_size"` | float \| int | Grid cell size in world units. Default: 64.0 |
| `"min_x"` | float \| int | Minimum X coordinate. Default: 0.0 |
| `"min_y"` | float \| int | Minimum Y coordinate. Default: 0.0 |
| `"max_x"` | float \| int | Maximum X coordinate. Default: 1024.0 |
| `"max_y"` | float \| int | Maximum Y coordinate. Default: 1024.0 |

Coordinates outside `[min, max)` bounds are clamped to valid range.

## EXAMPLE
~~~cxx
mapping cfg = ([
    "type": "grid",
    "cell_size": 64.0,
    "min_x": 0.0, "min_y": 0.0,
    "max_x": 4096.0, "max_y": 4096.0
]);
int spatial = spatial_create(cfg);
~~~

## SEE ALSO
[spatial_destroy()](spatial_destroy.md), [spatial_insert()](spatial_insert.md), [aoi_create()](aoi_create.md)
