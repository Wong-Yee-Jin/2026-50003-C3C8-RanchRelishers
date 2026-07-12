#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "models.h"

#define DURATION_LEN 32

// TO BE REMOVED

/* Parses strings like "2h 30m", "2h30m", "45m", "3h", or a bare integer
 * (treated as minutes). Returns total minutes, or -1 if the string has
 * no recognizable duration in it (Alternative Flow: invalid format). */
static int parse_duration(const char *s) {
    if (!s || strlen(s) == 0) return -1;

    int total = 0;
    int matched_any = 0;
    const char *p = s;

    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        char *end;
        long val = strtol(p, &end, 10);
        if (end == p) return -1; /* stray character with no leading number */

        while (*end == ' ') end++;

        if (*end == 'h' || *end == 'H') {
            total += (int)(val * 60);
            end++;
            matched_any = 1;
        } else if (*end == 'm' || *end == 'M') {
            total += (int)val;
            end++;
            matched_any = 1;
        } else if (*end == '\0') {
            total += (int)val; /* bare number defaults to minutes */
            matched_any = 1;
        } else {
            return -1; /* unrecognized unit */
        }
        p = end;
    }
    return matched_any ? total : -1;
}

/* Formats minutes back into a compact "2h 30m" style string for display. */
static void format_duration(int minutes, char *out, size_t outlen) {
    if (minutes <= 0) { snprintf(out, outlen, "0m"); return; }
    int h = minutes / 60, m = minutes % 60;
    if (h > 0 && m > 0)      snprintf(out, outlen, "%dh %dm", h, m);
    else if (h > 0)          snprintf(out, outlen, "%dh", h);
    else                     snprintf(out, outlen, "%dm", m);
}

void time_format_minutes(int minutes, char *out, size_t outlen) {
    format_duration(minutes, out, outlen);
}

/* ---- Set/replace the estimate for an issue ---- */
static void handle_set_estimate(const http_request_t *req, http_response_t *resp,
                                 const path_params_t *params) {
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char raw[DURATION_LEN] = {0};
    htttp_form_get(req->body, "estimate", raw, sizeof(raw));
    int minutes = parse_duration(raw);

    if (minutes < 0) {
        /* Alternative Flow: Invalid Duration Format */
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>");
        sb_append_escaped(&sb, iss.title);
        sb_append(&sb, "</h1><div class='error'>Couldn't parse that estimate. Try a "
                        "format like '2h 30m', '45m', or '3h'.</div><p><a href='/issues/");
        sb_append_escaped(&sb, issue_id);
        sb_append(&sb, "'>Back to issue</a></p>");
        char *page = render_page(iss.title, sb.data);
        http_response_html(resp, 400, page);
        free(page); sb_free(&sb);
        return;
    }

    db_issue_set_estimate(issue_id, minutes);
    char redirect[64];
    snprintf(redirect, sizeof(redirect), "/issues/%s", issue_id);
    http_response_redirect(resp, redirect);
}

/* ---- Log additional time spent on an issue ---- */
static void handle_log_time(const http_request_t *req, http_response_t *resp,
                             const path_params_t *params) {
    const char *issue_id = path_param_get(params, "id");
    issue_t iss;
    if (!db_issue_find_by_id(issue_id, &iss)) {
        http_response_html(resp, 404, "<h1>Issue not found</h1>");
        return;
    }

    char raw[DURATION_LEN] = {0};
    htttp_form_get(req->body, "duration", raw, sizeof(raw));
    int minutes = parse_duration(raw);

    if (minutes <= 0) {
        /* Alternative Flow: Invalid or Zero Duration */
        sb_t sb; sb_init(&sb);
        sb_append(&sb, "<h1>");
        sb_append_escaped(&sb, iss.title);
        sb_append(&sb, "</h1><div class='error'>Enter a duration greater than zero, "
                        "e.g. '1h 15m' or '20m'.</div><p><a href='/issues/");
        sb_append_escaped(&sb, issue_id);
        sb_append(&sb, "'>Back to issue</a></p>");
        char *page = render_page(iss.title, sb.data);
        http_response_html(resp, 400, page);
        free(page); sb_free(&sb);
        return;
    }

    db_issue_log_time(issue_id, minutes);
    char redirect[64];
    snprintf(redirect, sizeof(redirect), "/issues/%s", issue_id);
    http_response_redirect(resp, redirect);
}

void time_handlers_register(void) {
    router_add("POST", "/issues/:id/estimate", handle_set_estimate);
    router_add("POST", "/issues/:id/log", handle_log_time);
}
