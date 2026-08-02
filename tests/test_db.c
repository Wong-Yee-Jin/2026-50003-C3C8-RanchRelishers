#include "unity.h"
#include "db.h"
#include <stdlib.h>
#include <string.h>

/* Every case opens its own empty in-memory database. The flat version of this
   file shared one connection across all of its checks, so a row created for an
   early check was still there for the count assertions much further down, and
   the cases could not be read or run on their own. */
void setUp(void) {
    TEST_ASSERT_TRUE(db_init(":memory:"));   // schema applied on a fresh in-memory db
}

void tearDown(void) {
    db_shutdown();
}

/* Two projects whose issues overlap on both title and status. A search or
   filter that forgets its project scope pulls in the other project's issue
   and the count comes out wrong, instead of passing by luck. Project p holds
   "Login broken" (closed) and "Second" (open); p2 holds "Login button broken"
   (open). */
static void seed_two_projects(project_t *p, project_t *p2) {
    issue_t i1, i2, i3;
    TEST_ASSERT_TRUE(db_project_create("Backend", p));
    TEST_ASSERT_TRUE(db_issue_create(p->id, "Login broken", "steps...", &i1));
    TEST_ASSERT_TRUE(db_issue_create(p->id, "Second", "", &i2));
    TEST_ASSERT_TRUE(db_issue_set_status(i1.id, STATUS_CLOSED));
    TEST_ASSERT_TRUE(db_project_create("Mobile", p2));
    TEST_ASSERT_TRUE(db_issue_create(p2->id, "Login button broken", "steps...", &i3));
}

/* ---- Use case: Create Project ---- */

static void test_create_project_stores_name_and_id(void) {
    project_t p;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_EQUAL_size_t(24, strlen(p.id));
    TEST_ASSERT_EQUAL_STRING("Backend", p.name);
}

static void test_create_project_reports_a_taken_name(void) {
    project_t p;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_project_name_exists("Backend"));
    TEST_ASSERT_FALSE(db_project_name_exists("Missing"));
}

/* ---- Use case: View Projects ---- */

static void test_view_projects_finds_one_by_id(void) {
    project_t p, found;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_project_find_by_id(p.id, &found));
    TEST_ASSERT_EQUAL_STRING("Backend", found.name);
    TEST_ASSERT_FALSE(db_project_find_by_id("deadbeef", &found));
}

static void test_view_projects_lists_every_project(void) {
    project_t p;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_project_create("Frontend", &(project_t){0}));
    project_t *list = NULL;
    int n = db_project_list(&list);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_NOT_NULL(list);
    free(list);
}

/* ---- Use case: View Labels ---- */

static void test_view_labels_creates_a_label(void) {
    label_t lb;
    TEST_ASSERT_TRUE(db_label_create("bug", "defect", &lb));
    TEST_ASSERT_EQUAL_size_t(24, strlen(lb.id));
    TEST_ASSERT_TRUE(db_label_name_exists("bug"));
}

static void test_view_labels_finds_and_lists_labels(void) {
    label_t lb, lf;
    TEST_ASSERT_TRUE(db_label_create("bug", "defect", &lb));
    TEST_ASSERT_TRUE(db_label_find_by_id(lb.id, &lf));
    TEST_ASSERT_EQUAL_STRING("defect", lf.description);
    label_t *ll = NULL;
    int ln = db_label_list(&ll);
    TEST_ASSERT_EQUAL_INT(1, ln);
    free(ll);
}

static void test_view_labels_assigning_to_an_issue_is_idempotent(void) {
    project_t p;
    issue_t i1, got;
    label_t lb;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Login broken", "steps...", &i1));
    TEST_ASSERT_TRUE(db_label_create("bug", "defect", &lb));
    TEST_ASSERT_TRUE(db_issue_assign_label(i1.id, lb.id));
    TEST_ASSERT_TRUE(db_issue_assign_label(i1.id, lb.id));   // idempotent, no duplicate row
    TEST_ASSERT_TRUE(db_issue_find_by_id(i1.id, &got));
    TEST_ASSERT_EQUAL_INT(1, got.label_count);
    TEST_ASSERT_EQUAL_STRING(lb.id, got.label_ids[0]);
}

/* ---- Use case: Sign Up / Log In ---- */

/* Logging in again with the same GitHub account has to land on the existing
   row and refresh it, not create a second account for the same person. */
static void test_sign_up_upserts_the_github_user(void) {
    user_t u1, u2, uf;
    TEST_ASSERT_TRUE(db_user_upsert_github(42, "octocat", "The Octocat", "http://a/x.png", &u1));
    TEST_ASSERT_TRUE(db_user_upsert_github(42, "octocat2", "Octo Two", "http://a/y.png", &u2));
    TEST_ASSERT_EQUAL_STRING(u1.id, u2.id);              // same github_id maps to the same row
    TEST_ASSERT_EQUAL_STRING("octocat2", u2.username);   // fields updated in place
    TEST_ASSERT_TRUE(db_user_find_by_github_id(42, &uf));
    TEST_ASSERT_EQUAL_INT64(42, uf.github_id);
    user_t *ul = NULL;
    int un = db_user_list(&ul);
    TEST_ASSERT_EQUAL_INT(1, un);
    free(ul);
}

/* ---- Use case: Create Issue ---- */

static void test_create_issue_numbers_issues_per_project(void) {
    project_t p;
    issue_t i1, i2;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Login broken", "steps...", &i1));
    TEST_ASSERT_EQUAL_INT(1, i1.issue_number);
    TEST_ASSERT_EQUAL_INT(STATUS_OPEN, i1.status);
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Second", "", &i2));
    TEST_ASSERT_EQUAL_INT(2, i2.issue_number);   // numbering is per project, from MAX+1
}

