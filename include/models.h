#ifndef MODELS_H
#define MODELS_H

/* Records are copied into fixed-size char arrays, not heap strings, so every
   text field needs a compile-time cap. These bounds track what the terminal UI
   shows and what a GitHub import can hand us. */

/* 24 lowercase hex chars from id_generate, plus the terminating NUL. */
#define ID_LEN 25
#define NAME_LEN 128
#define TITLE_LEN 256
/* Issue bodies get the largest field because they carry pasted GitHub text. */
#define DESC_LEN 2048
#define COMMENT_LEN 1024
#define STATUS_LEN 8
/* An issue caches at most this many labels and assignees in memory; the read
   loops in db.c stop at these numbers so the fixed arrays cannot overflow. */
#define MAX_LABELS 16
#define LABEL_DESC_LEN 160
#define USERNAME_LEN 64
#define MAX_ASSIGNEES 8
#define DISPLAY_NAME_LEN 128
#define AVATAR_URL_LEN 256

/* Stored as the integer in issues.status (0 = open, 1 = closed), so this order
   is the on-disk encoding. Extend by appending, never by reordering. */
typedef enum { STATUS_OPEN, STATUS_CLOSED } issue_status_t;

/* A project groups issues under a name the main menu lists. name is UNIQUE in
   the table, which db_project_name_exists checks before create. */
typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
} project_t;

/* A reusable tag. description is optional and stored as "" when the caller
   passes none, so read paths never have to null-check it. */
typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    char description[LABEL_DESC_LEN];
} label_t;

/* A comment as the UI needs to show it: id and body only. The issue link and
   created_at timestamp stay in the comments table and are not carried here. */
typedef struct {
    char id[ID_LEN];
    char text[COMMENT_LEN];
} comment_t;

/* A person who can be assigned to issues. A locally created user has only a
   username; display_name, avatar_url and github_id fill in when the account is
   linked to GitHub. github_id is UNIQUE and is how login re-finds the row. */
typedef struct {
    char id[ID_LEN];
    char username[USERNAME_LEN];
    char display_name[DISPLAY_NAME_LEN];
    char avatar_url[AVATAR_URL_LEN];
    long long github_id;
} user_t;

/* One issue. The scalar fields (id, owning project, per-project number, title,
   body, open/closed status) mirror the columns of the issues table. */
typedef struct {
    char id[ID_LEN];
    char project_id[ID_LEN];
    int  issue_number;
    char title[TITLE_LEN];
    char description[DESC_LEN];
    issue_status_t status;
    /* Labels and assignees are loaded on demand by db_issue_find_by_id from the
       join tables. List views leave these empty with the counts at zero, and
       the counts say how many array slots are in use. */
    char label_ids[MAX_LABELS][ID_LEN];
    int  label_count;
    char assignee_ids[MAX_ASSIGNEES][ID_LEN];
    int  assignee_count;
} issue_t;

#endif
