#include "ui/menu.h"
#include "ui/input.h"
#include "core/github_service.h"
#include "core/services.h"
/* Straight to github.c for the username search only: it needs no token and
   no session, so it never goes near github_service the way login and the
   repo sync do. */
#include "github.h"
#include "render.h"
#include "assets.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Strips leading blanks and any trailing whitespace fgets() left on the line
   (the newline, and a trailing \r if input ever comes from a CRLF source). */
char *ui_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;

    size_t len = strlen(start);
    while (len > 0) {
        char c = start[len - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        start[--len] = '\0';
    }

    /* start may have moved past leading spaces, but the interface promises
       to return s itself, so shift the trimmed text back to the front. */
    if (start != s) memmove(s, start, len + 1);
    return s;
}

/* -1 means "not a selectable choice" rather than 0, so a blank line or a
   typo can't be mistaken for the user deliberately picking item 0 (back/quit). */
int ui_parse_choice(const char *line) {
    if (!line || line[0] == '\0') return -1;
    char *end;
    errno = 0;
    long v = strtol(line, &end, 10);
    if (*end != '\0') return -1;   // trailing junk means the line wasn't a plain number
    // a value strtol had to clamp, or one that would not survive the (int)
    // cast below, is not a real menu choice either
    if (errno == ERANGE || v < 0 || v > INT_MAX) return -1;
    return (int)v;
}

/* Reads one line into buf and trims it, so every screen below gets clean
   input without repeating the fgets/ui_trim pair. Returns false on EOF (a
   piped script running out, or ctrl-D) so callers can unwind instead of
   looping on a line that never arrives. */
