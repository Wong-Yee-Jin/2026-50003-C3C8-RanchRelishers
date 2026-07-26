#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "handlers.h"
#include "template.h"
#include "db.h"
#include "models.h"
#include "auth.h"

/* ---- View Projects ---- */
static void handle_list_projects(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)req; (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    app_shell_opts_t opts = {0};
    char *page = render_app_shell("Projects", &opts);
    http_response_html(resp, 200, page);
    free(page);
}

/* ---- Create Project ---- */
static void handle_create_project(const http_request_t *req, http_response_t *resp, const path_params_t *params) {
    (void)params;

    user_t cur;
    if (!auth_get_current_user(&cur)) {
        http_response_redirect(resp, "/");
        return;
    }

    char name[NAME_LEN] = {0};
    htttp_form_get(req->body, "name", name, sizeof(name));

    project_t out;
    if (!db_project_create(cur.id, name, &out)) {
        /* Alternative Flow: Invalid Project Name (empty or duplicate) */
        app_shell_opts_t opts = {0};
        opts.banner_html = "<div class='error'>Invalid project name: it may be "
                            "empty or already exist.</div>";
        char *page = render_app_shell("Projects", &opts);
        http_response_html(resp, 400, page);
        free(page);
        return;
    }

    http_response_redirect(resp, "/projects");
}

void project_handlers_register(void) {
    router_add("GET", "/projects", handle_list_projects);
    router_add("POST", "/projects", handle_create_project);
}
