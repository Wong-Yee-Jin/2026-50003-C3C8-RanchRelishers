#include "db.h"
#include "util.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* One process, one connection. A terminal app has a single user driving it,
   so there is no fork and no cross-process sharing, which is what made the
   old Mongo handle unsafe. */
static sqlite3 *DB = NULL;

static const char *SCHEMA =
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS projects("
    "  id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
    "CREATE TABLE IF NOT EXISTS labels("
    "  id TEXT PRIMARY KEY, name TEXT NOT NULL UNIQUE, description TEXT);"
    "CREATE TABLE IF NOT EXISTS users("
    "  id TEXT PRIMARY KEY, username TEXT, display_name TEXT,"
    "  avatar_url TEXT, github_id INTEGER UNIQUE);"
    "CREATE TABLE IF NOT EXISTS issues("
    "  id TEXT PRIMARY KEY,"
    "  project_id TEXT NOT NULL REFERENCES projects(id),"
    "  issue_number INTEGER NOT NULL, title TEXT NOT NULL,"
    "  description TEXT, status INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS issue_labels("
    "  issue_id TEXT NOT NULL REFERENCES issues(id),"
    "  label_id TEXT NOT NULL REFERENCES labels(id),"
    "  PRIMARY KEY(issue_id, label_id));"
    "CREATE TABLE IF NOT EXISTS issue_assignees("
    "  issue_id TEXT NOT NULL REFERENCES issues(id),"
    "  user_id TEXT NOT NULL REFERENCES users(id),"
    "  PRIMARY KEY(issue_id, user_id));"
    "CREATE TABLE IF NOT EXISTS comments("
    "  id TEXT PRIMARY KEY,"
    "  issue_id TEXT NOT NULL REFERENCES issues(id),"
    "  text TEXT NOT NULL, created_at INTEGER NOT NULL);";

bool db_init(const char *path) {
    if (sqlite3_open(path, &DB) != SQLITE_OK) return false;
    char *err = NULL;
    if (sqlite3_exec(DB, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "schema error: %s\n", err ? err : "?");
        sqlite3_free(err);
        return false;
    }
    return true;
}

void db_shutdown(void) {
    sqlite3_close(DB);
    DB = NULL;
}

/* ---- Project Management ---- */

bool db_project_create(const char *name, project_t *out) {
    memset(out, 0, sizeof(*out));          // memset before copy, per the audit
    if (!id_generate(out->id)) return false;
    snprintf(out->name, sizeof(out->name), "%s", name);

    sqlite3_stmt *st;
    const char *sql = "INSERT INTO projects(id, name) VALUES(?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, out->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, out->name, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool db_project_name_exists(const char *name) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT 1 FROM projects WHERE name=?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    bool exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return exists;
}

bool db_project_find_by_id(const char *id, project_t *out) {
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT id, name FROM projects WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        snprintf(out->id, sizeof(out->id), "%s", sqlite3_column_text(st, 0));
        snprintf(out->name, sizeof(out->name), "%s", sqlite3_column_text(st, 1));
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

int db_project_list(project_t **out_list) {
    *out_list = NULL;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT id, name FROM projects ORDER BY name",
                           -1, &st, NULL) != SQLITE_OK) return 0;
    int cap = 0, n = 0;
    project_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            project_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;   // assign only after realloc succeeds, per the audit
        }
        memset(&arr[n], 0, sizeof(arr[n]));
        snprintf(arr[n].id, sizeof(arr[n].id), "%s", sqlite3_column_text(st, 0));
        snprintf(arr[n].name, sizeof(arr[n].name), "%s", sqlite3_column_text(st, 1));
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

/* ---- Label Management ---- */

bool db_label_name_exists(const char *name) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT 1 FROM labels WHERE name=?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    bool exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return exists;
}

bool db_label_create(const char *name, const char *description, label_t *out) {
    memset(out, 0, sizeof(*out));
    if (!id_generate(out->id)) return false;
    snprintf(out->name, sizeof(out->name), "%s", name);
    snprintf(out->description, sizeof(out->description), "%s", description ? description : "");

    sqlite3_stmt *st;
    const char *sql = "INSERT INTO labels(id, name, description) VALUES(?, ?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, out->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, out->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, out->description, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool db_label_find_by_id(const char *id, label_t *out) {
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT id, name, description FROM labels WHERE id=?",
                           -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        snprintf(out->id, sizeof(out->id), "%s", sqlite3_column_text(st, 0));
        snprintf(out->name, sizeof(out->name), "%s", sqlite3_column_text(st, 1));
        const unsigned char *d = sqlite3_column_text(st, 2);
        snprintf(out->description, sizeof(out->description), "%s", d ? (const char *)d : "");
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

int db_label_list(label_t **out_list) {
    *out_list = NULL;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT id, name, description FROM labels ORDER BY name",
                           -1, &st, NULL) != SQLITE_OK) return 0;
    int cap = 0, n = 0;
    label_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            label_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        memset(&arr[n], 0, sizeof(arr[n]));
        snprintf(arr[n].id, sizeof(arr[n].id), "%s", sqlite3_column_text(st, 0));
        snprintf(arr[n].name, sizeof(arr[n].name), "%s", sqlite3_column_text(st, 1));
        const unsigned char *d = sqlite3_column_text(st, 2);
        snprintf(arr[n].description, sizeof(arr[n].description), "%s", d ? (const char *)d : "");
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

