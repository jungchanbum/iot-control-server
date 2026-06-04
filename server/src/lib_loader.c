#include <stdio.h>
#include <dlfcn.h>

#include "lib_loader.h"
#include "globals.h"

/* ─── Internal: resolve a symbol and print error on failure ────── */
static void *load_sym(void *handle, const char *name)
{
    void *sym = dlsym(handle, name);
    if (!sym)
        fprintf(stderr, "[lib_loader] symbol '%s' not found: %s\n", name, dlerror());
    return sym;
}

/* ─── Per-library loaders ───────────────────────────────────────── */
static int load_led(void)
{
    led_handle = dlopen("./libs/led/libled.so", RTLD_LAZY);
    if (!led_handle) { fprintf(stderr, "[lib_loader] libled.so: %s\n", dlerror()); return -1; }

    control_led = (void (*)(int, const char *))load_sym(led_handle, "control_led_brightness");
    return control_led ? 0 : -1;
}

static int load_buzzer(void)
{
    buzzer_handle = dlopen("./libs/buzzer/libbuzzer.so", RTLD_LAZY);
    if (!buzzer_handle) { fprintf(stderr, "[lib_loader] libbuzzer.so: %s\n", dlerror()); return -1; }

    init_buzzer    = (int  (*)(void))        load_sym(buzzer_handle, "init_buzzer");
    control_buzzer = (void (*)(const char *))load_sym(buzzer_handle, "control_buzzer");
    return (init_buzzer && control_buzzer) ? 0 : -1;
}

static int load_cds(void)
{
    cds_handle = dlopen("./libs/cds/libcds.so", RTLD_LAZY);
    if (!cds_handle) { fprintf(stderr, "[lib_loader] libcds.so: %s\n", dlerror()); return -1; }

    init_cds        = (int         (*)(void))load_sym(cds_handle, "init_cds_sensor");
    get_cds_status  = (const char *(*)(void))load_sym(cds_handle, "get_environment_status");
    get_light_level = (int         (*)(void))load_sym(cds_handle, "get_light_level");
    return (init_cds && get_cds_status && get_light_level) ? 0 : -1;
}

static int load_segment(void)
{
    segment_handle = dlopen("./libs/7seg/lib7seg.so", RTLD_LAZY);
    if (!segment_handle) { fprintf(stderr, "[lib_loader] lib7seg.so: %s\n", dlerror()); return -1; }

    init_segment    = (int  (*)(void))load_sym(segment_handle, "init_segment_display");
    start_countdown = (void (*)(int)) load_sym(segment_handle, "start_segment_countdown");
    return (init_segment && start_countdown) ? 0 : -1;
}

/* ─── Public API ────────────────────────────────────────────────── */
int load_all_libraries(void)
{
    printf("[lib_loader] Loading shared libraries...\n");

    if (load_led()     < 0) return -1;
    if (load_buzzer()  < 0) return -1;
    if (load_cds()     < 0) return -1;
    if (load_segment() < 0) return -1;

    /* Hardware initialization sequence */
    if (init_buzzer()  < 0) { fprintf(stderr, "[lib_loader] buzzer init failed.\n");  return -1; }
    if (init_cds()     < 0) { fprintf(stderr, "[lib_loader] cds init failed.\n");     return -1; }
    if (init_segment() < 0) { fprintf(stderr, "[lib_loader] segment init failed.\n"); return -1; }

    printf("[lib_loader] All libraries loaded and initialized.\n\n");
    return 0;
}

void unload_all_libraries(void)
{
    if (led_handle)     { dlclose(led_handle);     led_handle     = NULL; }
    if (buzzer_handle)  { dlclose(buzzer_handle);  buzzer_handle  = NULL; }
    if (cds_handle)     { dlclose(cds_handle);     cds_handle     = NULL; }
    if (segment_handle) { dlclose(segment_handle); segment_handle = NULL; }
    printf("[lib_loader] All library handles released.\n");
}
