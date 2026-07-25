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
