#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "template.h"
#include "auth.h"
#include "db.h"
#include "models.h"

void sb_init(sb_t *sb) {
    sb->cap = 1024;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_free(sb_t *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

static void sb_ensure(sb_t *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
}

void sb_append(sb_t *sb, const char *text) {
    size_t tlen = strlen(text);
    sb_ensure(sb, tlen);
    memcpy(sb->data + sb->len, text, tlen + 1);
    sb->len += tlen;
}

void sb_append_escaped(sb_t *sb, const char *text) {
    for (const char *p = text; *p; p++) {
        switch (*p) {
            case '<': sb_append(sb, "&lt;"); break;
            case '>': sb_append(sb, "&gt;"); break;
            case '&': sb_append(sb, "&amp;"); break;
            case '"': sb_append(sb, "&quot;"); break;
            default: {
                char c[2] = { *p, 0 };
                sb_append(sb, c);
            }
        }
    }
}

/*
 * Dark theme shared by every page: a fixed sidebar (logo, nav, and the
 * signed-in user's menu) plus a main area. 
 * render_page() puts a single scrolling column in that main area; 
 * render_app_shell() splits it into the three-column projects/issues/issue-detail layout instead.
 * Both share PAGE_CSS so the two never drift apart visually.
 */
static const char *PAGE_CSS =
    "*{box-sizing:border-box}"
    "html,body{height:100%;margin:0}"
    "body{font-family:-apple-system,'Segoe UI',Arial,sans-serif;"
    "background:#1b1e27;color:#e6e9ef}"
    "a{color:inherit;text-decoration:none}"
    "h1{margin:0 0 1rem;font-size:1.4rem}"
    "h3{font-size:1rem;color:#c7ccd6;margin:1.5rem 0 .5rem}"
    ".app{display:flex;min-height:100vh}"
    /* ---- sidebar ---- */
    ".sidebar{width:230px;flex-shrink:0;background:#1b1e27;"
    "border-right:1px solid #2a2e3a;display:flex;flex-direction:column;"
    "padding:1.25rem 0}"
    ".brand{display:flex;align-items:center;gap:.6rem;padding:0 1.25rem 1.5rem;"
    "font-weight:700;font-size:1.05rem;line-height:1.15}"
    ".brand-mark{position:relative;width:34px;height:34px;flex-shrink:0}"
    ".brand-mark .badge{position:absolute;right:-3px;bottom:-3px;width:14px;"
    "height:14px;border-radius:50%;background:#e5484d;color:#fff;font-size:9px;"
    "font-weight:900;display:flex;align-items:center;justify-content:center;"
    "border:2px solid #1b1e27}"
    ".brand small{display:block;font-weight:500;color:#9aa0ab;font-size:.75rem}"
    ".sidenav{display:flex;flex-direction:column;gap:.15rem;padding:0 .75rem}"
    ".sidenav a{display:flex;align-items:center;gap:.65rem;padding:.55rem .75rem;"
    "border-radius:6px;color:#c7ccd6;font-size:.92rem;font-weight:500}"
    ".sidenav a:hover{background:#242832;color:#fff}"
    ".sidenav svg{flex-shrink:0;opacity:.85}"
    ".sidebar-spacer{flex:1}"
    ".sidebar-user{padding:0 .75rem}"
    ".sidebar-user summary{list-style:none;display:flex;align-items:center;"
    "gap:.55rem;padding:.6rem .75rem;border-radius:6px;cursor:pointer;color:#e6e9ef}"
    ".sidebar-user summary::-webkit-details-marker{display:none}"
    ".sidebar-user summary:hover{background:#242832}"
    ".sidebar-user img,.avatar-fallback{width:26px;height:26px;border-radius:50%;"
    "object-fit:cover;background:#3a3f4d;display:flex;align-items:center;"
    "justify-content:center;font-size:.75rem;font-weight:700;flex-shrink:0}"
    ".sidebar-user .uname{flex:1;font-size:.88rem;font-weight:600;overflow:hidden;"
    "text-overflow:ellipsis;white-space:nowrap}"
    ".sidebar-user .menu{margin:.3rem .75rem 0;padding:.4rem;background:#242832;"
    "border-radius:6px}"
    ".sidebar-user .menu button{width:100%;text-align:left;background:none;"
    "border:none;color:#e6e9ef;padding:.45rem .5rem;border-radius:5px;"
    "font-size:.85rem;cursor:pointer;font-family:inherit}"
    ".sidebar-user .menu button:hover{background:#2f3542}"
    ".sidebar-login{margin:0 .75rem;padding:.6rem .75rem;text-align:center;"
    "border:1px solid #3a3f4d;border-radius:6px;font-weight:600;font-size:.88rem}"
    /* ---- main area ---- */
    ".main{flex:1;display:flex;flex-direction:column;min-width:0}"
    ".topbar{display:flex;align-items:center;gap:.75rem;padding:1rem 1.5rem;"
    "border-bottom:1px solid #2a2e3a}"
    ".topbar .grow{flex:1;display:flex;gap:.75rem}"
    ".search-input{width:100%;max-width:320px;background:#242832;"
    "border:1px solid #343a48;color:#e6e9ef;border-radius:6px;padding:.5rem .8rem;"
    "font-size:.88rem}"
    ".search-input::placeholder{color:#767c8a}"
    ".btn-add{background:#3fb950;color:#0b1f10;border:none;border-radius:6px;"
    "padding:.55rem 1rem;font-weight:700;font-size:.88rem;cursor:pointer;"
    "list-style:none;white-space:nowrap}"
    ".btn-add::-webkit-details-marker{display:none}"
    ".add-menu{position:relative}"
    ".add-menu .panel{position:absolute;right:0;top:calc(100% + 6px);width:280px;"
    "background:#242832;border:1px solid #343a48;border-radius:8px;padding:1rem;"
    "z-index:10;box-shadow:0 8px 24px rgba(0,0,0,.4)}"
    ".add-menu .panel h4{margin:.2rem 0 .5rem;font-size:.85rem;color:#9aa0ab}"
    ".add-menu .panel form{margin-bottom:.75rem}"
    ".single-col{padding:1.5rem}"
    /* ---- three column layout ---- */
    ".columns{flex:1;display:flex;min-height:0;overflow:hidden}"
    ".col{overflow-y:auto}"
    ".col1{width:300px;flex-shrink:0;border-right:1px solid #2a2e3a}"
    ".col2{width:320px;flex-shrink:0;border-right:1px solid #2a2e3a}"
    ".col3{flex:1;padding:1.5rem}"
    ".col-header{padding:.9rem 1rem}"
    ".row{display:flex;align-items:center;justify-content:space-between;"
    "gap:.5rem;padding:.75rem 1.1rem;color:#dfe3ea;font-size:.92rem;"
    "border-left:2px solid transparent}"
    ".row:hover{background:#20242e}"
    ".row.active{background:#242938;border-left:2px solid #3fb950}"
    ".row .chev{color:#5b6170;flex-shrink:0}"
    ".row .rowtitle{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    ".empty-hint{padding:0 1.1rem;color:#767c8a;font-size:.85rem}"
    /* ---- breadcrumb ---- */
    ".breadcrumb{display:flex;align-items:center;gap:.6rem;padding:.7rem 1.5rem;"
    "border-top:1px solid #2a2e3a;color:#767c8a;font-size:.8rem}"
    ".breadcrumb .crumb b{display:block;color:#c7ccd6;font-size:.85rem;"
    "font-weight:600}"
    ".breadcrumb .sep{color:#4b5061}"
    /* ---- issue detail ---- */
    ".open{color:#3fb950;font-weight:600}.closed{color:#a371f7;font-weight:600}"
    ".label{display:inline-block;background:#1c2a20;border:1px solid #2ea043;"
    "color:#7ee2a8;border-radius:12px;padding:.15rem .65rem;font-size:.78rem;"
    "margin:0 .3rem .3rem 0}"
    ".assignee{display:inline-block;background:#2a2010;border:1px solid #d29922;"
    "color:#f2c94c;border-radius:12px;padding:.15rem .65rem;font-size:.78rem;"
    "margin:0 .3rem .3rem 0}"
    ".muted{color:#9aa0ab;font-size:.88rem}"
    ".error{background:#3a1a1c;border:1px solid #b3413a;color:#ff9b93;"
    "padding:.6rem 1rem;border-radius:6px;margin-bottom:1rem}"
    ".field-label{font-weight:700;margin-top:1.1rem}"
    ".card{border:1px solid #2a2e3a;border-radius:6px;padding:.75rem 1rem;"
    "margin:.5rem 0;background:#20242e}"
    "input,textarea,select{width:100%;padding:.5rem .7rem;margin:.3rem 0 .8rem;"
    "background:#20242e;border:1px solid #343a48;color:#e6e9ef;border-radius:6px;"
    "box-sizing:border-box;font-family:inherit}"
    "button{background:#3fb950;color:#0b1f10;border:none;padding:.55rem 1rem;"
    "border-radius:6px;cursor:pointer;font-weight:700}"
    "button.secondary{background:#2f3542;color:#e6e9ef}"
    "form.inline{display:inline}"
    ".btn-wide{display:block;width:100%;text-align:center;margin-top:1rem}"
    ".label-pick,.assignee-pick{display:inline-block;margin:0 .6rem .5rem 0;"
    "font-size:.85rem;color:#c7ccd6}";

static const char *ICON_MONITOR =
    "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' "
    "stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<rect x='2' y='4' width='20' height='13' rx='2'/><line x1='8' y1='21' x2='16' y2='21'/>"
    "<line x1='12' y1='17' x2='12' y2='21'/></svg>";
static const char *ICON_TAG =
    "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' "
    "stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M20.59 13.41 11 3.83A2 2 0 0 0 9.59 3.24H4a1 1 0 0 0-1 1v5.59a2 2 0 0 0 "
    ".59 1.41l9.58 9.58a2 2 0 0 0 2.83 0l4.59-4.59a2 2 0 0 0 0-2.83z'/>"
    "<circle cx='7.5' cy='7.5' r='1.2' fill='currentColor'/></svg>";
static const char *ICON_USERS =
    "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' "
    "stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<path d='M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2'/><circle cx='9' cy='7' r='4'/>"
    "<path d='M23 21v-2a4 4 0 0 0-3-3.87'/><path d='M16 3.13a4 4 0 0 1 0 7.75'/></svg>";

static void append_sidebar(sb_t *sb) {
    sb_append(sb, "<aside class='sidebar'><div class='brand'>"
                     "<div class='brand-mark'>"
                     "<svg width='34' height='34' viewBox='0 0 34 34'>"
                     "<circle cx='17' cy='17' r='17' fill='#e6e9ef'/>"
                     "<circle cx='12' cy='15' r='2' fill='#1b1e27'/>"
                     "<circle cx='22' cy='15' r='2' fill='#1b1e27'/>"
                     "<path d='M10 22c2 3 12 3 14 0' stroke='#1b1e27' stroke-width='2' "
                     "fill='none' stroke-linecap='round'/></svg>"
                     "<span class='badge'>&times;</span></div>"
                     "<span>GitHub<small>Issue Tracker (Mini)</small></span>"
                     "</div>");
    sb_append(sb, "<nav class='sidenav'>");
    sb_append(sb, "<a href='/projects'>"); sb_append(sb, ICON_MONITOR); sb_append(sb, " Projects</a>");
    sb_append(sb, "<a href='/labels'>"); sb_append(sb, ICON_TAG); sb_append(sb, " Labels</a>");
    sb_append(sb, "<a href='/users'>"); sb_append(sb, ICON_USERS); sb_append(sb, " Contributers</a>");
    sb_append(sb, "</nav><div class='sidebar-spacer'></div>");

    user_t current;
    if (auth_get_current_user(&current)) {
        sb_append(sb, "<details class='sidebar-user'><summary>");
        if (current.avatar_url[0]) {
            sb_append(sb, "<img src='");
            sb_append_escaped(sb, current.avatar_url);
            sb_append(sb, "' alt=''>");
        } else {
            sb_append(sb, "<span class='avatar-fallback'>");
            char initial[2] = { current.username[0] ? current.username[0] : '?', 0 };
            sb_append_escaped(sb, initial);
            sb_append(sb, "</span>");
        }
        sb_append(sb, "<span class='uname'>");
        sb_append_escaped(sb, current.username);
        sb_append(sb, "</span>"
                       "<svg width='14' height='14' viewBox='0 0 24 24' fill='none' "
                       "stroke='currentColor' stroke-width='2'><path d='m6 9 6 6 6-6'/></svg>"
                       "</summary><div class='menu'>"
                       "<form method='POST' action='/logout'>"
                       "<button type='submit'>Log out</button></form>"
                       "</div></details>");
    } else {
        sb_append(sb, "<a class='sidebar-login' href='/login'>Sign In</a>");
    }
    sb_append(sb, "</aside>");
}

static char *finish_page(sb_t *sb, const char *title) {
    sb_append(sb, "</div></body></html>");
    /* stitch title + CSS into the head, then hand ownership to caller */
    sb_t out; sb_init(&out);
    sb_append(&out, "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                     "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<title>");
    sb_append_escaped(&out, title);
    sb_append(&out, " - Mini Issue Tracker</title><style>");
    sb_append(&out, PAGE_CSS);
    sb_append(&out, "</style></head><body><div class='app'>");
    sb_append(&out, sb->data);
    sb_free(sb);
    char *result = out.data;
    return result;
}

char *render_page(const char *title, const char *inner_html) {
    sb_t sb; sb_init(&sb);
    append_sidebar(&sb);
    sb_append(&sb, "<div class='main'><div class='single-col'>");
    sb_append(&sb, inner_html);
    sb_append(&sb, "</div></div>");
    return finish_page(&sb, title);
}

char *render_landing_page(void) {
    sb_t sb; sb_init(&sb);
    sb_append(&sb,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Mini Issue Tracker</title><style>"
        "*{box-sizing:border-box}html,body{height:100%;margin:0}"
        "body{font-family:-apple-system,'Segoe UI',Arial,sans-serif;background:#1b1e27;"
        "color:#e6e9ef;display:flex;flex-direction:column}"
        ".ltop{display:flex;align-items:center;justify-content:space-between;"
        "padding:1.25rem 2rem}"
        ".lbrand{display:flex;align-items:center;gap:.6rem;font-weight:700;"
        "font-size:1.05rem;line-height:1.15}"
        ".lbrand-mark{position:relative;width:34px;height:34px;flex-shrink:0}"
        ".lbrand-mark .badge{position:absolute;right:-3px;bottom:-3px;width:14px;"
        "height:14px;border-radius:50%;background:#e5484d;color:#fff;font-size:9px;"
        "font-weight:900;display:flex;align-items:center;justify-content:center;"
        "border:2px solid #1b1e27}"
        ".lbrand small{display:block;font-weight:500;color:#9aa0ab;font-size:.75rem}"
        ".signin{background:#2f3542;color:#e6e9ef;border:none;border-radius:6px;"
        "padding:.6rem 1.2rem;font-weight:600;font-size:.9rem}"
        ".lhero{flex:1;display:flex;flex-direction:column;align-items:center;"
        "justify-content:center;text-align:center;padding:2rem 1.5rem 6rem}"
        ".lhero h1{font-size:2.1rem;line-height:1.3;margin:0 0 1rem;font-weight:800}"
        ".lhero p{color:#9aa0ab;font-size:1.05rem;margin:0 0 1.75rem}"
        ".cta{display:inline-flex;align-items:center;gap:.5rem;background:#3fb950;"
        "color:#0b1f10;border:none;border-radius:8px;padding:.85rem 1.5rem;"
        "font-weight:700;font-size:1rem}"
        "</style></head><body>");

    sb_append(&sb, "<div class='ltop'><div class='lbrand'><div class='lbrand-mark'>"
                    "<svg width='34' height='34' viewBox='0 0 34 34'>"
                    "<circle cx='17' cy='17' r='17' fill='#e6e9ef'/>"
                    "<circle cx='12' cy='15' r='2' fill='#1b1e27'/>"
                    "<circle cx='22' cy='15' r='2' fill='#1b1e27'/>"
                    "<path d='M10 22c2 3 12 3 14 0' stroke='#1b1e27' stroke-width='2' "
                    "fill='none' stroke-linecap='round'/></svg>"
                    "<span class='badge'>&times;</span></div>"
                    "<span>GitHub<small>Issue Tracker (Mini)</small></span></div>"
                    "<a href='/login'><button class='signin' type='button'>Sign In</button></a>"
                    "</div>");

    sb_append(&sb, "<div class='lhero'>"
                    "<h1>Information overload from GitHub?<br>Me too :&rsquo;)</h1>"
                    "<p>Try out the Mini GitHub Issue Tracker for starters!</p>"
                    "<a class='cta' href='/login'>Projects and Issues &rarr;</a>"
                    "</div>");

    sb_append(&sb, "</body></html>");
    char *result = sb.data;
    return result;
}

char *render_app_shell(const char *page_title, const app_shell_opts_t *opts) {
    sb_t sb; sb_init(&sb);
    append_sidebar(&sb);

    sb_append(&sb, "<div class='main'><div class='topbar'><div class='grow'>"
                    "<input id='project-search' class='search-input' "
                    "placeholder='Search Projects'>");
    if (opts->active_project_id) {
        sb_append(&sb, "<input id='issue-search' class='search-input' "
                        "placeholder='Search Issues'>");
    }
    sb_append(&sb, "</div>");

    /* +Add Project/Issue dropdown */
    sb_append(&sb, "<details class='add-menu'><summary class='btn-add'>"
                    "+ Add Project/Issue</summary><div class='panel'>");
    sb_append(&sb, "<h4>New project</h4><form method='POST' action='/projects'>"
                    "<input name='name' placeholder='Project name' required>"
                    "<button type='submit'>Create Project</button></form>");
    if (opts->active_project_id) {
        sb_append(&sb, "<h4>New issue in this project</h4><form method='POST' action='/projects/");
        sb_append_escaped(&sb, opts->active_project_id);
        sb_append(&sb, "/issues'><input name='title' placeholder='Issue title' required>"
                        "<textarea name='description' placeholder='Description'></textarea>"
                        "<button type='submit'>Create Issue</button></form>");
    }
    sb_append(&sb, "</div></details></div>");

    if (opts->banner_html && opts->banner_html[0]) {
        sb_append(&sb, "<div style='padding:1rem 1.5rem 0'>");
        sb_append(&sb, opts->banner_html);
        sb_append(&sb, "</div>");
    }

    /* ---- columns ---- */
    sb_append(&sb, "<div class='columns'>");

    /* col1: projects */
    sb_append(&sb, "<div class='col col1'><div class='list'>");
    {
        project_t *list; int n = db_project_list(&list);
        if (n == 0) {
            sb_append(&sb, "<p class='empty-hint'>No projects exist yet.</p>");
        } else {
            for (int i = 0; i < n; i++) {
                int active = opts->active_project_id &&
                             strcmp(opts->active_project_id, list[i].id) == 0;
                sb_append(&sb, "<a class='row proj-row");
                if (active) sb_append(&sb, " active");
                sb_append(&sb, "' data-name='");
                sb_append_escaped(&sb, list[i].name);
                sb_append(&sb, "' href='/projects/");
                sb_append_escaped(&sb, list[i].id);
                sb_append(&sb, "/issues'><span class='rowtitle'>");
                sb_append_escaped(&sb, list[i].name);
                sb_append(&sb, "</span><span class='chev'>&rsaquo;</span></a>");
            }
        }
        free(list);
    }
    sb_append(&sb, "</div></div>");

    /* col2: issues (only once a project is selected) */
    if (opts->active_project_id) {
        sb_append(&sb, "<div class='col col2'><div class='list'>");
        if (opts->col2_rows_html && opts->col2_rows_html[0]) {
            sb_append(&sb, opts->col2_rows_html);
        } else {
            sb_append(&sb, "<p class='empty-hint'>No issues yet for this project.</p>");
        }
        sb_append(&sb, "</div></div>");
    }

    /* col3: issue detail (only once an issue is open) */
    if (opts->active_issue_id && opts->col3_html) {
        sb_append(&sb, "<div class='col col3'>");
        sb_append(&sb, opts->col3_html);
        sb_append(&sb, "</div>");
    }

    sb_append(&sb, "</div>"); /* .columns */

    /* ---- breadcrumb ---- */
    if (opts->active_project_id) {
        sb_append(&sb, "<div class='breadcrumb'><span class='crumb'><b>");
        sb_append_escaped(&sb, opts->active_project_name ? opts->active_project_name : "");
        sb_append(&sb, "</b>[Project]</span>");
        if (opts->active_issue_id) {
            sb_append(&sb, "<span class='sep'>&rsaquo;</span><span class='crumb'><b>");
            sb_append_escaped(&sb, opts->active_issue_label ? opts->active_issue_label : "");
            sb_append(&sb, "</b>[Issue]</span>");
        }
        sb_append(&sb, "</div>");
    }

    sb_append(&sb, "</div>"); /* .main */

    sb_append(&sb,
        "<script>"
        "function mgitFilter(inputId,sel){"
        "var i=document.getElementById(inputId);if(!i)return;"
        "i.addEventListener('input',function(){"
        "var q=i.value.toLowerCase();"
        "document.querySelectorAll(sel).forEach(function(el){"
        "var n=(el.getAttribute('data-name')||el.textContent||'').toLowerCase();"
        "el.style.display=n.indexOf(q)===-1?'none':'';"
        "});});}"
        "mgitFilter('project-search','.proj-row');"
        "mgitFilter('issue-search','.issue-row');"
        "</script>");

    return finish_page(&sb, page_title);
}
