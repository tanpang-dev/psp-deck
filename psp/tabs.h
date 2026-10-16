#ifndef DECK_TABS_H
#define DECK_TABS_H

#include <stdint.h>

void tabs_init(void);                              /* before the link thread starts */
void tabs_on_message(const uint8_t *p, int len);   /* link thread: a TABS message */

/* Main thread. */
void tabs_open(void);                              /* show "loading" until TABS arrives */
int tabs_is_open(void);
/* UP/DOWN move (with repeat), LEFT/RIGHT page, CROSS closes. Returns 1 with the tab to jump to on CIRCLE. */
int tabs_update(unsigned pressed, unsigned held, unsigned *window, unsigned *index);
void tabs_draw(void);

#endif