void db_labels_seed(void) {
    /* Only seed an empty table so calling this on every startup stays a no-op
       once the defaults exist. */
    label_t *existing = NULL;
    int n = db_label_list(&existing);
    free(existing);
    if (n > 0) return;
    label_t tmp;
    db_label_create("bug", "", &tmp);
    db_label_create("feature", "", &tmp);
    db_label_create("question", "", &tmp);
}

/* ---- Users / Assignees ---- */

/* The three text columns are nullable for locally created users, so read them
   defensively before copying into the fixed buffers. */
static void user_read_row(sqlite3_stmt *st, user_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", sqlite3_column_text(st, 0));
    const unsigned char *un = sqlite3_column_text(st, 1);
    const unsigned char *dn = sqlite3_column_text(st, 2);
    const unsigned char *av = sqlite3_column_text(st, 3);
    snprintf(out->username, sizeof(out->username), "%s", un ? (const char *)un : "");
    snprintf(out->display_name, sizeof(out->display_name), "%s", dn ? (const char *)dn : "");
    snprintf(out->avatar_url, sizeof(out->avatar_url), "%s", av ? (const char *)av : "");
    out->github_id = sqlite3_column_int64(st, 4);
}

bool db_user_name_exists(const char *username) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "SELECT 1 FROM users WHERE username=?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text(st, 1, username, -1, SQLITE_STATIC);
    bool exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return exists;
}

bool db_user_create(const char *username, user_t *out) {
    memset(out, 0, sizeof(*out));
    if (!id_generate(out->id)) return false;
    snprintf(out->username, sizeof(out->username), "%s", username);

    sqlite3_stmt *st;
    const char *sql = "INSERT INTO users(id, username, display_name, avatar_url, github_id)"
                      " VALUES(?, ?, NULL, NULL, NULL)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, out->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, out->username, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool db_user_find_by_id(const char *id, user_t *out) {
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB,
            "SELECT id, username, display_name, avatar_url, github_id FROM users WHERE id=?",
            -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) { user_read_row(st, out); found = true; }
    sqlite3_finalize(st);
    return found;
}

bool db_user_find_by_github_id(long long github_id, user_t *out) {
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB,
            "SELECT id, username, display_name, avatar_url, github_id FROM users WHERE github_id=?",
            -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, github_id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) { user_read_row(st, out); found = true; }
    sqlite3_finalize(st);
    return found;
}

int db_user_list(user_t **out_list) {
    *out_list = NULL;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB,
            "SELECT id, username, display_name, avatar_url, github_id FROM users ORDER BY username",
            -1, &st, NULL) != SQLITE_OK) return 0;
    int cap = 0, n = 0;
    user_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            user_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        user_read_row(st, &arr[n]);
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

bool db_user_upsert_github(long long github_id, const char *username,
                           const char *display_name, const char *avatar_url, user_t *out) {
    user_t existing;
    if (db_user_find_by_github_id(github_id, &existing)) {
        sqlite3_stmt *st;
        const char *sql = "UPDATE users SET username=?, display_name=?, avatar_url=? WHERE github_id=?";
        if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, username, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, display_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, avatar_url, -1, SQLITE_STATIC);
        sqlite3_bind_int64(st, 4, github_id);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) return false;
        return db_user_find_by_github_id(github_id, out);
    }

    char id[ID_LEN];
    if (!id_generate(id)) return false;
    sqlite3_stmt *st;
    const char *sql = "INSERT INTO users(id, username, display_name, avatar_url, github_id)"
                      " VALUES(?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, display_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, avatar_url, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 5, github_id);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return false;
    return db_user_find_by_github_id(github_id, out);
}

/* ---- Issue Management ---- */

/* Read the six scalar columns of an issue row. Label and assignee arrays are
   left empty here because list views do not need them and filling them would
   cost a follow-up query per row. */
