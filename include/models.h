#ifndef MODELS_H
#define MODELS_H

#define ID_LEN 25
#define NAME_LEN 128
#define TITLE_LEN 256
#define DESC_LEN 2048
#define COMMENT_LEN 1024
#define STATUS_LEN 8
#define MAX_LABELS 16
#define LABEL_DESC_LEN 160
#define USERNAME_LEN 64
#define MAX_ASSIGNEES 8
#define DISPLAY_NAME_LEN 128
#define AVATAR_URL_LEN 256

typedef enum { STATUS_OPEN, STATUS_CLOSED } issue_status_t;

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
} project_t;

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    char description[LABEL_DESC_LEN];
} label_t;

typedef struct {
    char id[ID_LEN];
    char text[COMMENT_LEN];
} comment_t;

typedef struct {
    char id[ID_LEN];
    char username[USERNAME_LEN];
    char display_name[DISPLAY_NAME_LEN];
    char avatar_url[AVATAR_URL_LEN];
    long long github_id;
} user_t;

typedef struct {
    char id[ID_LEN];
    char project_id[ID_LEN];
    int  issue_number;
    char title[TITLE_LEN];
    char description[DESC_LEN];
    issue_status_t status;
    char label_ids[MAX_LABELS][ID_LEN];
    int  label_count;
    char assignee_ids[MAX_ASSIGNEES][ID_LEN];
    int  assignee_count;
    int  estimate_minutes;
    int  logged_minutes;
} issue_t;

#endif