static bool read_line(char *buf, size_t n) {
    if (!fgets(buf, (int)n, stdin)) return false;
    /* No '\n' in buf means the real line was longer than the buffer, and the
       rest of it is still queued on stdin. Left there, it would answer the
       NEXT prompt instead of this one, so drain it now. Skipped at EOF, since
       there is nothing left to drain and getchar would just report EOF again. */
    if (!strchr(buf, '\n') && !feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    ui_trim(buf);
    return true;
}

/* Arrow keys need a terminal on both ends: a keyboard to read the escape
   sequences from, and a screen worth repainting. Anything else, a pipe on
   either side or a window too small to decorate, keeps the line-based flow
   the e2e suite reads byte for byte. Asked fresh each time rather than cached
   so a resize between screens is picked up like everything else here. */
static bool ui_interactive(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && render_decorate();
}

/* One line under the header saying what the last action did. It belongs to
   the repaint rather than to the scrollback, so it is cleared the moment the
   user acts again. Piped runs have scrollback and print their outcomes
   inline instead, which is why nothing here reaches them. */
static char ui_status[192];
static render_slot_t ui_status_slot = RENDER_DIM;

/* Set for exactly one frame right after an action changes ui_status, so that
   frame draws the line in reverse video before the next one draws it normal.
   Read by draw_frame, written only by screen_open_reveal. */
static bool ui_status_flash = false;

/* Row-reveal state for a screen's first draw. ui_line increments the seen
   count on every row and, once a limit is set, drops anything past it, so a
   throwaway call with limit 0 counts a draw()'s rows with nothing printed
   and a real call with limit N shows only the first N. -1 means unlimited:
   every row draws, which is what a screen's first frame settles on and what
   every repaint after it uses throughout. */
static int ui_reveal_limit = -1;
static int ui_reveal_seen = 0;

/* 0..100. project_progress_append scales the closed count it bars against by
   this, so the Projects screen's first draw can ramp a meter up from empty
   instead of snapping straight to its value. 100 outside that animation,
   which is every other screen and every repaint after the first. */
static int ui_meter_pct = 100;

static void ui_say(render_slot_t slot, const char *text) {
    if (!ui_interactive()) return;
    ui_status_slot = slot;
    snprintf(ui_status, sizeof(ui_status), "%s", text);
}

/* Something worth saying that is not a service result. Both paths hear it:
   piped runs print the line they always printed, an interactive screen puts
   the same words in its status row. */
static void note(render_slot_t slot, const char *text) {
    if (ui_interactive()) ui_say(slot, text);
    else printf("%s\n", text);
}

/* Every action funnels its svc_result_t through here so the wording for
   "not signed in" / "not found" / "database error" is written once. Callers
   supply the text for SVC_INVALID, since that is the one code whose meaning
   changes per action, plus the line to show when the write went through.
   A piped run still prints nothing at all on success, exactly as before. */
static void report(svc_result_t r, const char *invalid_reason, const char *done) {
    switch (r) {
        case SVC_OK:        ui_say(RENDER_OK, done); break;
        case SVC_DENIED:    note(RENDER_DANGER, "sign in first"); break;
        case SVC_INVALID:   note(RENDER_DANGER, invalid_reason); break;
        case SVC_NOT_FOUND: note(RENDER_DANGER, "not found"); break;
        case SVC_DB_ERROR:  note(RENDER_DANGER, "database error"); break;
    }
}

/* The caret marks the row Enter would act on. A screen being read from a pipe
   passes selected < 0 and gets back the indent it has always printed, so its
   rows do not move. The caret is two columns wide either way, which is why
   the unselected rows are indented to match. */
static const char *ui_row(int i, int selected, const char *plain) {
    static char caret[32];
    if (selected < 0) return plain;
    if (i != selected) return "  ";
    snprintf(caret, sizeof(caret), "%s%s", render_style(RENDER_ACCENT),
             render_utf8() ? "❯ " : "> ");
    return caret;
}

/* Key hints for a screen that is being navigated rather than typed at. The
   arrows are the only part that has to know about the locale, so callers hand
   over just their own half of the line. */
static const char *ui_keys(const char *tail) {
    static char row[160];
    snprintf(row, sizeof(row), "%s move   %s",
             render_utf8() ? "↑↓" : "up/down", tail);
    return row;
}

/* One text prompt, in canonical mode with the cursor showing. The screens are
   navigated with the cursor hidden, and typing a project name into a screen
   that is not showing you where you are typing is the one place that stops
   being right. The accent "> " only shows up on the interactive path: a
   piped script has no cursor to point at and the bare prompt text is what
   the e2e suite already matches. */
static bool prompt_line(const char *prompt, char *buf, size_t n) {
    printf("%s", render_cursor(true));
    if (ui_interactive())
        printf("%s> %s", render_style(RENDER_ACCENT), render_reset());
    printf("%s", prompt);
    bool got = read_line(buf, n);
    printf("%s", render_cursor(false));
    return got;
}

/* One line of a screen's body. On the interactive path it becomes a sided
   row inside the box; off it, this is the exact printf every screen has
   always used, so a piped run reads byte for byte the same as before. Every
   draw_* function below routes its output through here instead of calling
   printf directly, which is what turns a screen into a closed rectangle
   without duplicating that decision at every call site. */
static void ui_line(const char *text) {
    if (ui_interactive()) {
        ui_reveal_seen++;
        if (ui_reveal_limit >= 0 && ui_reveal_seen > ui_reveal_limit) return;
        render_box_line(text);
    } else {
        printf("%s\n", text);
    }
}

/* Content columns available inside the box, for a caller sizing a
   render_truncate() budget. Only meaningful on the interactive path; the
   piped path never truncates a name or title; it never has, and a box that
   is not being drawn has no width to fit inside. */
static int ui_content_cols(void) {
    int cols, rows;
    render_query_size(&cols, &rows);
    int content = cols - 4;
    return content > 0 ? content : 0;
}

/* Truncates `text` to fit next to a `prefix_cols`-wide row prefix and the
   caret column (always 2 columns, selected or not) inside the box. A no-op
   off the interactive path: piped rows print names and titles in full, the
   way they always have, since there is no box to overflow. */
static void ui_fit_field(char *out, size_t outsz, const char *text, int prefix_cols) {
    if (!ui_interactive()) { snprintf(out, outsz, "%s", text); return; }
    int budget = ui_content_cols() - 2 - prefix_cols;
    render_truncate(text, budget, render_utf8(), out, outsz);
}

/* The empty-state message plus a one-line hint, framed as two dim lines
   inside the box on the interactive path. The piped path only ever printed
   the bare message and keeps doing exactly that; the hint line is new and
   interactive-only, so it never reaches the e2e suite. */
static void ui_empty(const char *message, const char *hint) {
    if (!ui_interactive()) { printf("%s\n", message); return; }
    char line[160];
    snprintf(line, sizeof(line), "%s%s%s", render_style(RENDER_DIM), message, render_reset());
    ui_line(line);
    snprintf(line, sizeof(line), "%s%s%s", render_style(RENDER_DIM), hint, render_reset());
    ui_line(line);
}

/* A blank answer to a field that needs one. Piped runs send it through to the
   service, which rejects it with the wording it has always used; on a screen
   that repaints, the same blank Enter is how someone backs out of a prompt
   they opened by mistake. */
static bool cancelled(const char *text) {
    if (text[0] != '\0' || !ui_interactive()) return false;
    ui_say(RENDER_DIM, "cancelled");
    return true;
}

/* Holds a one-off listing on screen until the reader is done with it. Search
   results and repo lists are printed below the frame, and the next repaint
   clears the screen, so without this they would be gone before anyone read
   them. Piped runs keep everything in their scrollback and skip it. */
static void ui_pause(void) {
    if (!ui_interactive()) return;
    printf("%spress any key%s\n", render_style(RENDER_DIM), render_reset());
    fflush(stdout);
    if (render_raw_enter()) {
        input_read_key();
        render_raw_leave();
    }
}

/* Prints a screen's title, styled to how much room the terminal actually
   has. render_mode() is re-queried on every call rather than cached, so a
   terminal resized between screens picks up the new mode on the next
   screen the user opens instead of staying stuck at whatever mode was
   active at startup. */
static void screen_header(const char *title) {
    render_mode_t mode = render_mode();

    /* On a real terminal the title opens a rule that render_help_row closes
       further down the screen. Everywhere else, including every piped run,
       the plain header below is printed byte for byte as it always was. */
    if (render_decorate()) {
        char banner[TITLE_LEN + 32];
        if (mode == RENDER_FULL) snprintf(banner, sizeof(banner), "%s %s", logo_compact, title);
        else                     snprintf(banner, sizeof(banner), "%s", title);
        /* The blank line above the rule separates this screen from the last
           one in the scrollback. A screen that repaints in place has no last
           one to separate from, so the line would only push it down a row. */
        if (!ui_interactive()) printf("\n");
        render_title_rule(banner);
        return;
    }

    switch (mode) {
        case RENDER_FULL:
            printf("\n%s%s%s  %s%s%s\n", render_style(RENDER_ACCENT), logo_compact, render_reset(),
                   render_style(RENDER_ACCENT), title, render_reset());
            break;
        case RENDER_COMPACT:
            printf("\n%s%s%s\n", render_style(RENDER_ACCENT), title, render_reset());
            break;
        case RENDER_MINIMAL:
            printf("\n%s\n%senlarge to 80x24 for the full art%s\n",
                   title, render_style(RENDER_DIM), render_reset());
            break;
    }
}

/* What a screen got back from the person using it. UI_UNKNOWN only ever
   comes from a typed line, since a keystroke that means nothing is dropped
   where it is read rather than reported as a choice. */
typedef enum { UI_INDEX, UI_LETTER, UI_BACK, UI_UNKNOWN, UI_EOF } ui_kind_t;

typedef struct {
    ui_kind_t kind;
    int index;      /* 1-based row, for UI_INDEX */
    char letter;    /* the shortcut, lowercased, for UI_LETTER */
} ui_choice_t;

/* Draws the body of one screen: everything between the status line and the
   help row. selected is the row the caret sits on, or -1 for a screen that
   is being read rather than navigated. */
typedef void (*ui_draw_fn)(const void *ctx, int selected);

/* The one decision every screen makes: header, body, hints, and then a wait
   for the answer. The two paths through it differ only in how that answer is
   collected, which is what keeps a piped session printing what it always
   printed while a real terminal gets arrow keys and a repaint per keystroke.

   sel belongs to the caller so the caret stays where it was left after an
   action redraws the screen. count is the number of rows Enter or a digit can
   land on, which some screens deliberately set to 0 on the piped path: their
   rows have never been selectable by number there, and a digit has to keep
   reading as an unknown choice.

   letters lists the shortcut keys this screen answers to, lowercase. hints is
   the help row for a piped run, keys the one for a navigated screen. */
/* Draws one full frame: sync bracket, clear, header, status line (flashed in
   reverse video for the one frame after an action sets it, per
   ui_status_flash), blank line, the screen's own rows, then the help row.
   Every repaint after a screen's first draw goes straight through here with
   nothing animated; screen_open_reveal below reaches it once per phase for
   the first. */
static void draw_frame(const char *title, ui_draw_fn draw, const void *ctx,
                       int count, int sel, const char *keys) {
    printf("%s%s", render_sync_begin(), render_clear());
    screen_header(title);
    /* Printed even when empty, so the rows below it do not jump up and
       down a line as messages come and go. Framed line, blank framed
       line, then the body: the status sits right under the title rule
       on every screen, with the same one line of breathing room before
       whatever the screen draws next. */
    char status_line[sizeof(ui_status) + 32];
    snprintf(status_line, sizeof(status_line), "%s%s%s%s",
             ui_status_flash ? render_flash() : "",
             render_style(ui_status_slot), ui_status, render_reset());
    render_box_line(status_line);
    render_box_line("");
    ui_reveal_seen = 0;
    draw(ctx, count > 0 ? sel : -1);
    render_help_row(keys);
    printf("%s", render_sync_end());
    fflush(stdout);
}

/* Timing for a screen's first draw: a status flash if an action just set
   one, then its rows appearing top to bottom, then (Projects only) its
   meters filling. Every phase polls for a waiting key between frames and,
   the moment one shows up, stops drawing intermediate frames and jumps
   straight to the finished one with that key still unread on stdin, so a
   held-down arrow never queues animation behind it. */
#define REVEAL_FLASH_MS 120
#define REVEAL_ROW_STEP_MS 12
#define REVEAL_ROW_BUDGET_MS 150
#define REVEAL_METER_STEPS 10
#define REVEAL_METER_BUDGET_MS 250

static void screen_open_reveal(const char *title, ui_draw_fn draw, const void *ctx,
                               int count, int sel, const char *keys, bool animate_meters) {
    bool skip = false;

    if (ui_status[0] != '\0') {
        ui_status_flash = true;
        draw_frame(title, draw, ctx, count, sel, keys);
        ui_status_flash = false;
        if (input_poll(REVEAL_FLASH_MS)) skip = true;
    }

    if (!skip) {
        /* A throwaway call with the limit clamped to zero counts this
           screen's rows with nothing printed, so the real reveal below knows
           how many steps to spread its budget across. */
        ui_reveal_limit = 0;
        ui_reveal_seen = 0;
        draw(ctx, count > 0 ? sel : -1);
        int total_rows = ui_reveal_seen;

        if (total_rows > 0) {
            int step_ms = REVEAL_ROW_STEP_MS;
            if (step_ms * total_rows > REVEAL_ROW_BUDGET_MS) step_ms = REVEAL_ROW_BUDGET_MS / total_rows;
            if (step_ms < 1) step_ms = 1;

            for (int shown = 1; shown <= total_rows && !skip; shown++) {
                ui_reveal_limit = shown;
                draw_frame(title, draw, ctx, count, sel, keys);
                if (input_poll(step_ms)) skip = true;
            }
        }
    }
    ui_reveal_limit = -1;

    if (!skip && animate_meters && count > 0) {
        int step_ms = REVEAL_METER_BUDGET_MS / REVEAL_METER_STEPS;
        for (int step = 1; step <= REVEAL_METER_STEPS && !skip; step++) {
            ui_meter_pct = step * 100 / REVEAL_METER_STEPS;
            draw_frame(title, draw, ctx, count, sel, keys);
            if (input_poll(step_ms)) skip = true;
        }
    }
    ui_meter_pct = 100;

    draw_frame(title, draw, ctx, count, sel, keys);   // the settled, complete frame
}

static ui_choice_t ui_select_impl(const char *title, ui_draw_fn draw, const void *ctx,
                                  int count, int *sel, const char *letters,
                                  const char *hints, const char *keys, bool animate_meters) {
    ui_choice_t choice = { UI_UNKNOWN, 0, 0 };

    if (!ui_interactive()) {
        /* Long enough for any answer that means something. A longer line
           still drains the way it always has and still reads as an unknown
           choice, so the size only costs stack, never behaviour. */
        char line[128];
        screen_header(title);
        draw(ctx, -1);
        render_help_row(hints);
        printf("> ");
        if (!read_line(line, sizeof(line))) {
            choice.kind = UI_EOF;
            return choice;
        }
        char first = (char)tolower((unsigned char)line[0]);
        if (first != '\0' && strchr(letters, first) != NULL) {
            choice.kind = UI_LETTER;
            choice.letter = first;
            return choice;
        }
        int typed = ui_parse_choice(line);
        if (typed == 0) choice.kind = UI_BACK;
        else if (typed >= 1 && typed <= count) {
            choice.kind = UI_INDEX;
            choice.index = typed;
        }
        return choice;
    }

    /* A list that shrank under the caret would otherwise leave it pointing
       past the end of the rows it is drawn against. */
    if (*sel >= count) *sel = count - 1;
    if (*sel < 0) *sel = 0;

    render_raw_enter();
    bool first = true;
    for (bool settled = false; !settled; ) {
        if (first) {
            screen_open_reveal(title, draw, ctx, count, *sel, keys, animate_meters);
            first = false;
        } else {
            draw_frame(title, draw, ctx, count, *sel, keys);
        }

        key_event_t key = input_read_key();
        switch (key.type) {
            case KEY_UP:
                if (count > 0) *sel = (*sel + count - 1) % count;
                break;
            case KEY_DOWN:
                if (count > 0) *sel = (*sel + 1) % count;
                break;
            case KEY_ENTER:
                if (count > 0) {
                    choice.kind = UI_INDEX;
                    choice.index = *sel + 1;
                    settled = true;
                }
                break;
            /* Ctrl-C reaches us as a byte rather than a signal, and one level
               back is what someone pressing it on a submenu wants. The main
               menu reads the same answer as a quit. */
            case KEY_ESC:
            case KEY_INTERRUPT:
                choice.kind = UI_BACK;
                settled = true;
                break;
            case KEY_EOF:
                choice.kind = UI_EOF;
                settled = true;
                break;
            case KEY_CHAR: {
                char c = (char)tolower((unsigned char)key.ch);
                if (count > 0 && c == 'j') { *sel = (*sel + 1) % count; break; }
                if (count > 0 && c == 'k') { *sel = (*sel + count - 1) % count; break; }
                if (c == 'q' || c == '0') {
                    choice.kind = UI_BACK;
                    settled = true;
                } else if (c >= '1' && c <= '9' && c - '0' <= count) {
                    choice.kind = UI_INDEX;
                    choice.index = c - '0';
                    settled = true;
                } else if (strchr(letters, c) != NULL) {
                    choice.kind = UI_LETTER;
                    choice.letter = c;
                    settled = true;
                }
                break;
            }
            default:
                break;
        }
    }
    /* Canonical mode for whatever the choice leads to. Prompts get their echo
       and line editing back, and anything that blocks for a while (the device
       flow waits minutes) is interruptible again. */
    render_raw_leave();
    ui_status[0] = '\0';
    return choice;
}

static ui_choice_t ui_select(const char *title, ui_draw_fn draw, const void *ctx,
                             int count, int *sel, const char *letters,
                             const char *hints, const char *keys) {
    return ui_select_impl(title, draw, ctx, count, sel, letters, hints, keys, false);
}

/* Same as ui_select, but the first draw also ramps the screen's meters up
   from empty over their own budget. screen_projects is the only caller,
   since Projects is the only screen with a meter to animate. */
static ui_choice_t ui_select_meters(const char *title, ui_draw_fn draw, const void *ctx,
                                    int count, int *sel, const char *letters,
                                    const char *hints, const char *keys) {
    return ui_select_impl(title, draw, ctx, count, sel, letters, hints, keys, true);
}

/* Appends a closed/total bar onto a project row buffer. Nothing at all is
   appended unless a terminal is watching, which is what keeps the plain
   "  1) Name" line the e2e suite reads byte for byte: draw_projects builds
   the whole row in one buffer before handing it to ui_line, so the bar has to
   land in that buffer rather than going straight to stdout the way it used
   to. A project with no issues gets no bar either: an empty meter says less
   than the absence of one. */
static void project_progress_append(char *line, size_t n, const char *project_id) {
    if (!render_decorate()) return;

    issue_t *issues = NULL;
    int count = issue_service_list(project_id, &issues);
    if (count > 0) {
        int closed = 0;
        for (int i = 0; i < count; i++)
            if (issues[i].status == STATUS_CLOSED) closed++;
        /* The bar fills at ui_meter_pct of the real count so the Projects
           screen's first draw can ramp it up from empty; the printed
           fraction and its color stay on the real numbers throughout, so
           only the bar itself is seen animating. */
        char bar[64];
        render_meter(bar, sizeof(bar), closed * ui_meter_pct / 100, count, 12, render_utf8());
        size_t len = strlen(line);
        snprintf(line + len, n - len, "  %s%s%s %d/%d closed",
                 render_style(closed == count ? RENDER_OK : RENDER_ACCENT),
                 bar, render_reset(), closed, count);
    }
    free(issues);
}

/* Renders one issue in full: header line, description, then labels and
   assignees resolved from the ids stored on the issue into names a person
   reads on screen. Each printed line goes through ui_line on its own, which
   is what lets draw_issue_detail's box hold an arbitrarily long description
   without breaking: ui_line's own overflow handling cuts a line that's too
   wide rather than this function guessing at one. */
static void print_issue(const issue_t *is) {
    char line[DESC_LEN + 64];
    ui_line("");
    snprintf(line, sizeof(line), "#%d %s [%s]", is->issue_number, is->title,
             is->status == STATUS_OPEN ? "open" : "closed");
    ui_line(line);
    snprintf(line, sizeof(line), "%s", is->description[0] ? is->description : "(no description)");
    ui_line(line);

    /* issue_t only carries label/user ids, so we look each one up in the
       full list to print a name a person can actually read. */
    label_t *labels = NULL;
    int ln = label_service_list(&labels);
    if (ln < 0) ui_line("could not read from the database");

    char lbl_line[MAX_LABELS * (NAME_LEN + 24) + 16];
    int off = snprintf(lbl_line, sizeof(lbl_line), "labels:");
    if (is->label_count == 0) off += snprintf(lbl_line + off, sizeof(lbl_line) - (size_t)off, " (none)");
    for (int i = 0; i < is->label_count; i++) {
        const char *name = is->label_ids[i];
        for (int j = 0; j < ln; j++) {
            if (strcmp(labels[j].id, is->label_ids[i]) == 0) { name = labels[j].name; break; }
        }
        /* Colored off the name, since a label row has no color column of its
           own. Same name, same color, every screen. */
        off += snprintf(lbl_line + off, sizeof(lbl_line) - (size_t)off, " %s%s%s",
                         render_style(render_slot_for_label(name)), name, render_reset());
    }
    ui_line(lbl_line);
    free(labels);

    user_t *users = NULL;
    int un = user_service_list(&users);
    if (un < 0) ui_line("could not read from the database");

    char asg_line[MAX_ASSIGNEES * (USERNAME_LEN + 4) + 16];
    off = snprintf(asg_line, sizeof(asg_line), "assignees:");
    if (is->assignee_count == 0) off += snprintf(asg_line + off, sizeof(asg_line) - (size_t)off, " (none)");
    for (int i = 0; i < is->assignee_count; i++) {
        const char *name = is->assignee_ids[i];
        for (int j = 0; j < un; j++) {
            if (strcmp(users[j].id, is->assignee_ids[i]) == 0) { name = users[j].username; break; }
        }
        off += snprintf(asg_line + off, sizeof(asg_line) - (size_t)off, " %s", name);
    }
    ui_line(asg_line);
    free(users);
}

/* The letters behind the issue detail action rows, in the order they are
   drawn, so a caret landing on a row fires the same code as typing its
   letter. Labels and assignees are set once when the issue is created, so
   toggling open/closed is all that is left to do from here. */
static const char DETAIL_ACTIONS[] = "t";

static void draw_issue_detail(const void *ctx, int selected) {
    const issue_t *is = ctx;
    print_issue(is);
    ui_line("");
    if (selected < 0) return;   /* a typed screen lists its keys in the help row instead */

    static const char *const ROWS[] = {
        "t) toggle status"
    };
    for (int i = 0; i < (int)(sizeof(ROWS) / sizeof(ROWS[0])); i++) {
        char line[64];
        snprintf(line, sizeof(line), "%s%s%s", ui_row(i, selected, "  "), ROWS[i], render_reset());
        ui_line(line);
    }
}

/* Action screen for a single issue. Re-fetches the issue at the top of every
   loop pass instead of holding onto the struct across an edit, so a status
   toggle shows up on the very next redraw. Labels and assignees are set
   once at issue-creation time (see prompt_labels_and_assignees) and shown
   here read-only; the only action left on this screen is toggling
   open/closed -- there is no comments feature anymore. */
static void screen_issue_detail(const char *issue_id) {
    int sel = 0;
    for (;;) {
        issue_t is;
        if (issue_service_get(issue_id, &is) != SVC_OK) {
            note(RENDER_DANGER, "not found");
            return;
        }
        char title[32];
        snprintf(title, sizeof(title), "Issue #%d", is.issue_number);

        /* The action rows only become selectable where there is a caret to
           select them with. A digit typed at a piped run has always been an
           unknown choice here, and it stays one. */
        int rows = ui_interactive() ? (int)(sizeof(DETAIL_ACTIONS) - 1) : 0;
        ui_choice_t choice = ui_select(title, draw_issue_detail, &is, rows, &sel, DETAIL_ACTIONS,
                                       "t) toggle status   0) back",
                                       ui_keys("enter run   esc back"));
        if (choice.kind == UI_EOF || choice.kind == UI_BACK) return;

        char action = 0;
        if (choice.kind == UI_LETTER) action = choice.letter;
        else if (choice.kind == UI_INDEX) action = DETAIL_ACTIONS[choice.index - 1];

        if (action == 't') {
            issue_status_t next = is.status == STATUS_OPEN ? STATUS_CLOSED : STATUS_OPEN;
            report(issue_service_set_status(issue_id, next), "invalid request",
                   next == STATUS_CLOSED ? "issue closed" : "issue reopened");

        } else if (choice.kind == UI_UNKNOWN) {
            note(RENDER_DANGER, "unknown choice");
        }
    }
}

/* The issue list as one screen's worth of rows. The array belongs to the
   caller, which frees it on every branch below. */
typedef struct {
    const issue_t *items;
    int n;
} issue_view_t;

static void draw_issues(const void *ctx, int selected) {
    const issue_view_t *v = ctx;
    if (v->n < 0) { ui_line("could not read from the database"); return; }
    if (v->n == 0) { ui_empty("(no issues yet)", "press c to create the first one"); return; }
    for (int i = 0; i < v->n; i++) {
        char prefix[32], title[TITLE_LEN], line[TITLE_LEN + 64];
        snprintf(prefix, sizeof(prefix), "%d) #%d [%s] ", i + 1, v->items[i].issue_number,
                 v->items[i].status == STATUS_OPEN ? "open" : "closed");
        /* The title is the one field on this row long enough to actually
           overflow an 80-column box, so it gets its own budget-aware
           truncation rather than leaning on ui_line's plain-text fallback. */
        ui_fit_field(title, sizeof(title), v->items[i].title, (int)strlen(prefix));
        snprintf(line, sizeof(line), "%s%s%s%s", ui_row(i, selected, "  "), prefix, title, render_reset());
        ui_line(line);
    }
}

/* Prints one search or filter result set. Both blocks land below the frame
   rather than inside it, so the caller pauses afterwards on a screen that is
   about to repaint over them. */
static void print_matches(const issue_t *results, int n) {
    if (n < 0) printf("could not read from the database\n");
    else if (n == 0) printf("(no matches)\n");
    for (int i = 0; i < n; i++)
        printf("  #%d [%s] %s\n", results[i].issue_number,
               results[i].status == STATUS_OPEN ? "open" : "closed", results[i].title);
}

/* Parses a "1 3 5" / "1,3,5" style multi-select line into 1-based indices,
   writing up to max of them into out and returning how many were found.
   Anything that doesn't parse as a positive number is silently skipped
   rather than aborting the whole line, so one typo doesn't throw away the
   rest of a person's picks. */
static int parse_index_list(const char *line, int *out, int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end != p && v > 0) out[n++] = (int)v;
        p = end;
    }
    return n;
}

