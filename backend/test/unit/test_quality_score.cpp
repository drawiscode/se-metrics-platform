#include "db/db.h"
#include "quality/quality_score.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require_true(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message + " actual=" + std::to_string(actual) +
                                 " expected=" + std::to_string(expected));
    }
}

double expected_score(double penalty, int lines_analyzed, int files_with_issues)
{
    if (penalty <= 0.0) return 100.0;
    double density = 0.0;
    if (lines_analyzed > 0) {
        density = penalty * 1000.0 / static_cast<double>(lines_analyzed);
    } else {
        density = penalty / static_cast<double>(std::max(1, files_with_issues));
    }
    double score = 100.0 - 10.0 * std::log1p(density);
    if (score < 0.0) return 0.0;
    if (score > 100.0) return 100.0;
    return score;
}

void seed_repo(Db& db)
{
    db.exec("INSERT INTO repos(id, full_name) VALUES (1, 'sitaowang/quality-test');");
}

void insert_quality_issue(Db& db,
                          const std::string& tool,
                          const std::string& file,
                          const std::string& severity,
                          const std::string& status)
{
    db.exec("INSERT INTO quality_issues(repo_id, tool, issue_key, file_path, line, column, "
            "rule_id, severity, message, status) VALUES "
            "(1, '" + tool + "', '" + tool + "-" + file + "-" + severity + "', '" + file +
            "', 10, 2, 'rule.test', '" + severity + "', 'test issue', '" + status + "');");
}

void insert_quality_run(Db& db,
                        int run_id,
                        const std::string& tools,
                        int lines_analyzed)
{
    db.exec("INSERT INTO quality_analysis_runs(id, repo_id, branch, tools, mode, status, "
            "lines_analyzed, started_at) VALUES (" +
            std::to_string(run_id) + ", 1, 'main', '" + tools +
            "', 'full', 'Finished', " + std::to_string(lines_analyzed) +
            ", datetime('now'));");
}

void insert_run_issue(Db& db,
                      int run_id,
                      const std::string& tool,
                      const std::string& file,
                      const std::string& severity)
{
    db.exec("INSERT INTO quality_run_issues(run_id, repo_id, tool, issue_key, file_path, line, "
            "column, rule_id, severity, message) VALUES (" +
            std::to_string(run_id) + ", 1, '" + tool + "', '" + tool + "-" + file + "-" +
            severity + "', '" + file + "', 8, 1, 'rule.test', '" + severity +
            "', 'test run issue');");
}

void test_empty_repo_scores_100()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    QualityScore score = compute_quality_score(db, 1, "");
    require_true(score.total_issues == 0, "empty repo should have no quality issues");
    require_near(score.score, 100.0, 0.0001, "empty repo should score 100");
}

void test_active_issue_score_uses_severity_and_tool_weights()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "cppcheck,pylint,checkstyle", 1000);
    insert_quality_issue(db, "cppcheck", "src/main.cpp", "error", "active");
    insert_quality_issue(db, "pylint", "scripts/check.py", "warning", "active");
    insert_quality_issue(db, "pylint", "scripts/check.py", "warning", "active");
    insert_quality_issue(db, "checkstyle", "src/App.java", "style", "active");
    insert_quality_issue(db, "cppcheck", "src/legacy.cpp", "error", "fixed");

    QualityScore score = compute_quality_score(db, 1, "");

    const double expected_penalty = 10.0 + 4.0 + 0.1;
    require_true(score.total_issues == 4, "score should count active issues only");
    require_true(score.files_with_issues == 3, "score should count distinct active files only");
    require_true(score.severity_counts["error"] == 1, "score should count active errors");
    require_true(score.severity_counts["warning"] == 2, "score should count active warnings");
    require_true(score.severity_counts["style"] == 1, "score should count active style issues");
    require_near(score.penalty, expected_penalty, 0.0001,
                 "score should apply severity and tool weights");
    require_near(score.score, expected_score(expected_penalty, 1000, 3), 0.0001,
                 "score should use issue density based on analyzed lines");
}

void test_tool_filter_uses_latest_matching_run_lines()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "pylint", 500);
    insert_quality_run(db, 2, "cppcheck,pylint", 1200);
    insert_quality_issue(db, "cppcheck", "src/main.cpp", "error", "active");
    insert_quality_issue(db, "pylint", "scripts/a.py", "warning", "active");
    insert_quality_issue(db, "pylint", "scripts/b.py", "warning", "active");

    QualityScore score = compute_quality_score(db, 1, "pylint");

    const double expected_penalty = 4.0;
    require_true(score.total_issues == 2, "tool filter should count only selected tool issues");
    require_true(score.files_with_issues == 2, "tool filter should count selected tool files");
    require_true(score.lines_analyzed == 500, "tool filter should prefer latest exact tool run");
    require_near(score.penalty, expected_penalty, 0.0001,
                 "pylint warnings should use pylint tool weight");
    require_near(score.score, expected_score(expected_penalty, 500, 2), 0.0001,
                 "tool-filtered score should use matching run lines");
}

