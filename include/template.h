#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <stddef.h>

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} sb_t;

void sb_init(sb_t *sb);
void sb_free(sb_t *sb);
void sb_append(sb_t *sb, const char *text);
void sb_append_escaped(sb_t *sb, const char *text);

char *render_page(const char *title, const char *inner_html);

char *render_landing_page(void);

typedef struct {
    const char *active_project_id;
    const char *active_project_name;
    const char *active_issue_id;
    const char *active_issue_label;
    const char *col2_rows_html;
    const char *col3_html;
    const char *banner_html;
} app_shell_opts_t;

/* Renders the persistent three-column app shell. 
 * Returns a heap string the caller must free(). */
char *render_app_shell(const char *page_title, const app_shell_opts_t *opts);

#endif