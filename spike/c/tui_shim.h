/* tui_shim.h — declarations for the terevaka terminal shim.
 * Included via `cc -include c/tui_shim.h` so the emitted C sees
 * the prototypes for the extern "C" symbols kaikai binds. */
#ifndef KAI_TUI_SHIM_H
#define KAI_TUI_SHIM_H
#include <stdint.h>

int64_t kai_tui_raw_enable(void);
int64_t kai_tui_raw_disable(void);
int64_t kai_tui_read_key(void);
int64_t kai_tui_poll_key(int64_t timeout_ms);
int64_t kai_tui_write(const char *s);

#endif
