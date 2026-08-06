#ifndef SERVICES_H
#define SERVICES_H

#include "../models.h"

/* Every mutating service call returns one of these so the UI, or a later
   front end, can react without knowing SQL. SVC_OK is a clean write.
   SVC_INVALID is a rule the input itself broke: a blank name, missing text,
   a duplicate that already exists. SVC_DENIED means nobody is signed in.
   SVC_NOT_FOUND means the id given doesn't point at a live record.
   SVC_DB_ERROR covers everything else, the write failed after the input
   already checked out. */
typedef enum { SVC_OK, SVC_INVALID, SVC_DENIED, SVC_NOT_FOUND, SVC_DB_ERROR } svc_result_t;

/* ---- Projects ----
   Creating a project is the only write; listing and fetching by id are
   open reads. */
svc_result_t project_service_create(const char *name, project_t *out);
int          project_service_list(project_t **out);
svc_result_t project_service_get(const char *id, project_t *out);

/* ---- Issues ----
   Filing an issue and changing its status, labels, or assignees are writes
   and require a signed-in caller. Search and filter are the two read paths:
   search matches a keyword against title and description, filter narrows by
   an exact status and/or label instead. */
svc_result_t issue_service_create(const char *project_id, const char *title,
                                  const char *desc, issue_t *out);
int          issue_service_list(const char *project_id, issue_t **out);
svc_result_t issue_service_get(const char *id, issue_t *out);
svc_result_t issue_service_set_status(const char *id, issue_status_t status);
svc_result_t issue_service_add_label(const char *issue_id, const char *label_id);
svc_result_t issue_service_add_assignee(const char *issue_id, const char *user_id);
int          issue_service_search(const char *project_id, const char *keyword, issue_t **out);
int          issue_service_filter(const char *project_id, const char *status, const char *label_id, issue_t **out);

/* ---- Labels ----
   Labels are shared across every project, so creating one is a write
   guarded by both auth and a uniqueness check; listing them is a read. */
svc_result_t label_service_create(const char *name, const char *desc, label_t *out);
int          label_service_list(label_t **out);

/* ---- Users ----
   Most users arrive through the GitHub login flow, but without a network
   the assignee list needs another way in: create adds a local-only user,
   guarded by auth and a uniqueness check like the other writes here. list
   and get stay plain reads with no auth gate. */
svc_result_t user_service_create(const char *username, user_t *out);
int          user_service_list(user_t **out);
svc_result_t user_service_get(const char *id, user_t *out);

/* ---- Comments ----
   Adding a comment is a write that also confirms the parent issue exists;
   listing them is a read. */
svc_result_t comment_service_add(const char *issue_id, const char *text, issue_t *parent_check);
int          comment_service_list(const char *issue_id, comment_t **out);

#endif
