# Title Extract Integration

This module is not a standalone runtime plugin. Integrate it at build time.

## Files To Reuse

Copy these files to the other project:

- `include/core/title_extract.h`
- `source/core/title_extract.c`

The module depends on existing core APIs in this repo:

- `core/title.h`
- `core/nca.h`
- `core/pfs.h`
- `core/romfs.h`
- `core/tik.h`
- `core/nxdt_utils.h`

If your other project does not already include these, you must copy/link those modules too.

## Build Wiring (Makefile-style)

Ensure your build includes:

- `source/core/title_extract.c` in C sources.
- `include` in include paths.

Example:

```make
SOURCES += source/core
INCLUDES += include
```

## Usage Example

```c
#include <core/title_extract.h>

void run_extract(u64 app_id)
{
    TitleExtractResult result = {0};

    bool ok = titleExtractMainAndGlobalMetadata(
        app_id,
        "sdmc:/switch/myapp/extracted",
        &result
    );

    // result.main_extracted / result.metadata_extracted
    // result.main_path / result.metadata_path
    // ok == true only if both were extracted
}
```

## Output

For title ID `010099B0235D6000`, expected output paths:

- `sdmc:/switch/myapp/extracted/010099B0235D6000/main`
- `sdmc:/switch/myapp/extracted/010099B0235D6000/global-metadata.dat`
