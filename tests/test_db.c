#include "test.h"
#include "db.h"
#include <stdlib.h>

int main(void) {
    CHECK(db_init(":memory:") == true);   // schema applied on a fresh in-memory db

    project_t p;
    CHECK(db_project_create("Backend", &p) == true);
    CHECK(strlen(p.id) == 24);
    CHECK_STR(p.name, "Backend");

    CHECK(db_project_name_exists("Backend") == true);
    CHECK(db_project_name_exists("Missing") == false);

    project_t found;
    CHECK(db_project_find_by_id(p.id, &found) == true);
    CHECK_STR(found.name, "Backend");
    CHECK(db_project_find_by_id("deadbeef", &found) == false);

    db_project_create("Frontend", &(project_t){0});
    project_t *list = NULL;
    int n = db_project_list(&list);
    CHECK(n == 2);
    CHECK(list != NULL);
    free(list);

    label_t lb;
    CHECK(db_label_create("bug", "defect", &lb) == true);
    CHECK(strlen(lb.id) == 24);
    CHECK(db_label_name_exists("bug") == true);
    label_t lf; CHECK(db_label_find_by_id(lb.id, &lf) == true);
    CHECK_STR(lf.description, "defect");
    label_t *ll = NULL; int ln = db_label_list(&ll); CHECK(ln == 1); free(ll);

    user_t u1;
    CHECK(db_user_upsert_github(42, "octocat", "The Octocat", "http://a/x.png", &u1) == true);
    user_t u2;
    CHECK(db_user_upsert_github(42, "octocat2", "Octo Two", "http://a/y.png", &u2) == true);
    CHECK_STR(u1.id, u2.id);                 // same github_id maps to the same row
    CHECK_STR(u2.username, "octocat2");      // fields updated in place
    user_t uf; CHECK(db_user_find_by_github_id(42, &uf) == true);
    CHECK(uf.github_id == 42);
    user_t *ul = NULL; int un = db_user_list(&ul); CHECK(un == 1); free(ul);

    issue_t i1;
    CHECK(db_issue_create(p.id, "Login broken", "steps...", &i1) == true);
    CHECK(i1.issue_number == 1);
    CHECK(i1.status == STATUS_OPEN);
    issue_t i2;
    db_issue_create(p.id, "Second", "", &i2);
    CHECK(i2.issue_number == 2);              // numbering is per project, from MAX+1

    CHECK(db_issue_set_status(i1.id, STATUS_CLOSED) == true);
    issue_t got; db_issue_find_by_id(i1.id, &got);
    CHECK(got.status == STATUS_CLOSED);

    CHECK(db_issue_assign_label(i1.id, lb.id) == true);
    CHECK(db_issue_assign_label(i1.id, lb.id) == true);   // idempotent, no duplicate row
    db_issue_find_by_id(i1.id, &got);
    CHECK(got.label_count == 1);
    CHECK_STR(got.label_ids[0], lb.id);

    CHECK(db_issue_assign_user(i1.id, u1.id) == true);
    db_issue_find_by_id(i1.id, &got);
    CHECK(got.assignee_count == 1);

    issue_t *il = NULL; int in = db_issue_list_by_project(p.id, &il);
    CHECK(in == 2); free(il);

    // a second project with an issue that would collide with the searches
    // and filters below if project scoping is broken
    project_t p2;
    CHECK(db_project_create("Mobile", &p2) == true);
    issue_t i3;
    CHECK(db_issue_create(p2.id, "Login button broken", "steps...", &i3) == true);

    // i1 title "Login broken", i2 title "Second" from Task 1.6
    issue_t *s = NULL;
    int sn = db_issue_search(p.id, "login", 50, &s);   // case-insensitive substring, scoped to p
    CHECK(sn == 1); free(s);                            // i3 also matches "login" but lives in p2

    int sn2 = db_issue_search(p.id, "%", 50, &s);       // literal percent, not a match-all
    CHECK(sn2 == 0); free(s);                            // proves % is escaped, not a wildcard

    issue_t *f = NULL;
    int fn = db_issue_filter(p.id, "open", NULL, 50, &f);   // i2 is open, i1 was closed
    CHECK(fn == 1); free(f);                                 // i3 is open too but must not leak in from p2

    CHECK(db_comment_add(i1.id, "first note") == true);
    CHECK(db_comment_add(i1.id, "second note") == true);
    comment_t *cl = NULL; int cn = db_comment_list_by_issue(i1.id, &cl);
    CHECK(cn == 2);
    CHECK_STR(cl[0].text, "first note");   // ordered by created_at ascending
    free(cl);

    issue_t *sg = NULL;
    int sgn = db_issue_search(p.id, NULL, 50, &sg);   // NULL keyword must not crash, matches all rows in p
    CHECK(sgn == 2);   // i1 and i2 only, i3 is scoped out because it belongs to p2
    free(sg);

    issue_t io;
    CHECK(db_issue_create(p.id, NULL, "d", &io) == true);   // NULL title must not crash
    CHECK_STR(io.title, "");

    db_shutdown();
    return TEST_SUMMARY();
}
