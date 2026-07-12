#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "db.h"

static mongoc_client_t     *g_client   = NULL;
static mongoc_database_t   *g_db       = NULL;
static mongoc_collection_t *g_projects = NULL;
static mongoc_collection_t *g_issues   = NULL;
static mongoc_collection_t *g_labels   = NULL;
static mongoc_collection_t *g_users    = NULL;
static mongoc_collection_t *g_counters = NULL;
static mongoc_collection_t *g_sessions = NULL;

bool db_init(const char *mongo_uri, const char *db_name) {
    mongoc_init();

    bson_error_t error;
    mongoc_uri_t *uri = mongoc_uri_new_with_error(mongo_uri, &error);
    if (!uri) {
        fprintf(stderr, "[db] bad Mongo URI: %s\n", error.message);
        return false;
    }

    g_client = mongoc_client_new_from_uri(uri);
    mongoc_uri_destroy(uri);
    if (!g_client) {
        fprintf(stderr, "[db] failed to create Mongo client\n");
        return false;
    }
    mongoc_client_set_appname(g_client, "mini-gh-tracker");

    g_db       = mongoc_client_get_database(g_client, db_name);
    g_projects = mongoc_client_get_collection(g_client, db_name, "projects");
    g_issues   = mongoc_client_get_collection(g_client, db_name, "issues");
    g_labels   = mongoc_client_get_collection(g_client, db_name, "labels");
    g_users    = mongoc_client_get_collection(g_client, db_name, "users");
    g_counters = mongoc_client_get_collection(g_client, db_name, "counters");
    g_sessions = mongoc_client_get_collection(g_client, db_name, "sessions");

    /* Ping to confirm connectivity */
    bson_t *ping = BCON_NEW("ping", BCON_INT32(1));
    bson_t reply;
    bool ok = mongoc_client_command_simple(g_client, "admin", ping, NULL, &reply, &error);
    bson_destroy(ping);
    bson_destroy(&reply);
    if (!ok) {
        fprintf(stderr, "[db] Mongo ping failed: %s\n", error.message);
        return false;
    }

    printf("[db] connected to MongoDB (%s)\n", db_name);
    return true;
}

void db_shutdown(void) {
    if (g_projects) mongoc_collection_destroy(g_projects);
    if (g_issues)   mongoc_collection_destroy(g_issues);
    if (g_labels)   mongoc_collection_destroy(g_labels);
    if (g_users)    mongoc_collection_destroy(g_users);
    if (g_counters) mongoc_collection_destroy(g_counters);
    if (g_sessions) mongoc_collection_destroy(g_sessions);
    if (g_db)       mongoc_database_destroy(g_db);
    if (g_client)   mongoc_client_destroy(g_client);
    mongoc_cleanup();
}

/* ---------- helpers ---------- */

static void oid_to_str(const bson_oid_t *oid, char out[ID_LEN]) {
    bson_oid_to_string(oid, out);
}

static bool str_to_oid(const char *id_str, bson_oid_t *out) {
    if (!id_str || strlen(id_str) != 24 || !bson_oid_is_valid(id_str, strlen(id_str)))
        return false;
    bson_oid_init_from_string(out, id_str);
    return true;
}

static const char *bson_get_str(const bson_t *doc, const char *key, const char *dflt) {
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_UTF8(&it))
        return bson_iter_utf8(&it, NULL);
    return dflt;
}

static int32_t bson_get_int32(const bson_t *doc, const char *key, int32_t dflt) {
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_INT32(&it))
        return bson_iter_int32(&it);
    return dflt;
}

static int64_t bson_get_int64(const bson_t *doc, const char *key, int64_t dflt) {
    bson_iter_t it;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_INT64(&it))
        return bson_iter_int64(&it);
    return dflt;
}