/* Offers to attach label(s) and assignee(s) to a just-created issue, right in
   the creation flow: there is no separate "add label" / "assign user" screen,
   so this is the only place either ever gets set. Blank input skips a section
   entirely; unknown indices are just skipped. Both lists print below the
   frame, like every other prompt sequence here, and the caller pauses once
   afterwards before the screen repaints over them. */
static void prompt_labels_and_assignees(const char *issue_id) {
    label_t *labels = NULL;
    int ln = label_service_list(&labels);
    if (ln > 0) {
        printf("\nlabels:\n");
        for (int i = 0; i < ln; i++) {
            printf("  %d) %s%s%s", i + 1,
                   render_style(render_slot_for_label(labels[i].name)),
                   labels[i].name, render_reset());
            if (labels[i].description[0]) printf(" - %s", labels[i].description);
            printf("\n");
        }
        char line[256];
        if (prompt_line("add label #s (space/comma separated, blank for none): ",
                        line, sizeof(line)) && line[0]) {
            int idxs[MAX_LABELS];
            int n = parse_index_list(line, idxs, MAX_LABELS);
            for (int i = 0; i < n; i++) {
                if (idxs[i] >= 1 && idxs[i] <= ln)
                    issue_service_add_label(issue_id, labels[idxs[i] - 1].id);
            }
        }
    }
    free(labels);

    user_t *users = NULL;
    int un = user_service_list(&users);
    if (un > 0) {
        printf("\nassignees:\n");
        for (int i = 0; i < un; i++) printf("  %d) %s\n", i + 1, users[i].username);
        char line[256];
        if (prompt_line("assign user #s (space/comma separated, blank for none): ",
                        line, sizeof(line)) && line[0]) {
            int idxs[MAX_ASSIGNEES];
            int n = parse_index_list(line, idxs, MAX_ASSIGNEES);
            for (int i = 0; i < n; i++) {
                if (idxs[i] >= 1 && idxs[i] <= un)
                    issue_service_add_assignee(issue_id, users[idxs[i] - 1].id);
            }
        }
    }
    free(users);
}