static void test_create_issue_tolerates_a_null_title(void) {
    project_t p;
    issue_t io;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, NULL, "d", &io));   // NULL title must not crash
    TEST_ASSERT_EQUAL_STRING("", io.title);
}

/* ---- Use case: Close Issue ---- */

static void test_close_issue_sets_status_closed(void) {
    project_t p;
    issue_t i1, got;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Login broken", "steps...", &i1));
    TEST_ASSERT_TRUE(db_issue_set_status(i1.id, STATUS_CLOSED));
    TEST_ASSERT_TRUE(db_issue_find_by_id(i1.id, &got));
    TEST_ASSERT_EQUAL_INT(STATUS_CLOSED, got.status);
}

/* ---- Use case: Assign User to Issue ---- */

static void test_assign_user_to_issue_records_the_assignee(void) {
    project_t p;
    issue_t i1, got;
    user_t u1;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Login broken", "steps...", &i1));
    TEST_ASSERT_TRUE(db_user_upsert_github(42, "octocat", "The Octocat", "http://a/x.png", &u1));
    TEST_ASSERT_TRUE(db_issue_assign_user(i1.id, u1.id));
    TEST_ASSERT_TRUE(db_issue_find_by_id(i1.id, &got));
    TEST_ASSERT_EQUAL_INT(1, got.assignee_count);
}

/* ---- Use case: View Issue ---- */

static void test_view_issue_lists_the_issues_in_a_project(void) {
    project_t p, p2;
    seed_two_projects(&p, &p2);
    issue_t *il = NULL;
    int in = db_issue_list_by_project(p.id, &il);
    TEST_ASSERT_EQUAL_INT(2, in);   // p2's issue must not leak in
    free(il);
}

static void test_view_issue_filters_by_status_within_the_project(void) {
    project_t p, p2;
    seed_two_projects(&p, &p2);
    issue_t *f = NULL;
    int fn = db_issue_filter(p.id, "open", NULL, 50, &f);   // "Second" is open, "Login broken" was closed
    TEST_ASSERT_EQUAL_INT(1, fn);   // p2's issue is open too but must not leak in
    free(f);
}

static void test_view_issue_lists_comments_oldest_first(void) {
    project_t p;
    issue_t i1;
    TEST_ASSERT_TRUE(db_project_create("Backend", &p));
    TEST_ASSERT_TRUE(db_issue_create(p.id, "Login broken", "steps...", &i1));
    TEST_ASSERT_TRUE(db_comment_add(i1.id, "first note"));
    TEST_ASSERT_TRUE(db_comment_add(i1.id, "second note"));
    comment_t *cl = NULL;
    int cn = db_comment_list_by_issue(i1.id, &cl);
    TEST_ASSERT_EQUAL_INT(2, cn);
    TEST_ASSERT_EQUAL_STRING("first note", cl[0].text);   // ordered by created_at ascending
    free(cl);
}

/* ---- Use case: Search Issue ---- */

static void test_search_issue_is_scoped_to_project(void) {
    project_t p, p2;
    seed_two_projects(&p, &p2);
    issue_t *s = NULL;
    int sn = db_issue_search(p.id, "login", 50, &s);   // case-insensitive substring, scoped to p
    TEST_ASSERT_EQUAL_INT(1, sn);   // p2's "Login button broken" matches too but lives elsewhere
    free(s);
}

/* A literal percent has to be escaped before it reaches LIKE, otherwise a
   person searching for "%" would silently match every issue. */
static void test_search_issue_treats_percent_as_a_literal(void) {
    project_t p, p2;
    seed_two_projects(&p, &p2);
    issue_t *s = NULL;
    int sn2 = db_issue_search(p.id, "%", 50, &s);
    TEST_ASSERT_EQUAL_INT(0, sn2);
    free(s);
}

static void test_search_issue_with_a_null_keyword_returns_the_project(void) {
    project_t p, p2;
    seed_two_projects(&p, &p2);
    issue_t *sg = NULL;
    int sgn = db_issue_search(p.id, NULL, 50, &sg);   // NULL keyword must not crash
    TEST_ASSERT_EQUAL_INT(2, sgn);   // p's two issues only, p2's is scoped out
    free(sg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_project_stores_name_and_id);
    RUN_TEST(test_create_project_reports_a_taken_name);
    RUN_TEST(test_view_projects_finds_one_by_id);
    RUN_TEST(test_view_projects_lists_every_project);
    RUN_TEST(test_view_labels_creates_a_label);
    RUN_TEST(test_view_labels_finds_and_lists_labels);
    RUN_TEST(test_view_labels_assigning_to_an_issue_is_idempotent);
    RUN_TEST(test_sign_up_upserts_the_github_user);
    RUN_TEST(test_create_issue_numbers_issues_per_project);
    RUN_TEST(test_create_issue_tolerates_a_null_title);
    RUN_TEST(test_close_issue_sets_status_closed);
    RUN_TEST(test_assign_user_to_issue_records_the_assignee);
    RUN_TEST(test_view_issue_lists_the_issues_in_a_project);
    RUN_TEST(test_view_issue_filters_by_status_within_the_project);
    RUN_TEST(test_view_issue_lists_comments_oldest_first);
    RUN_TEST(test_search_issue_is_scoped_to_project);
    RUN_TEST(test_search_issue_treats_percent_as_a_literal);
    RUN_TEST(test_search_issue_with_a_null_keyword_returns_the_project);
    return UNITY_END();
}