/* Reads an array-of-OID field (used for label_ids and assignee_ids) into a fixed [max][ID_LEN] buffer, returning how many were copied. */
static int bson_get_oid_array(const bson_t *doc, const char *key, char out[][ID_LEN], int max) {
    bson_iter_t it;
    int n = 0;
    if (bson_iter_init_find(&it, doc, key) && BSON_ITER_HOLDS_ARRAY(&it)) {
        bson_iter_t arr_it;
        bson_iter_recurse(&it, &arr_it);
        while (bson_iter_next(&arr_it) && n < max) {
            if (BSON_ITER_HOLDS_OID(&arr_it))
                oid_to_str(bson_iter_oid(&arr_it), out[n++]);
        }
    }
    return n;
}

static void issue_from_bson(const bson_t *doc, issue_t *out) {
    memset(out, 0, sizeof(*out));
    bson_iter_t it;

    if (bson_iter_init_find(&it, doc, "_id") && BSON_ITER_HOLDS_OID(&it))
        oid_to_str(bson_iter_oid(&it), out->id);

    if (bson_iter_init_find(&it, doc, "project_id") && BSON_ITER_HOLDS_OID(&it))
        oid_to_str(bson_iter_oid(&it), out->project_id);

    out->issue_number = bson_get_int32(doc, "number", 0);

    strncpy(out->title, bson_get_str(doc, "title", ""), TITLE_LEN - 1);
    strncpy(out->description, bson_get_str(doc, "description", ""), DESC_LEN - 1);

    const char *status = bson_get_str(doc, "status", "open");
    out->status = (strcmp(status, "closed") == 0) ? STATUS_CLOSED : STATUS_OPEN;

    out->label_count    = bson_get_oid_array(doc, "label_ids", out->label_ids, MAX_LABELS);
    out->assignee_count = bson_get_oid_array(doc, "assignee_ids", out->assignee_ids, MAX_ASSIGNEES);

    out->estimate_minutes = bson_get_int32(doc, "estimate_minutes", 0);
    out->logged_minutes   = bson_get_int32(doc, "logged_minutes", 0);
}

static void user_from_bson(const bson_t *doc, user_t *out) {
    memset(out, 0, sizeof(*out));
    bson_iter_t it;

    if (bson_iter_init_find(&it, doc, "_id") && BSON_ITER_HOLDS_OID(&it))
        oid_to_str(bson_iter_oid(&it), out->id);

    strncpy(out->username, bson_get_str(doc, "username", ""), USERNAME_LEN - 1);
    strncpy(out->display_name, bson_get_str(doc, "display_name", ""), DISPLAY_NAME_LEN - 1);
    strncpy(out->avatar_url, bson_get_str(doc, "avatar_url", ""), AVATAR_URL_LEN - 1);
    out->github_id = (long long)bson_get_int64(doc, "github_id", 0);
}

/* Atomically increments (creating if needed) the per-project issue
 * sequence counter and returns the new value. 
 * Backs the "#123" style sequential issue numbers, using the same findAndModify($inc) pattern
 * Mongo recommends for atomic counters. */
static int next_issue_number(const bson_oid_t *project_oid) {
    bson_t *query = BCON_NEW("_id", BCON_OID(project_oid));
    bson_t *update = BCON_NEW("$inc", "{", "seq", BCON_INT32(1), "}");

    mongoc_find_and_modify_opts_t *opts = mongoc_find_and_modify_opts_new();
    mongoc_find_and_modify_opts_set_update(opts, update);
    mongoc_find_and_modify_opts_set_flags(opts, MONGOC_FIND_AND_MODIFY_UPSERT | MONGOC_FIND_AND_MODIFY_RETURN_NEW);

    bson_t reply;
    bson_error_t error;
    bool ok = mongoc_collection_find_and_modify_with_opts(g_counters, query, opts, &reply, &error);

    int seq = 0;
    if (ok) {
        bson_iter_t it;
        if (bson_iter_init_find(&it, &reply, "value") && BSON_ITER_HOLDS_DOCUMENT(&it)) {
            uint32_t len; const uint8_t *data;
            bson_iter_document(&it, &len, &data);
            bson_t value_doc;
            bson_init_static(&value_doc, data, len);
            seq = bson_get_int32(&value_doc, "seq", 0);
        }
        bson_destroy(&reply);
    } else {
        fprintf(stderr, "[db] issue counter increment failed: %s\n", error.message);
    }

    bson_destroy(query);
    bson_destroy(update);
    mongoc_find_and_modify_opts_destroy(opts);
    return seq;
}