/* Issue list for one project, with create/search/filter and drill-down into
   a single issue. issue_service_list hands back a heap array each pass, so
   every branch below frees it before it returns or goes around again, the
   unknown choice branch included, or the array would leak on every mistyped
   line. */
static void screen_issues(const char *project_id, const char *project_name) {
    int sel = 0;
    for (;;) {
        issue_t *issues = NULL;
        int n = issue_service_list(project_id, &issues);
        issue_view_t view = { issues, n };
        char title[TITLE_LEN + 16];
        snprintf(title, sizeof(title), "Issues: %s", project_name);

        ui_choice_t choice = ui_select(title, draw_issues, &view, n > 0 ? n : 0, &sel, "cs",
                                       "  c) create   s) search   0) back",
                                       ui_keys("enter open   c create   s search   esc back"));
        if (choice.kind == UI_EOF || choice.kind == UI_BACK) { free(issues); return; }

        if (choice.kind == UI_INDEX) {
            char id[ID_LEN];
            snprintf(id, sizeof(id), "%s", issues[choice.index - 1].id);
            free(issues);
            screen_issue_detail(id);
            continue;
        }
        if (choice.kind == UI_UNKNOWN) {
            // a bad selection still has to release the list fetched above
            // before the loop goes around and fetches it again
            note(RENDER_DANGER, "unknown choice");
            free(issues);
            continue;
        }
        free(issues);

        if (choice.letter == 'c') {
            char new_title[TITLE_LEN], desc[DESC_LEN];
            if (!prompt_line("title: ", new_title, sizeof(new_title))) return;
            if (cancelled(new_title)) continue;
            /* An issue with no body is ordinary, so a blank line here is an
               answer rather than a way out of the prompt. */
            if (!prompt_line("description: ", desc, sizeof(desc))) return;
            issue_t created;
            svc_result_t r = issue_service_create(project_id, new_title, desc, &created);
            /* Labels and assignees are only ever set here, so the picker runs
               before the result line lands: report() writes the status row the
               next repaint shows, and the prompts below would otherwise print
               over it. */
            if (r == SVC_OK) {
                prompt_labels_and_assignees(created.id);
                ui_pause();
            }
            report(r, "title cannot be blank", "issue created");

        } else if (choice.letter == 's') {
            char kw[TITLE_LEN];
            if (!prompt_line("keyword: ", kw, sizeof(kw))) return;
            if (cancelled(kw)) continue;
            issue_t *results = NULL;
            int rn = issue_service_search(project_id, kw, &results);
            printf("-- results for \"%s\" --\n", kw);
            print_matches(results, rn);
            free(results);
            ui_pause();

        }
    }
}

