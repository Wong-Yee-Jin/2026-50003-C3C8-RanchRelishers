#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include "models.h"

bool db_init(const char *mongo_uri, const char *db_name);
void db_shutdown(void);

/* ---- Project Management ---- */
bool db_project_name_exists(const char *owner_id, const char *name);
bool db_project_create(const char *owner_id, const char *name, project_t *out);
int  db_project_list(const char *owner_id, project_t **out_list);
bool db_project_find_by_id(const char *id, project_t *out);

/* ---- Issue Management ---- */
bool db_issue_create(const char *project_id, const char *title, const char *description, issue_t *out);
bool db_issue_find_by_id(const char *id, issue_t *out);
bool db_issue_set_status(const char *id, issue_status_t new_status);
int  db_issue_list_by_project(const char *project_id, issue_t **out_list);
int  db_issue_search(const char *keyword, issue_t **out_list);
int  db_issue_filter(const char *status_filter, const char *label_id_filter,issue_t **out_list);

/* ---- Label Management ---- */
bool db_label_name_exists(const char *name);
bool db_label_create(const char *name, const char *description, label_t *out);
int  db_label_list(label_t **out_list);
bool db_label_find_by_id(const char *id, label_t *out);
bool db_issue_assign_label(const char *issue_id, const char *label_id);
void db_labels_seed(void);

/* ---- Users / Assignees ---- */
bool db_user_name_exists(const char *owner_id, const char *username);
bool db_user_create(const char *owner_id, const char *username, user_t *out);
int  db_user_list(const char *owner_id, user_t **out_list);
bool db_user_find_by_id(const char *id, user_t *out);
bool db_issue_assign_user(const char *issue_id, const char *user_id);

/* ---- GitHub account linking (register + login) ---- */
bool db_user_find_by_github_id(long long github_id, user_t *out);
bool db_user_upsert_github(long long github_id, const char *username, const char *display_name, const char *avatar_url, user_t *out);

/* ---- Sessions (first-party login state) ---- */
bool db_session_create(const char *token, const char *user_id, long long expires_at_unix);
bool db_session_find_user_id(const char *token, char user_id_out[ID_LEN]);
bool db_session_delete(const char *token);

#endif