/* ---------- Projects ---------- */

bool db_project_name_exists(const char *name) {
    bson_t *q = BCON_NEW("name", BCON_UTF8(name));
    int64_t n = mongoc_collection_count_documents(g_projects, q, NULL, NULL, NULL, NULL);
    bson_destroy(q);
    return n > 0;
}

bool db_project_create(const char *name, project_t *out) {
    if (!name || strlen(name) == 0) return false; /* Alternative Flow: invalid name */
    if (db_project_name_exists(name)) return false;

    bson_oid_t oid;
    bson_oid_init(&oid, NULL);
    bson_t *doc = BCON_NEW("_id", BCON_OID(&oid), "name", BCON_UTF8(name));

    bson_error_t error;
    bool ok = mongoc_collection_insert_one(g_projects, doc, NULL, NULL, &error);
    bson_destroy(doc);
    if (!ok) {
        fprintf(stderr, "[db] project insert failed: %s\n", error.message);
        return false;
    }
    oid_to_str(&oid, out->id);
    strncpy(out->name, name, NAME_LEN - 1);
    return true;
}

int db_project_list(project_t **out_list) {
    bson_t *q = bson_new();
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_projects, q, NULL, NULL);

    int cap = 16, n = 0;
    project_t *list = malloc(cap * sizeof(project_t));
    const bson_t *doc;
    while (mongoc_cursor_next(cur, &doc)) {
        if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(project_t)); }
        bson_iter_t it;
        if (bson_iter_init_find(&it, doc, "_id") && BSON_ITER_HOLDS_OID(&it))
            oid_to_str(bson_iter_oid(&it), list[n].id);
        strncpy(list[n].name, bson_get_str(doc, "name", ""), NAME_LEN - 1);
        n++;
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    *out_list = list;
    return n;
}

bool db_project_find_by_id(const char *id, project_t *out) {
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_projects, q, NULL, NULL);
    bool found = mongoc_cursor_next(cur, &doc);
    if (found) {
        strncpy(out->id, id, ID_LEN - 1);
        strncpy(out->name, bson_get_str(doc, "name", ""), NAME_LEN - 1);
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return found;
}

/* ---------- Issues ---------- */

bool db_issue_create(const char *project_id, const char *title,
                      const char *description, issue_t *out) {
    if (!title || strlen(title) == 0) return false; /* Alternative Flow: missing info */

    bson_oid_t proj_oid;
    if (!str_to_oid(project_id, &proj_oid)) return false;

    int number = next_issue_number(&proj_oid);

    bson_oid_t oid;
    bson_oid_init(&oid, NULL);
    bson_t *doc = BCON_NEW(
        "_id", BCON_OID(&oid),
        "project_id", BCON_OID(&proj_oid),
        "number", BCON_INT32(number),
        "title", BCON_UTF8(title),
        "description", BCON_UTF8(description ? description : ""),
        "status", BCON_UTF8("open"),
        "label_ids", "[", "]",
        "assignee_ids", "[", "]",
        "comments", "[", "]",
        "estimate_minutes", BCON_INT32(0),
        "logged_minutes", BCON_INT32(0)
    );

    bson_error_t error;
    bool ok = mongoc_collection_insert_one(g_issues, doc, NULL, NULL, &error);
    bson_destroy(doc);
    if (!ok) {
        fprintf(stderr, "[db] issue insert failed: %s\n", error.message);
        return false;
    }

    memset(out, 0, sizeof(*out));
    oid_to_str(&oid, out->id);
    strncpy(out->project_id, project_id, ID_LEN - 1);
    out->issue_number = number;
    strncpy(out->title, title, TITLE_LEN - 1);
    if (description) strncpy(out->description, description, DESC_LEN - 1);
    out->status = STATUS_OPEN;
    return true;
}