void test_run_score_uses_run_snapshot_instead_of_current_issue_state()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 10, "cppcheck,pylint", 800);
    insert_run_issue(db, 10, "cppcheck", "src/main.cpp", "error");
    insert_run_issue(db, 10, "pylint", "scripts/a.py", "warning");
    insert_quality_issue(db, "cppcheck", "src/main.cpp", "error", "fixed");

    QualityScore score = compute_quality_score_for_run(db, 10);

    const double expected_penalty = 10.0 + 2.0;
    require_true(score.total_issues == 2, "run score should read quality_run_issues snapshot");
    require_true(score.files_with_issues == 2, "run score should count run snapshot files");
    require_true(score.lines_analyzed == 800, "run score should use run lines_analyzed");
    require_near(score.penalty, expected_penalty, 0.0001,
                 "run score should apply tool weights to run issues");
    require_near(score.score, expected_score(expected_penalty, 800, 2), 0.0001,
                 "run score should use run-level issue density");
}

void test_score_json_escapes_severity_keys()
{
    QualityScore score;
    score.score = 98.5;
    score.penalty = 1.2;
    score.total_issues = 1;
    score.files_with_issues = 1;
    score.lines_analyzed = 200;
    score.severity_counts["custom\"severity"] = 1;

    const std::string json = quality_score_to_json(score);
    require_true(json.find("\"score\":98.5") != std::string::npos,
                 "score json should include score");
    require_true(json.find("custom\\\"severity") != std::string::npos,
                 "score json should escape severity keys");
}

void test_empty_status_is_treated_as_active()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "cppcheck", 300);
    insert_quality_issue(db, "cppcheck", "src/no_status.cpp", "warning", "");

    QualityScore score = compute_quality_score(db, 1, "");

    require_true(score.total_issues == 1, "empty issue status should be treated as active");
    require_true(score.severity_counts["warning"] == 1,
                 "empty status issue should contribute to severity counts");
    require_near(score.penalty, 4.0, 0.0001,
                 "empty status issue should contribute to penalty");
}

void test_unknown_severity_uses_default_weight()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "custom-tool", 400);
    insert_quality_issue(db, "custom-tool", "src/custom.cpp", "custom-risk", "active");

    QualityScore score = compute_quality_score(db, 1, "");

    require_true(score.total_issues == 1, "unknown severity issue should be counted");
    require_near(score.penalty, 1.0, 0.0001,
                 "unknown severity and tool should use default weights");
    require_near(score.score, expected_score(1.0, 400, 1), 0.0001,
                 "unknown severity should still produce a density-based score");
}

void test_score_falls_back_to_file_density_when_lines_are_missing()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_issue(db, "cppcheck", "src/a.cpp", "error", "active");
    insert_quality_issue(db, "cppcheck", "src/b.cpp", "warning", "active");

    QualityScore score = compute_quality_score(db, 1, "");

    const double expected_penalty = 14.0;
    require_true(score.lines_analyzed == 0, "score should have no analyzed lines without runs");
    require_true(score.files_with_issues == 2, "fallback density should know affected files");
    require_near(score.score, expected_score(expected_penalty, 0, 2), 0.0001,
                 "score should fall back to per-file density when lines are missing");
}

void test_cpplint_style_issues_have_low_penalty()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "cpplint", 1000);
    insert_quality_issue(db, "cpplint", "src/style.cpp", "style", "active");
    insert_quality_issue(db, "cpplint", "src/style.cpp", "style", "active");

    QualityScore score = compute_quality_score(db, 1, "cpplint");

    const double expected_penalty = 0.4 * 0.08 * 2.0;
    require_true(score.total_issues == 2, "cpplint issues should be counted");
    require_near(score.penalty, expected_penalty, 0.0001,
                 "cpplint style issues should have low weighted penalty");
}

void test_aggregate_score_prefers_multi_tool_run_lines()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 1, "cppcheck", 3000);
    insert_quality_run(db, 2, "cppcheck,pylint", 900);
    insert_quality_issue(db, "cppcheck", "src/main.cpp", "warning", "active");

    QualityScore score = compute_quality_score(db, 1, "");

    require_true(score.lines_analyzed == 900,
                 "aggregate score should use multi-tool run lines when available");
}

void test_empty_run_snapshot_scores_100()
{
    Db db(":memory:");
    db.init_schema();
    seed_repo(db);

    insert_quality_run(db, 20, "cppcheck", 500);

    QualityScore score = compute_quality_score_for_run(db, 20);

    require_true(score.total_issues == 0, "empty run snapshot should have no issues");
    require_near(score.score, 100.0, 0.0001, "empty run snapshot should score 100");
}

} // namespace

int main()
{
    try {
        test_empty_repo_scores_100();
        test_active_issue_score_uses_severity_and_tool_weights();
        test_tool_filter_uses_latest_matching_run_lines();
        test_run_score_uses_run_snapshot_instead_of_current_issue_state();
        test_score_json_escapes_severity_keys();
        test_empty_status_is_treated_as_active();
        test_unknown_severity_uses_default_weight();
        test_score_falls_back_to_file_density_when_lines_are_missing();
        test_cpplint_style_issues_have_low_penalty();
        test_aggregate_score_prefers_multi_tool_run_lines();
        test_empty_run_snapshot_scores_100();
        std::cout << "quality_score_unit_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "quality_score_unit_tests failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
}
