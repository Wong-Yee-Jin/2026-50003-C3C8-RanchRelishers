#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include "models.h"

/* Open the one sqlite connection and build the schema; everything below assumes
   db_init returned true. A false return leaves no connection open, so a caller
   is free to try a different path. db_shutdown closes it at exit. */
bool db_init(const char *path);   // path is a filename or ":memory:"
void db_shutdown(void);

/* Every call below that returns int (the list, search and filter calls) shares
   one convention: the value is how many rows came back, 0 for none, and -1 when
   the query or an allocation failed. An empty table and a broken one are not
   the same answer, which is why the failure case is negative rather than 0.
   On 0 and on -1 alike *out_list is left NULL, so a caller that loops to the
   count and frees the pointer stays correct without checking the sign first.
   A positive count hands back a heap array the caller owns and frees. */

/* ---- Project Management ---- */
/* Projects are the top-level grouping. Creation guards the UNIQUE name, and the
   list feeds the main menu. */
bool db_project_name_exists(const char *name);
bool db_project_create(const char *name, project_t *out);
int  db_project_list(project_t **out_list);
bool db_project_find_by_id(const char *id, project_t *out);

/* ---- Issue Management ---- */
/* Issues live under a project. find_by_id also loads labels and assignees;
   search and filter bind every user value rather than splicing it into SQL. */
bool db_issue_create(const char *project_id, const char *title, const char *description, issue_t *out);
bool db_issue_find_by_id(const char *id, issue_t *out);
bool db_issue_set_status(const char *id, issue_status_t new_status);
int  db_issue_list_by_project(const char *project_id, issue_t **out_list);
int  db_issue_search(const char *project_id, const char *keyword, int limit, issue_t **out_list);
int  db_issue_filter(const char *project_id, const char *status_filter, const char *label_id_filter, int limit, issue_t **out_list);

/* ---- Comments ---- */
/* Free-text notes on an issue, listed in the order they were added. */
bool db_comment_add(const char *issue_id, const char *text);
int  db_comment_list_by_issue(const char *issue_id, comment_t **out_list);

/* ---- Label Management ---- */
/* Labels are shared tags. seed installs the default bug/feature/question set on
   an empty table. */
bool db_label_name_exists(const char *name);
bool db_label_create(const char *name, const char *description, label_t *out);
int  db_label_list(label_t **out_list);
bool db_label_find_by_id(const char *id, label_t *out);
bool db_issue_assign_label(const char *issue_id, const char *label_id);
void db_labels_seed(void);

/* ---- Users / Assignees ---- */
/* Users can be assigned to issues. name_exists guards local creation. */
bool db_user_name_exists(const char *username);
bool db_user_create(const char *username, user_t *out);
int  db_user_list(user_t **out_list);
bool db_user_find_by_id(const char *id, user_t *out);
bool db_issue_assign_user(const char *issue_id, const char *user_id);

/* ---- GitHub account linking (register + login) ---- */
/* Find or refresh a user by GitHub id. The upsert re-stores the profile fields
   on each login so a changed name or avatar follows along. */
bool db_user_find_by_github_id(long long github_id, user_t *out);
bool db_user_upsert_github(long long github_id, const char *username, const char *display_name, const char *avatar_url, user_t *out);

#endif