bool db_issue_find_by_id(const char *id, issue_t *out) {
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_issues, q, NULL, NULL);
    bool found = mongoc_cursor_next(cur, &doc);
    if (found) issue_from_bson(doc, out);
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return found;
}

bool db_issue_set_status(const char *id, issue_status_t new_status) {
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    bson_t *update = BCON_NEW("$set", "{",
        "status", BCON_UTF8(new_status == STATUS_CLOSED ? "closed" : "open"),
    "}");
    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    bson_destroy(q);
    bson_destroy(update);
    if (!ok) fprintf(stderr, "[db] status update failed: %s\n", error.message);
    return ok;
}

/* ---- TO BE REMOVED: Time tracking ---- */

bool db_issue_set_estimate(const char *id, int minutes) {
    if (minutes < 0) return false; /* Alternative Flow: invalid duration */
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    bson_t *update = BCON_NEW("$set", "{",
        "estimate_minutes", BCON_INT32(minutes),
    "}");
    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    bson_destroy(q);
    bson_destroy(update);
    if (!ok) fprintf(stderr, "[db] set estimate failed: %s\n", error.message);
    return ok;
}

bool db_issue_log_time(const char *id, int minutes) {
    if (minutes <= 0) return false; /* Alternative Flow: invalid/zero duration */
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    bson_t *update = BCON_NEW("$inc", "{",
        "logged_minutes", BCON_INT32(minutes),
    "}");
    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    bson_destroy(q);
    bson_destroy(update);
    if (!ok) fprintf(stderr, "[db] log time failed: %s\n", error.message);
    return ok;
}

static int issue_query_to_list(bson_t *q, issue_t **out_list) {
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_issues, q, NULL, NULL);
    int cap = 16, n = 0;
    issue_t *list = malloc(cap * sizeof(issue_t));
    const bson_t *doc;
    while (mongoc_cursor_next(cur, &doc)) {
        if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(issue_t)); }
        issue_from_bson(doc, &list[n]);
        n++;
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    *out_list = list;
    return n;
}

int db_issue_list_by_project(const char *project_id, issue_t **out_list) {
    bson_oid_t oid;
    if (!str_to_oid(project_id, &oid)) { *out_list = NULL; return 0; }
    bson_t *q = BCON_NEW("project_id", BCON_OID(&oid));
    return issue_query_to_list(q, out_list);
}

int db_issue_search(const char *keyword, issue_t **out_list) {
    /* case-insensitive substring match on title, per UC10 */
    bson_t *q = bson_new();
    bson_t regex_doc;
    BSON_APPEND_DOCUMENT_BEGIN(q, "title", &regex_doc);
    BSON_APPEND_UTF8(&regex_doc, "$regex", keyword ? keyword : "");
    BSON_APPEND_UTF8(&regex_doc, "$options", "i");
    bson_append_document_end(q, &regex_doc);
    return issue_query_to_list(q, out_list);
}

int db_issue_filter(const char *status_filter, const char *label_id_filter, issue_t **out_list) {
    bson_t *q = bson_new();
    if (status_filter && strlen(status_filter) > 0)
        BSON_APPEND_UTF8(q, "status", status_filter);
    if (label_id_filter && strlen(label_id_filter) > 0) {
        bson_oid_t label_oid;
        if (str_to_oid(label_id_filter, &label_oid))
            BSON_APPEND_OID(q, "label_ids", &label_oid); /* matches if array contains it */
    }
    return issue_query_to_list(q, out_list);
}