typedef struct {
    const project_t *items;
    int n;
} project_view_t;

static void draw_projects(const void *ctx, int selected) {
    const project_view_t *v = ctx;
    if (v->n < 0) { ui_line("could not read from the database"); return; }
    if (v->n == 0) { ui_empty("(no projects yet)", "press c to create the first one"); return; }
    for (int i = 0; i < v->n; i++) {
        char prefix[16], name[NAME_LEN], line[NAME_LEN + 96];
        snprintf(prefix, sizeof(prefix), "%d) ", i + 1);
        ui_fit_field(name, sizeof(name), v->items[i].name, (int)strlen(prefix));
        snprintf(line, sizeof(line), "%s%s%s%s", ui_row(i, selected, "  "), prefix, name, render_reset());
        project_progress_append(line, sizeof(line), v->items[i].id);
        ui_line(line);
    }
}

/* Pulls the signed-in GitHub user's repos -- public and private alike, since
   github_list_repos asks GitHub for everything the token can see -- and adds
   any repo name not already tracked as a project. Called once on every visit
   to the Projects screen, so the list stays current with GitHub without a
   separate "My repos" screen.

   Routed through github_service rather than github.c directly: the service
   owns the token lookup and the spinner tick, so a slow fetch animates like
   every other blocking call here instead of freezing the screen. Without a
   saved token it answers GH_SVC_LOCAL and nothing happens. A name that is
   already a project comes back SVC_INVALID from project_service_create and
   is silently skipped. */
