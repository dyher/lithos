
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "cmod.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define DL_HANDLE HMODULE
#define DL_OPEN(path) LoadLibraryA(path)
#define DL_SYM(h, name) ((void *)GetProcAddress(h, name))
#define DL_CLOSE(h) FreeLibrary(h)
#define DL_ERROR() "LoadLibrary failed"
#else
#include <dlfcn.h>
#define DL_HANDLE void *
#define DL_OPEN(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define DL_SYM(h, name) dlsym(h, name)
#define DL_CLOSE(h) dlclose(h)
#define DL_ERROR() dlerror()
#endif

/* Loaded module tracking */
#define MAX_CMODS 32
typedef struct {
    char        name[128];
    char        path[512];
    DL_HANDLE   handle;
    uint32_t    num_efuns;
} cmod_entry_t;

static cmod_entry_t loaded_cmods[MAX_CMODS];
static uint32_t num_cmods = 0;


/* Efun registration for CMODs.
 * This wraps the driver's internal efun table.
 * For prototype, we store in a local table and expose via apply. */
#define MAX_CMOD_EFUNS 256
typedef struct {
    char          name[64];
    cmod_efun_fn  fn;
    int           min_args;
    int           max_args;
} cmod_efun_entry_t;

static cmod_efun_entry_t cmod_efuns[MAX_CMOD_EFUNS];
static uint32_t num_cmod_efuns = 0;

int cmod_register_efun(const char *name, cmod_efun_fn fn,
                       int min_args, int max_args) {
    if (num_cmod_efuns >= MAX_CMOD_EFUNS) {
        fprintf(stderr, "CMOD: efun pool full\n");
        return -1;
    }
    cmod_efun_entry_t *e = &cmod_efuns[num_cmod_efuns++];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->fn = fn;
    e->min_args = min_args;
    e->max_args = max_args;
    printf("CMOD: registered efun '%s' (args %d-%d)\n", name, min_args, max_args);
    return 0;
}

/* Lookup a CMOD efun by name. Returns NULL if not found. */
cmod_efun_fn cmod_find_efun(const char *name) {
    for (uint32_t i = 0; i < num_cmod_efuns; i++) {
        if (strcmp(cmod_efuns[i].name, name) == 0)
            return cmod_efuns[i].fn;
    }
    return NULL;
}

void cmod_init(void) {
    memset(loaded_cmods, 0, sizeof(loaded_cmods));
    printf("CMOD: loader initialized (max %d modules)\n", MAX_CMODS);
}

int cmod_load(const char *path) {
    if (num_cmods >= MAX_CMODS) {
        fprintf(stderr, "CMOD: module pool full (%d)\n", MAX_CMODS);
        return -1;
    }

    DL_HANDLE handle = DL_OPEN(path);
    if (!handle) {
        fprintf(stderr, "CMOD: failed to load '%s': %s\n", path, DL_ERROR());
        return -1;
    }

    /* Look for init function */
    union { void *obj; cmod_init_fn fn; } cast1;
    cast1.obj = DL_SYM(handle, "cmod_init");
    cmod_init_fn init_fn = cast1.fn;
    if (!init_fn) {
        /* Try alternative names */
        cast1.obj = DL_SYM(handle, "module_init");
        init_fn = cast1.fn;
    }
    if (!init_fn) {
        fprintf(stderr, "CMOD: no init function found in '%s'\n", path);
        DL_CLOSE(handle);
        return -1;
    }

    /* Call init - it should register efuns via add_efun() or similar */
    int result = init_fn();
    if (result != 0) {
        fprintf(stderr, "CMOD: init failed for '%s' (returned %d)\n", path, result);
        DL_CLOSE(handle);
        return -1;
    }

    /* Extract module name from path */
    const char *basename = strrchr(path, '/');
    if (!basename) basename = strrchr(path, '\\');
    basename = basename ? basename + 1 : path;

    cmod_entry_t *entry = &loaded_cmods[num_cmods++];
    strncpy(entry->name, basename, sizeof(entry->name) - 1);
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->handle = handle;
    entry->num_efuns = 0; /* Will be updated by init_fn registrations */

    printf("CMOD: loaded '%s' from '%s'\n", entry->name, path);
    return 0;
}

void cmod_unload(const char *name) {
    for (uint32_t i = 0; i < num_cmods; i++) {
        if (strcmp(loaded_cmods[i].name, name) == 0) {
            DL_CLOSE(loaded_cmods[i].handle);
            printf("CMOD: unloaded '%s'\n", name);
            /* Shift remaining entries */
            if (i < num_cmods - 1) {
                memmove(&loaded_cmods[i], &loaded_cmods[i+1],
                        (num_cmods - i - 1) * sizeof(cmod_entry_t));
            }
            num_cmods--;
            return;
        }
    }
    fprintf(stderr, "CMOD: module '%s' not found\n", name);
}

void cmod_list(void) {
    printf("CMOD: %u loaded modules:\n", num_cmods);
    for (uint32_t i = 0; i < num_cmods; i++) {
        printf("  [%u] %s (%s)\n", i, loaded_cmods[i].name, loaded_cmods[i].path);
    }
}
