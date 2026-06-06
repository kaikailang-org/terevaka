/* terevaka_term.h — prototypes for the terevaka terminal shim.
 * Included via `cc -include c/terevaka_term.h` so the emitted C
 * sees the extern "C" symbols terevaka/term.kai binds. */
#ifndef KAI_TVK_TERM_H
#define KAI_TVK_TERM_H
#include <stdint.h>

int64_t kai_tvk_raw_enable(void);
int64_t kai_tvk_raw_disable(void);
int64_t kai_tvk_poll_key(int64_t timeout_ms);
int64_t kai_tvk_write(const char *s);
int64_t kai_tvk_term_rows(void);
int64_t kai_tvk_term_cols(void);

#endif