static void sync_projects_from_github(void) {
    char names[100][128];
    int n = 0;
    if (github_service_repos(names, 100, &n) != GH_SVC_OK) return;

    for (int i = 0; i < n; i++) {
        project_t created;
        project_service_create(names[i], &created);
    }
}

/* Top-level project list and create screen. Same discipline as the issues
   screen: the project array is freed on every branch, including a stray
   keypress that matches nothing, since it's about to be re-listed anyway. */
static void screen_projects(void) {
    int sel = 0;
    sync_projects_from_github();
    for (;;) {
        project_t *projects = NULL;
        int n = project_service_list(&projects);
        project_view_t view = { projects, n };

        ui_choice_t choice = ui_select_meters("Projects", draw_projects, &view, n > 0 ? n : 0, &sel, "c",
                                             "  c) create   0) back",
                                             ui_keys("enter open   c create   esc back"));
        if (choice.kind == UI_EOF || choice.kind == UI_BACK) { free(projects); return; }

        if (choice.kind == UI_INDEX) {
            char id[ID_LEN], name[NAME_LEN];
            snprintf(id, sizeof(id), "%s", projects[choice.index - 1].id);
            snprintf(name, sizeof(name), "%s", projects[choice.index - 1].name);
            free(projects);
            screen_issues(id, name);
            continue;
        }
        if (choice.kind == UI_UNKNOWN) {
            // unrecognized input still exits this pass of the loop, so the
            // list has to go before control gets back to the top
            printf("unknown choice\n");
            free(projects);
            continue;
        }
        free(projects);

        char name[NAME_LEN];
        if (!prompt_line("project name: ", name, sizeof(name))) return;
        if (cancelled(name)) continue;
        project_t created;
        report(project_service_create(name, &created), "blank or duplicate project name",
               "project created");
    }
}