/* ---------- Comments (embedded in issue doc) ---------- */

bool db_comment_add(const char *issue_id, const char *text) {
    if (!text || strlen(text) == 0) return false; /* Alternative Flow: empty comment */
    bson_oid_t issue_oid;
    if (!str_to_oid(issue_id, &issue_oid)) return false;

    bson_oid_t comment_oid;
    bson_oid_init(&comment_oid, NULL);

    bson_t *q = BCON_NEW("_id", BCON_OID(&issue_oid));
    bson_t *comment = BCON_NEW("_id", BCON_OID(&comment_oid), "text", BCON_UTF8(text));
    bson_t *update = bson_new();
    bson_t push_doc, comments_doc;
    BSON_APPEND_DOCUMENT_BEGIN(update, "$push", &push_doc);
    BSON_APPEND_DOCUMENT(&push_doc, "comments", comment);
    bson_append_document_end(update, &push_doc);
    (void)comments_doc;

    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    if (!ok) fprintf(stderr, "[db] add comment failed: %s\n", error.message);

    bson_destroy(q);
    bson_destroy(comment);
    bson_destroy(update);
    return ok;
}

int db_comment_list_by_issue(const char *issue_id, comment_t **out_list) {
    bson_oid_t oid;
    if (!str_to_oid(issue_id, &oid)) { *out_list = NULL; return 0; }
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_issues, q, NULL, NULL);

    int n = 0;
    comment_t *list = NULL;
    if (mongoc_cursor_next(cur, &doc)) {
        bson_iter_t it;
        if (bson_iter_init_find(&it, doc, "comments") && BSON_ITER_HOLDS_ARRAY(&it)) {
            bson_iter_t arr_it;
            bson_iter_recurse(&it, &arr_it);
            int cap = 8;
            list = malloc(cap * sizeof(comment_t));
            while (bson_iter_next(&arr_it)) {
                if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(comment_t)); }
                bson_iter_t sub;
                bson_iter_recurse(&arr_it, &sub);
                bson_t sub_doc;
                uint32_t len; const uint8_t *data;
                bson_iter_document(&arr_it, &len, &data);
                bson_init_static(&sub_doc, data, len);
                if (bson_iter_init_find(&sub, &sub_doc, "_id") && BSON_ITER_HOLDS_OID(&sub))
                    oid_to_str(bson_iter_oid(&sub), list[n].id);
                strncpy(list[n].text, bson_get_str(&sub_doc, "text", ""), COMMENT_LEN - 1);
                n++;
            }
        }
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    *out_list = list;
    return n;
}

/* ---------- Labels ---------- */

bool db_label_name_exists(const char *name) {
    bson_t *q = BCON_NEW("name", BCON_UTF8(name));
    int64_t n = mongoc_collection_count_documents(g_labels, q, NULL, NULL, NULL, NULL);
    bson_destroy(q);
    return n > 0;
}

bool db_label_create(const char *name, const char *description, label_t *out) {
    if (!name || strlen(name) == 0) return false;
    if (db_label_name_exists(name)) return false; /* Alternative Flow: duplicate */

    bson_oid_t oid;
    bson_oid_init(&oid, NULL);
    bson_t *doc = BCON_NEW("_id", BCON_OID(&oid), "name", BCON_UTF8(name),
                            "description", BCON_UTF8(description ? description : ""));
    bson_error_t error;
    bool ok = mongoc_collection_insert_one(g_labels, doc, NULL, NULL, &error);
    bson_destroy(doc);
    if (!ok) {
        fprintf(stderr, "[db] label insert failed: %s\n", error.message);
        return false;
    }
    oid_to_str(&oid, out->id);
    strncpy(out->name, name, NAME_LEN - 1);
    strncpy(out->description, description ? description : "", LABEL_DESC_LEN - 1);
    return true;
}

