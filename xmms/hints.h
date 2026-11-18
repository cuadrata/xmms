#ifndef HINTS_H
#define HINTS_H

void hint_set_always(gboolean always);
void hint_set_skip_winlist(GtkWidget *window);
void hint_set_sticky(gboolean sticky);
gboolean hint_always_on_top_available(void);
gboolean hint_move_resize_available(void);
void hint_move_resize(GtkWidget *window, int x, int y, gboolean move);

#endif
