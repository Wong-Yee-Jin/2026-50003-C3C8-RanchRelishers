#ifndef MENU_H
#define MENU_H

/* Trim trailing newline and surrounding spaces in place. Returns s. Split out
   from ui_parse_choice so both it and every fgets() line in the menu loops
   share one tested implementation instead of each screen hand-rolling it. */
char *ui_trim(char *s);

/* Parse a menu selection. Returns the integer, or -1 when the line is not a
   number, so a stray letter or empty Enter press reads as "invalid" rather
   than silently matching choice 0. */
int ui_parse_choice(const char *line);

/* Runs the interactive main menu until the user quits or stdin closes. */
void menu_run(void);

#endif