static void issue_read_scalar(sqlite3_stmt *st, issue_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", sqlite3_column_text(st, 0));
    snprintf(out->project_id, sizeof(out->project_id), "%s", sqlite3_column_text(st, 1));
    out->issue_number = sqlite3_column_int(st, 2);
    snprintf(out->title, sizeof(out->title), "%s", sqlite3_column_text(st, 3));
    const unsigned char *d = sqlite3_column_text(st, 4);
    snprintf(out->description, sizeof(out->description), "%s", d ? (const char *)d : "");
    out->status = (issue_status_t)sqlite3_column_int(st, 5);
}

bool db_issue_create(const char *project_id, const char *title,
                     const char *description, issue_t *out) {
    memset(out, 0, sizeof(*out));

    /* Issue numbers run per project from MAX+1 so each project counts from 1
       the way a user reading the menu expects. */
    int number = 1;
    sqlite3_stmt *cst;
    if (sqlite3_prepare_v2(DB,
            "SELECT COALESCE(MAX(issue_number),0)+1 FROM issues WHERE project_id=?",
            -1, &cst, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(cst, 1, project_id, -1, SQLITE_STATIC);
    if (sqlite3_step(cst) == SQLITE_ROW) number = sqlite3_column_int(cst, 0);
    sqlite3_finalize(cst);

    if (!id_generate(out->id)) return false;
    snprintf(out->project_id, sizeof(out->project_id), "%s", project_id);
    out->issue_number = number;
    // title is external-derived and may be NULL, same as description below
    snprintf(out->title, sizeof(out->title), "%s", title ? title : "");
    snprintf(out->description, sizeof(out->description), "%s", description ? description : "");
    out->status = STATUS_OPEN;
    out->label_count = 0;
    out->assignee_count = 0;

    sqlite3_stmt *st;
    const char *sql = "INSERT INTO issues(id, project_id, issue_number, title, description, status)"
                      " VALUES(?, ?, ?, ?, ?, 0)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, out->id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, out->project_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, out->issue_number);
    sqlite3_bind_text(st, 4, out->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, out->description, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

bool db_issue_find_by_id(const char *id, issue_t *out) {
    memset(out, 0, sizeof(*out));
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB,
            "SELECT id, project_id, issue_number, title, description, status"
            " FROM issues WHERE id=?", -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) { issue_read_scalar(st, out); found = true; }
    sqlite3_finalize(st);
    if (!found) return false;

    sqlite3_stmt *ls;
    if (sqlite3_prepare_v2(DB, "SELECT label_id FROM issue_labels WHERE issue_id=?",
                           -1, &ls, NULL) == SQLITE_OK) {
        sqlite3_bind_text(ls, 1, id, -1, SQLITE_STATIC);
        while (out->label_count < MAX_LABELS && sqlite3_step(ls) == SQLITE_ROW) {
            snprintf(out->label_ids[out->label_count], ID_LEN, "%s", sqlite3_column_text(ls, 0));
            out->label_count++;
        }
        sqlite3_finalize(ls);
    }

    sqlite3_stmt *as;
    if (sqlite3_prepare_v2(DB, "SELECT user_id FROM issue_assignees WHERE issue_id=?",
                           -1, &as, NULL) == SQLITE_OK) {
        sqlite3_bind_text(as, 1, id, -1, SQLITE_STATIC);
        while (out->assignee_count < MAX_ASSIGNEES && sqlite3_step(as) == SQLITE_ROW) {
            snprintf(out->assignee_ids[out->assignee_count], ID_LEN, "%s", sqlite3_column_text(as, 0));
            out->assignee_count++;
        }
        sqlite3_finalize(as);
    }
    return true;
}

bool db_issue_set_status(const char *id, issue_status_t new_status) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, "UPDATE issues SET status=? WHERE id=?", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int(st, 1, (int)new_status);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

int db_issue_list_by_project(const char *project_id, issue_t **out_list) {
    *out_list = NULL;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB,
            "SELECT id, project_id, issue_number, title, description, status"
            " FROM issues WHERE project_id=? ORDER BY issue_number",
            -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, project_id, -1, SQLITE_STATIC);
    int cap = 0, n = 0;
    issue_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            issue_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        issue_read_scalar(st, &arr[n]);
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

bool db_issue_assign_label(const char *issue_id, const char *label_id) {
    sqlite3_stmt *st;
    /* OR IGNORE turns a repeat assignment into a no-op instead of a constraint
       error, so the menu can call assign without first checking. */
    const char *sql = "INSERT OR IGNORE INTO issue_labels(issue_id, label_id) VALUES(?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, issue_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, label_id, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

/* Escape %, _ and the escape char itself so a keyword is matched literally.
   Without this a user typing % would match every row, which was the injection
   the audit flagged in the old Mongo regex path. */
static void like_escape(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
        if (in[i] == '%' || in[i] == '_' || in[i] == '\\') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = '\0';
}

int db_issue_search(const char *keyword, int limit, issue_t **out_list) {
    *out_list = NULL;
    if (!keyword) keyword = "";   // caller input crosses a trust boundary here and may be NULL
    char esc[256], pattern[300];
    like_escape(keyword, esc, sizeof(esc));
    snprintf(pattern, sizeof(pattern), "%%%s%%", esc);   // %keyword%
    sqlite3_stmt *st;
    const char *sql =
        "SELECT id, project_id, issue_number, title, description, status "
        "FROM issues WHERE title LIKE ? ESCAPE '\\' "
        "OR description LIKE ? ESCAPE '\\' ORDER BY issue_number LIMIT ?";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pattern, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, limit);
    int cap = 0, n = 0;
    issue_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            issue_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        issue_read_scalar(st, &arr[n]);
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

int db_issue_filter(const char *status_filter, const char *label_id_filter,
                    int limit, issue_t **out_list) {
    *out_list = NULL;
    bool has_status = status_filter != NULL;
    bool has_label = label_id_filter != NULL;
    int status_val = STATUS_OPEN;
    if (has_status) status_val = (strcmp(status_filter, "closed") == 0) ? STATUS_CLOSED : STATUS_OPEN;

    /* Only fixed clause fragments are chosen in C. Every user value is bound,
       so no keyword or id reaches the SQL text. */
    char sql[512];
    if (has_label) {
        snprintf(sql, sizeof(sql),
            "SELECT i.id, i.project_id, i.issue_number, i.title, i.description, i.status "
            "FROM issues i JOIN issue_labels il ON il.issue_id = i.id "
            "WHERE il.label_id = ?%s ORDER BY i.issue_number LIMIT ?",
            has_status ? " AND i.status = ?" : "");
    } else {
        snprintf(sql, sizeof(sql),
            "SELECT id, project_id, issue_number, title, description, status "
            "FROM issues%s ORDER BY issue_number LIMIT ?",
            has_status ? " WHERE status = ?" : "");
    }

    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    int idx = 1;
    if (has_label) sqlite3_bind_text(st, idx++, label_id_filter, -1, SQLITE_STATIC);
    if (has_status) sqlite3_bind_int(st, idx++, status_val);
    sqlite3_bind_int(st, idx++, limit);

    int cap = 0, n = 0;
    issue_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            issue_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        issue_read_scalar(st, &arr[n]);
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}

bool db_issue_assign_user(const char *issue_id, const char *user_id) {
    sqlite3_stmt *st;
    const char *sql = "INSERT OR IGNORE INTO issue_assignees(issue_id, user_id) VALUES(?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, issue_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, user_id, -1, SQLITE_STATIC);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

/* ---- Comments ---- */

bool db_comment_add(const char *issue_id, const char *text) {
    char id[ID_LEN];
    if (!id_generate(id)) return false;
    sqlite3_stmt *st;
    const char *sql = "INSERT INTO comments(id, issue_id, text, created_at) VALUES(?, ?, ?, ?)";
    if (sqlite3_prepare_v2(DB, sql, -1, &st, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, issue_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, text, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 4, (long long)time(NULL));
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

int db_comment_list_by_issue(const char *issue_id, comment_t **out_list) {
    *out_list = NULL;
    sqlite3_stmt *st;
    /* rowid breaks ties within a single created_at second so two comments added
       back to back keep the order they were written, which the seconds-only
       timestamp cannot guarantee on its own. */
    if (sqlite3_prepare_v2(DB,
            "SELECT id, text FROM comments WHERE issue_id=? ORDER BY created_at, rowid",
            -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, issue_id, -1, SQLITE_STATIC);
    int cap = 0, n = 0;
    comment_t *arr = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int ncap = cap ? cap * 2 : 8;
            comment_t *tmp = realloc(arr, ncap * sizeof(*arr));
            if (!tmp) { free(arr); *out_list = NULL; sqlite3_finalize(st); return 0; }
            arr = tmp; cap = ncap;
        }
        memset(&arr[n], 0, sizeof(arr[n]));
        snprintf(arr[n].id, sizeof(arr[n].id), "%s", sqlite3_column_text(st, 0));
        snprintf(arr[n].text, sizeof(arr[n].text), "%s", sqlite3_column_text(st, 1));
        n++;
    }
    sqlite3_finalize(st);
    *out_list = arr;
    return n;
}