typedef struct {
    const label_t *items;
    int n;
} label_view_t;

static void draw_labels(const void *ctx, int selected) {
    const label_view_t *v = ctx;
    if (v->n < 0) { ui_line("could not read from the database"); return; }
    if (v->n == 0) { ui_empty("(no labels yet)", "the catalog is seeded at startup"); return; }
    for (int i = 0; i < v->n; i++) {
        /* Name and description are colored/plain rather than truncated
           individually; a row long enough to overflow falls back to
           ui_line's plain-text ellipsis, which is rare enough here not to be
           worth splitting the color out of. */
        char line[NAME_LEN + LABEL_DESC_LEN + 64];
        int off = snprintf(line, sizeof(line), "%s%d) %s%s%s", ui_row(i, selected, "  "), i + 1,
                            render_style(render_slot_for_label(v->items[i].name)),
                            v->items[i].name, render_reset());
        if (v->items[i].description[0])
            off += snprintf(line + off, sizeof(line) - (size_t)off, " - %s", v->items[i].description);
        snprintf(line + off, sizeof(line) - (size_t)off, "%s", render_reset());
        ui_line(line);
    }
}

/* The label catalog, read only. The full set is seeded once at startup by
   db_labels_seed and every project picks from that same shared list, so
   there is nothing to create here. The rows can still be walked with the
   arrow keys, but there is nothing behind them: labels have no screen of
   their own, so Enter has nothing to open. */
static void screen_labels(void) {
    int sel = 0;
    for (;;) {
        label_t *labels = NULL;
        int n = label_service_list(&labels);
        label_view_t view = { labels, n };

        /* Numbers have never selected anything on this screen from a pipe,
           and they still read as an unknown choice there. */
        int rows = ui_interactive() && n > 0 ? n : 0;
        ui_choice_t choice = ui_select("Labels", draw_labels, &view, rows, &sel, "",
                                       "  0) back",
                                       ui_keys("esc back"));
        free(labels);
        if (choice.kind == UI_EOF || choice.kind == UI_BACK) return;
        if (choice.kind == UI_UNKNOWN) note(RENDER_DANGER, "unknown choice");
    }
}

typedef struct {
    const user_t *items;
    int n;
} user_view_t;

static void draw_users(const void *ctx, int selected) {
    const user_view_t *v = ctx;
    /* Starting a session always lands a local user, so an empty list is no
       longer the ordinary empty state: it means that row is not there, and
       the database is not keeping what we write to it. Not the usual
       "nothing here yet" case, so it gets a plain line rather than the
       two-line create-something hint. */
    if (v->n < 0) { ui_line("could not read from the database"); return; }
    if (v->n == 0) { ui_line("(no users found, the database may not be saving)"); return; }
    for (int i = 0; i < v->n; i++) {
        char line[USERNAME_LEN + 32];
        snprintf(line, sizeof(line), "%s- %s%s", ui_row(i, selected, "  "), v->items[i].username,
                 render_reset());
        ui_line(line);
    }
}

#define GH_SUGGEST_MAX 5

/* Same shape as screen_labels: list what's there, then let a signed-in user
   add to it. Typing a name and pressing enter doesn't add it outright --
   it searches GitHub for real accounts matching what was typed and shows
   up to GH_SUGGEST_MAX suggestions to pick from, the same "type, see real
   matches, pick one" idea as the web app's live autocomplete, just resolved
   in one round trip instead of on every keystroke since there's no
   keystroke-level event loop in a blocking terminal read. */
static void screen_assignees(void) {
    int sel = 0;
    for (;;) {
        user_t *users = NULL;
        int n = user_service_list(&users);
        user_view_t view = { users, n };

        int rows = ui_interactive() && n > 0 ? n : 0;
        ui_choice_t choice = ui_select("Assignees", draw_users, &view, rows, &sel, "c",
                                       "  c) create   0) back",
                                       ui_keys("c create   esc back"));
        free(users);
        if (choice.kind == UI_EOF || choice.kind == UI_BACK) return;

        if (choice.kind == UI_LETTER) {
            char query[USERNAME_LEN];
            if (!prompt_line("search github username: ", query, sizeof(query))) return;
            if (cancelled(query)) continue;

            /* The matches and the pick prompt both land below the frame, so
               the caller pauses once before the screen repaints over them. */
            char matches[GH_SUGGEST_MAX][128];
            int mn = github_search_usernames(query, matches, GH_SUGGEST_MAX);
            if (mn < 0) {
                note(RENDER_DANGER, "could not reach GitHub to search usernames");
                continue;
            }
            if (mn == 0) {
                char text[USERNAME_LEN + 40];
                snprintf(text, sizeof(text), "no GitHub accounts matched \"%s\"", query);
                note(RENDER_DANGER, text);
                continue;
            }

            printf("matches:\n");
            for (int i = 0; i < mn; i++) printf("  %d) %s\n", i + 1, matches[i]);
            char line[NAME_LEN];
            if (!prompt_line("pick #: ", line, sizeof(line))) return;
            int idx = ui_parse_choice(line);
            if (idx < 1 || idx > mn) { note(RENDER_DANGER, "unknown choice"); continue; }

            user_t created;
            report(user_service_create(matches[idx - 1], &created), "name taken or empty",
                   "user created");
        } else if (choice.kind == UI_UNKNOWN) {
            note(RENDER_DANGER, "unknown choice");
        }
    }
}

/* Advances and draws one spinner frame while a github_service call is in
   flight. Registered with github_service_set_tick only for the duration of
   that call, and only when a terminal is watching: a piped run registers
   nothing, so github_service never has a tick to invoke and prints exactly
   what it always has. ctx is the message to show beside the glyph. */
static int ui_spinner_frame = 0;
static bool ui_spinner_active = false;

static void ui_spinner_tick(void *ctx) {
    const char *message = ctx;
    printf("\r%s%s %s%s", render_style(RENDER_DIM),
           render_spinner_frame(ui_spinner_frame++, render_utf8()), message, render_reset());
    fflush(stdout);
    ui_spinner_active = true;
}

