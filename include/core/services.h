#ifndef SERVICES_H
#define SERVICES_H

#include "models.h"

/* Lets the UI tell "blocked by validation", "not signed in", "no such record"
   and "the db call itself failed" apart without knowing any SQL. */
typedef enum { SVC_OK, SVC_INVALID, SVC_DENIED, SVC_NOT_FOUND, SVC_DB_ERROR } svc_result_t;

/* ---- Projects ---- */
svc_result_t project_service_create(const char *name, project_t *out);
int          project_service_list(project_t **out);
svc_result_t project_service_get(const char *id, project_t *out);

/* ---- Issues ---- */
svc_result_t issue_service_create(const char *project_id, const char *title,
                                  const char *desc, issue_t *out);
int          issue_service_list(const char *project_id, issue_t **out);
svc_result_t issue_service_get(const char *id, issue_t *out);
svc_result_t issue_service_set_status(const char *id, issue_status_t status);
svc_result_t issue_service_add_label(const char *issue_id, const char *label_id);
svc_result_t issue_service_add_assignee(const char *issue_id, const char *user_id);
int          issue_service_search(const char *keyword, issue_t **out);
int          issue_service_filter(const char *status, const char *label_id, issue_t **out);

/* ---- Labels ---- */
svc_result_t label_service_create(const char *name, const char *desc, label_t *out);
int          label_service_list(label_t **out);

/* ---- Users ---- */
int          user_service_list(user_t **out);
svc_result_t user_service_get(const char *id, user_t *out);

/* ---- Comments ---- */
svc_result_t comment_service_add(const char *issue_id, const char *text, issue_t *parent_check);
int          comment_service_list(const char *issue_id, comment_t **out);

#endif