int db_label_list(label_t **out_list) {
    bson_t *q = bson_new();
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_labels, q, NULL, NULL);
    int cap = 16, n = 0;
    label_t *list = malloc(cap * sizeof(label_t));
    const bson_t *doc;
    while (mongoc_cursor_next(cur, &doc)) {
        if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(label_t)); }
        bson_iter_t it;
        if (bson_iter_init_find(&it, doc, "_id") && BSON_ITER_HOLDS_OID(&it))
            oid_to_str(bson_iter_oid(&it), list[n].id);
        strncpy(list[n].name, bson_get_str(doc, "name", ""), NAME_LEN - 1);
        strncpy(list[n].description, bson_get_str(doc, "description", ""), LABEL_DESC_LEN - 1);
        n++;
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    *out_list = list;
    return n;
}

bool db_label_find_by_id(const char *id, label_t *out) {
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_labels, q, NULL, NULL);
    bool found = mongoc_cursor_next(cur, &doc);
    if (found) {
        strncpy(out->id, id, ID_LEN - 1);
        strncpy(out->name, bson_get_str(doc, "name", ""), NAME_LEN - 1);
        strncpy(out->description, bson_get_str(doc, "description", ""), LABEL_DESC_LEN - 1);
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return found;
}

/* Fixed label catalog.
 * db_labels_seed() skips any name that already exists so it's safe to call on every startup. */
static const struct { const char *name; const char *description; } LABEL_CATALOG[] = {
    { "bug",                  "Something isn't working" },
    { "enhancement",          "New feature or request" },
    { "documentation",        "Documentation improvements" },
    { "duplicate",            "Duplicate issue" },
    { "wontfix",              "Will not be worked on" },
    { "priority/low",         "Low priority" },
    { "priority/medium",      "Medium priority" },
    { "priority/high",        "High priority" },
    { "priority/critical",    "Critical priority" },
    { "status/needs-triage",  "Needs triage" },
    { "status/in-progress",   "In progress" },
    { "status/blocked",       "Blocked" },
    { "status/ready-for-review", "Ready for review" },
    { "component/api",        "API component" },
    { "component/ui",         "UI component" },
    { "component/database",   "Database component" },
    { "component/auth",       "Auth component" },
};
#define LABEL_CATALOG_COUNT (int)(sizeof(LABEL_CATALOG) / sizeof(LABEL_CATALOG[0]))

void db_labels_seed(void) {
    label_t tmp;
    int created = 0;
    for (int i = 0; i < LABEL_CATALOG_COUNT; i++) {
        if (db_label_name_exists(LABEL_CATALOG[i].name)) continue;
        if (db_label_create(LABEL_CATALOG[i].name, LABEL_CATALOG[i].description, &tmp))
            created++;
    }
    if (created > 0)
        printf("[db] seeded %d label(s) from the fixed catalog\n", created);
}

bool db_issue_assign_label(const char *issue_id, const char *label_id) {
    bson_oid_t issue_oid, label_oid;
    if (!str_to_oid(issue_id, &issue_oid)) return false;
    if (!str_to_oid(label_id, &label_oid)) return false; /* Alt Flow: label doesn't exist (checked by caller) */

    bson_t *q = BCON_NEW("_id", BCON_OID(&issue_oid));
    bson_t *update = bson_new();
    bson_t addtoset_doc;
    BSON_APPEND_DOCUMENT_BEGIN(update, "$addToSet", &addtoset_doc);
    BSON_APPEND_OID(&addtoset_doc, "label_ids", &label_oid);
    bson_append_document_end(update, &addtoset_doc);

    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    if (!ok) fprintf(stderr, "[db] assign label failed: %s\n", error.message);

    bson_destroy(q);
    bson_destroy(update);
    return ok;
}

/* ---------- Users / Assignees ---------- */

