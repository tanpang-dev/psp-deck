#ifndef DECK_APPS_H
#define DECK_APPS_H

#include <stdint.h>

void apps_init(void);  /* before the link thread starts */

/* Link thread: one APP message (DESIGN §6.2). The list goes live once its last entry has arrived. */
void apps_on_message(const uint8_t *p, int len);

/* Main thread, once per frame before drawing: takes a freshly received list and frees the old one
 * (safe here because the previous frame has finished on the GPU). */
void apps_frame_begin(void);

int apps_count(void);

/* Main thread: D-pad LEFT/RIGHT move the carousel (with key repeat); returns the pid to bring to the
 * front when CIRCLE was pressed (and use_circle is set), else 0. front_pid comes from STATUS. */
unsigned apps_update(unsigned pressed, unsigned held, int use_circle, unsigned front_pid);

void apps_draw(unsigned front_pid);

#endif
