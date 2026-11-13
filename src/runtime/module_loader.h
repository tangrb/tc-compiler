/* TC 0.0.46 contract: seam 4, module loading (design §7.2).
 *
 * Module resolution is a build/link boundary.  The host compiler resolves
 * import paths during LEX/SYN discovery; the device runtime never resolves
 * imports and never executes this interface.  A freestanding profile therefore
 * supplies tc_module_loader_absent.
 */
#ifndef TC_RUNTIME_MODULE_LOADER_H
#define TC_RUNTIME_MODULE_LOADER_H

typedef enum {
    TC_MODULE_FOUND = 0,
    TC_MODULE_NOT_FOUND,
    TC_MODULE_ERROR /* implementation/API error, not a language diagnostic */
} TcModuleStatus;

typedef struct TcModuleLoader {
    /* Resolve module_name relative to the importing module's path.  out_path is
     * loader-owned and stable for the compilation; the core interns by path. */
    TcModuleStatus (*resolve)(void *ctx, const char *importer_path,
                              const char *module_name, const char **out_path);
    void *ctx;
} TcModuleLoader;

/* Device-side loader: no resolution is performed. */
extern const TcModuleLoader tc_module_loader_absent;

#endif /* TC_RUNTIME_MODULE_LOADER_H */