bool db_user_name_exists(const char *username) {
    bson_t *q = BCON_NEW("username", BCON_UTF8(username));
    int64_t n = mongoc_collection_count_documents(g_users, q, NULL, NULL, NULL, NULL);
    bson_destroy(q);
    return n > 0;
}

bool db_user_create(const char *username, user_t *out) {
    if (!username || strlen(username) == 0) return false; /* Alt Flow: invalid username */
    if (db_user_name_exists(username)) return false;       /* Alt Flow: duplicate */

    bson_oid_t oid;
    bson_oid_init(&oid, NULL);
    bson_t *doc = BCON_NEW("_id", BCON_OID(&oid), "username", BCON_UTF8(username));
    bson_error_t error;
    bool ok = mongoc_collection_insert_one(g_users, doc, NULL, NULL, &error);
    bson_destroy(doc);
    if (!ok) {
        fprintf(stderr, "[db] user insert failed: %s\n", error.message);
        return false;
    }
    oid_to_str(&oid, out->id);
    strncpy(out->username, username, USERNAME_LEN - 1);
    return true;
}

int db_user_list(user_t **out_list) {
    bson_t *q = bson_new();
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_users, q, NULL, NULL);
    int cap = 16, n = 0;
    user_t *list = malloc(cap * sizeof(user_t));
    const bson_t *doc;
    while (mongoc_cursor_next(cur, &doc)) {
        if (n == cap) { cap *= 2; list = realloc(list, cap * sizeof(user_t)); }
        user_from_bson(doc, &list[n]);
        n++;
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    *out_list = list;
    return n;
}

bool db_user_find_by_id(const char *id, user_t *out) {
    bson_oid_t oid;
    if (!str_to_oid(id, &oid)) return false;
    bson_t *q = BCON_NEW("_id", BCON_OID(&oid));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_users, q, NULL, NULL);
    bool found = mongoc_cursor_next(cur, &doc);
    if (found) user_from_bson(doc, out);
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return found;
}

/* ---- GitHub account linking (register + login) ---- */

bool db_user_find_by_github_id(long long github_id, user_t *out) {
    if (github_id == 0) return false;
    bson_t *q = BCON_NEW("github_id", BCON_INT64(github_id));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_users, q, NULL, NULL);
    bool found = mongoc_cursor_next(cur, &doc);
    if (found) user_from_bson(doc, out);
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return found;
}

/* Finds the account for this GitHub user id, creating it on first
 * sign-in ("register") or refreshing the cached profile fields on
 * every subsequent sign-in ("login") -- same call either way, matching
 * how "Sign in with GitHub" buttons normally behave. Keyed on the
 * immutable github_id, not username (people rename themselves on
 * GitHub). Uses findAndModify+upsert so it's a single atomic op even
 * under concurrent requests, the same pattern next_issue_number() uses
 * above. */
bool db_user_upsert_github(long long github_id, const char *username, const char *display_name, const char *avatar_url, user_t *out) {
    if (github_id == 0 || !username || strlen(username) == 0) return false;

    bson_t *query  = BCON_NEW("github_id", BCON_INT64(github_id));
    bson_t *update = BCON_NEW("$set", "{",
        "github_id",     BCON_INT64(github_id),
        "username",      BCON_UTF8(username),
        "display_name",  BCON_UTF8(display_name ? display_name : ""),
        "avatar_url",    BCON_UTF8(avatar_url ? avatar_url : ""),
    "}");

    mongoc_find_and_modify_opts_t *opts = mongoc_find_and_modify_opts_new();
    mongoc_find_and_modify_opts_set_update(opts, update);
    mongoc_find_and_modify_opts_set_flags(
        opts, MONGOC_FIND_AND_MODIFY_UPSERT | MONGOC_FIND_AND_MODIFY_RETURN_NEW);

    bson_t reply;
    bson_error_t error;
    bool ok = mongoc_collection_find_and_modify_with_opts(g_users, query, opts, &reply, &error);

    bool got = false;
    if (ok) {
        bson_iter_t it;
        if (bson_iter_init_find(&it, &reply, "value") && BSON_ITER_HOLDS_DOCUMENT(&it)) {
            uint32_t len; const uint8_t *data;
            bson_iter_document(&it, &len, &data);
            bson_t value_doc;
            bson_init_static(&value_doc, data, len);
            user_from_bson(&value_doc, out);
            got = true;
        }
        bson_destroy(&reply);
    } else {
        fprintf(stderr, "[db] github user upsert failed: %s\n", error.message);
    }

    bson_destroy(query);
    bson_destroy(update);
    mongoc_find_and_modify_opts_destroy(opts);
    return got;
}