/* Wraps a github_service call that may block on the network. Only registers
   the tick when interactive, and always unregisters afterward so a later
   piped call in the same process never sees a stale one. */
static void spinner_begin(const char *message) {
    if (ui_interactive()) github_service_set_tick(ui_spinner_tick, (void *)message);
}

static void spinner_end(void) {
    github_service_set_tick(NULL, NULL);
    if (ui_spinner_active) {
        printf("\r\x1b[K");
        fflush(stdout);
        ui_spinner_active = false;
        ui_spinner_frame = 0;
    }
}

/* Startup identity. The service always leaves us signed in, as a real GitHub
   account when a saved token still works and as the local user otherwise, so
   only the first case is worth announcing: the local session is the ordinary
   way this program runs and saying so on every start would be noise. */
static void github_resume_session(void) {
    spinner_begin("checking GitHub session");
    gh_svc_result_t result = github_service_resume();
    spinner_end();
    if (result != GH_SVC_OK) return;
    char text[USERNAME_LEN + 32];
    snprintf(text, sizeof(text), "Signed in as %s", github_service_username());
    note(RENDER_OK, text);
}

/* Walks the person through the device flow: show them where to go and what
   to type, then block until the service hears back. The wait is the whole
   menu's wait, which is fine, since there is nothing useful to do here until
   they have finished in the browser anyway. Everything below prints straight
   out rather than into the status row, because the code has to stay on screen
   for as long as the person is typing it into a browser. */
static void github_login(void) {
    gh_login_prompt_t prompt;
    if (github_service_login_start(&prompt) != GH_SVC_OK) {
        note(RENDER_DANGER, "Set GH_CLIENT_ID and enable device flow on your GitHub OAuth app");
        return;
    }
    printf("Open %s and enter code: %s\n", prompt.verification_uri, prompt.user_code);

    bool token_saved = true;
    spinner_begin("waiting for authorization");
    gh_svc_result_t result = github_service_login_wait(&token_saved);
    spinner_end();
    /* Printed ahead of the outcome below because it qualifies it: the login
       itself worked, it just will not still be there next start. */
    if (!token_saved)
        printf("warning: could not save the login token, you will need to sign in again next start\n");

    switch (result) {
        case GH_SVC_OK:          printf("Signed in as %s\n", github_service_username()); break;
        case GH_SVC_NO_PROFILE:  printf("signed in, but fetching your GitHub profile failed; try again later\n"); break;
        case GH_SVC_DENIED:      printf("authorization denied\n"); break;
        case GH_SVC_EXPIRED:     printf("code expired, try again\n"); break;
        case GH_SVC_UNREACHABLE: printf("could not reach GitHub, try again\n"); break;
        case GH_SVC_TIMED_OUT:   printf("timed out waiting for authorization, try again\n"); break;
        /* Out of reach from here: we only wait after a start that worked, and
           LOCAL belongs to a session that never opened a flow at all. */
        case GH_SVC_UNAVAILABLE:
        case GH_SVC_LOCAL:       break;
    }
    ui_pause();
}

/* Drops the saved token and hands the session back to the same local
   identity that startup uses when there was never a token to begin with. */
static void github_logout(void) {
    github_service_logout();
    note(RENDER_OK, "Logged out of GitHub");
}

/* The main menu's rows, which depend on whether there is a GitHub identity.
   Signed out there is exactly one thing worth offering, since every screen
   below now leans on GitHub: Projects syncs from repos, Assignees resolves
   real accounts. Signed in, the full list. There is no "My repos" row any
   more; repos arrive on their own (see sync_projects_from_github). */
static void draw_main(const void *ctx, int selected) {
    const char *gh_user = ctx;
    char line[USERNAME_LEN + 48];
    if (!gh_user[0]) {
        snprintf(line, sizeof(line), "%s1) GitHub login%s", ui_row(0, selected, ""), render_reset());
        ui_line(line);
        return;
    }
    snprintf(line, sizeof(line), "%s1) Projects%s", ui_row(0, selected, ""), render_reset());
    ui_line(line);
    snprintf(line, sizeof(line), "%s2) Labels%s", ui_row(1, selected, ""), render_reset());
    ui_line(line);
    snprintf(line, sizeof(line), "%s3) Assignees%s", ui_row(2, selected, ""), render_reset());
    ui_line(line);
    snprintf(line, sizeof(line), "%s4) Log out (%s)%s", ui_row(3, selected, ""), gh_user, render_reset());
    ui_line(line);
}

/* Boots the session: play the splash, resume whatever GitHub identity (if
   any) was saved from last time, then loop the main menu until the user
   quits or stdin runs out.

   "Signed in" here means an actual GitHub identity, not the local fallback
   auth_ctx keeps so the service layer always has somebody to attribute a
   write to. Someone who has never signed in sees only the login and the
   quit, since GitHub is the source of truth for both repos and usernames.
   The caret count follows the row count, so the two states are one screen
   with a different number of rows rather than two code paths. */
void menu_run(void) {
    render_splash();
    github_resume_session();
    int sel = 0;
    for (;;) {
        const char *gh_user = github_service_username();
        bool signed_in = gh_user[0] != '\0';
        int rows = signed_in ? 4 : 1;
        /* sel belongs to the caller across screens, so a logout shrinking the
           menu under it would otherwise leave the caret past the last row. */
        if (sel >= rows) sel = rows - 1;

        ui_choice_t choice = ui_select("Main Menu", draw_main, gh_user, rows, &sel, "",
                                       "0) Quit", ui_keys("enter open   esc quit"));
        if (choice.kind == UI_UNKNOWN) { note(RENDER_DANGER, "unknown choice"); continue; }
        /* Back, quit and a closed stdin all mean the same thing at the top
           level: there is no level above this one to go back to. */
        if (choice.kind != UI_INDEX) return;

        if (!signed_in) {
            if (choice.index == 1) github_login();
            continue;
        }
        switch (choice.index) {
            case 1: screen_projects(); break;
            case 2: screen_labels(); break;
            case 3: screen_assignees(); break;
            case 4: github_logout(); break;
            default: break;
        }
    }
}
