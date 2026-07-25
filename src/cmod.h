#ifndef LITHOS_CMOD_H
#define LITHOS_CMOD_H

#include <stdint.h>

/* Forward declarations - avoid pulling in interpret.h */
typedef void (*cmod_efun_fn)(void);
typedef int (*cmod_init_fn)(void);

/* Load a CMOD shared library. Returns 0 on success, -1 on failure. */
int cmod_load(const char *path);

/* Unload a previously loaded CMOD. */
void cmod_unload(const char *name);

/* List all loaded CMODs. */
void cmod_list(void);

/* Register an efun from a CMOD.
 * name: efun name visible in LPC
 * fn: C function pointer (same signature as native efun)
 * min_args, max_args: argument count bounds
 * Returns 0 on success, -1 on failure. */
int cmod_register_efun(const char *name, cmod_efun_fn fn,
                       int min_args, int max_args);

/* Lookup a CMOD efun by name. Returns NULL if not found. */
cmod_efun_fn cmod_find_efun(const char *name);

/* Initialize CMOD subsystem. */
void cmod_init(void);

#endif /* LITHOS_CMOD_H */