/* ---- Sessions (first-party login state) ---- */

bool db_session_create(const char *token, const char *user_id, long long expires_at_unix) {
    if (!token || strlen(token) == 0) return false;
    bson_oid_t user_oid;
    if (!str_to_oid(user_id, &user_oid)) return false;

    bson_t *doc = BCON_NEW(
        "_id",        BCON_UTF8(token),
        "user_id",    BCON_OID(&user_oid),
        "expires_at", BCON_INT64(expires_at_unix)
    );
    bson_error_t error;
    bool ok = mongoc_collection_insert_one(g_sessions, doc, NULL, NULL, &error);
    bson_destroy(doc);
    if (!ok) fprintf(stderr, "[db] session insert failed: %s\n", error.message);
    return ok;
}

bool db_session_find_user_id(const char *token, char user_id_out[ID_LEN]) {
    if (!token || strlen(token) == 0) return false;
    bson_t *q = BCON_NEW("_id", BCON_UTF8(token));
    const bson_t *doc;
    mongoc_cursor_t *cur = mongoc_collection_find_with_opts(g_sessions, q, NULL, NULL);
    bool valid = false;
    if (mongoc_cursor_next(cur, &doc)) {
        int64_t expires_at = bson_get_int64(doc, "expires_at", 0);
        bson_iter_t it;
        if (expires_at > (int64_t)time(NULL) &&
            bson_iter_init_find(&it, doc, "user_id") && BSON_ITER_HOLDS_OID(&it)) {
            oid_to_str(bson_iter_oid(&it), user_id_out);
            valid = true;
        }
    }
    mongoc_cursor_destroy(cur);
    bson_destroy(q);
    return valid;
}

bool db_session_delete(const char *token) {
    if (!token || strlen(token) == 0) return false;
    bson_t *q = BCON_NEW("_id", BCON_UTF8(token));
    bson_error_t error;
    bool ok = mongoc_collection_delete_one(g_sessions, q, NULL, NULL, &error);
    bson_destroy(q);
    if (!ok) fprintf(stderr, "[db] session delete failed: %s\n", error.message);
    return ok;
}

bool db_issue_assign_user(const char *issue_id, const char *user_id) {
    bson_oid_t issue_oid, user_oid;
    if (!str_to_oid(issue_id, &issue_oid)) return false;
    if (!str_to_oid(user_id, &user_oid)) return false; /* Alternate Flow: user doesn't exist (checked by caller) */

    bson_t *q = BCON_NEW("_id", BCON_OID(&issue_oid));
    bson_t *update = bson_new();
    bson_t addtoset_doc;
    BSON_APPEND_DOCUMENT_BEGIN(update, "$addToSet", &addtoset_doc);
    BSON_APPEND_OID(&addtoset_doc, "assignee_ids", &user_oid);
    bson_append_document_end(update, &addtoset_doc);

    bson_error_t error;
    bool ok = mongoc_collection_update_one(g_issues, q, update, NULL, NULL, &error);
    if (!ok) fprintf(stderr, "[db] assign user failed: %s\n", error.message);

    bson_destroy(q);
    bson_destroy(update);
    return ok;
}
