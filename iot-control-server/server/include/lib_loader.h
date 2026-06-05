#ifndef LIB_LOADER_H
#define LIB_LOADER_H

/*
 * load_all_libraries - Dynamically loads all device .so libraries,
 *                      binds function pointers, and initializes hardware.
 * Returns  0 on success, -1 on any load or init failure.
 */
int load_all_libraries(void);

/*
 * unload_all_libraries - Closes all open library handles.
 *                        Must be called before program exit.
 */
void unload_all_libraries(void);

#endif /* LIB_LOADER_H */
