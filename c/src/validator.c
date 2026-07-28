#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sheet_dimensions {
    size_t rows;
    size_t columns;
} sheet_dimensions;

typedef struct dimension_vote {
    sheet_dimensions dimensions;
    size_t count;
    size_t first_workbook_index;
} dimension_vote;

typedef struct template_score {
    size_t sheet_count;
    size_t total_rows;
    size_t total_columns;
    size_t non_empty_cells;
} template_score;

static int string_list_contains(
    char *const *items,
    size_t count,
    const char *value
)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        if (strcmp(items[index], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int string_list_append(
    char ***items,
    size_t *count,
    const char *value,
    xls_error *error
)
{
    char **replacement;
    char *copy;
    replacement = (char **)realloc(
        *items, (*count + 1) * sizeof(**items)
    );
    if (replacement == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "记录校验结果时内存不足");
        return 0;
    }
    *items = replacement;
    copy = xls_strdup(value);
    if (copy == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "复制工作表名称时内存不足");
        return 0;
    }
    (*items)[(*count)++] = copy;
    return 1;
}

static int add_issue(
    xls_validation_report *report,
    xls_validation_issue_code code,
    size_t workbook_index,
    const char *filename,
    const char *sheet_name,
    const char *message,
    xls_error *error
)
{
    xls_validation_issue *replacement;
    xls_validation_issue *issue;
    replacement = (xls_validation_issue *)realloc(
        report->issues, (report->issue_count + 1) * sizeof(*report->issues)
    );
    if (replacement == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "记录校验问题时内存不足");
        return 0;
    }
    report->issues = replacement;
    issue = &report->issues[report->issue_count++];
    memset(issue, 0, sizeof(*issue));
    issue->code = code;
    issue->workbook_index = workbook_index;
    issue->filename = xls_strdup(filename);
    issue->sheet_name = xls_strdup(sheet_name);
    issue->message = xls_strdup(message);
    if (issue->filename == NULL || issue->sheet_name == NULL
        || issue->message == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "复制校验问题时内存不足");
        return 0;
    }
    return 1;
}

static sheet_dimensions effective_dimensions(const xls_sheet *sheet)
{
    sheet_dimensions dimensions;
    dimensions.rows = xls_sheet_effective_row_count(sheet);
    dimensions.columns = xls_sheet_effective_column_count(sheet);
    return dimensions;
}

static int dimensions_equal(sheet_dimensions left, sheet_dimensions right)
{
    return left.rows == right.rows && left.columns == right.columns;
}

static int dimensions_larger(sheet_dimensions left, sheet_dimensions right)
{
    if (left.rows != right.rows) {
        return left.rows > right.rows;
    }
    return left.columns > right.columns;
}

static size_t sheet_non_empty_cells(const xls_sheet *sheet)
{
    size_t count = 0;
    size_t row;
    size_t column;
    for (row = 0; row < sheet->row_count; ++row) {
        for (column = 0; column < sheet->column_count; ++column) {
            const xls_cell *cell = xls_sheet_cell_const(sheet, row, column);
            if (cell != NULL && !xls_string_empty(cell->value)) {
                ++count;
            }
        }
    }
    return count;
}

static template_score workbook_score(
    const xls_workbook *workbook,
    char *const *relevant_sheets,
    size_t relevant_count
)
{
    template_score score;
    size_t index;
    memset(&score, 0, sizeof(score));
    for (index = 0; index < workbook->sheet_count; ++index) {
        sheet_dimensions dimensions;
        if (relevant_count > 0
            && !string_list_contains(
                relevant_sheets,
                relevant_count,
                workbook->sheets[index].name
            )) {
            continue;
        }
        dimensions = effective_dimensions(&workbook->sheets[index]);
        ++score.sheet_count;
        score.total_rows += dimensions.rows;
        score.total_columns += dimensions.columns;
        score.non_empty_cells += sheet_non_empty_cells(&workbook->sheets[index]);
    }
    return score;
}

static int score_larger(template_score left, template_score right)
{
    if (left.sheet_count != right.sheet_count) {
        return left.sheet_count > right.sheet_count;
    }
    if (left.total_rows != right.total_rows) {
        return left.total_rows > right.total_rows;
    }
    if (left.total_columns != right.total_columns) {
        return left.total_columns > right.total_columns;
    }
    return left.non_empty_cells > right.non_empty_cells;
}

static size_t choose_template(
    const xls_workbook *workbooks,
    const size_t *candidate_indexes,
    size_t candidate_count,
    char *const *relevant_sheets,
    size_t relevant_count
)
{
    size_t best = candidate_indexes[0];
    template_score best_score = workbook_score(
        &workbooks[best], relevant_sheets, relevant_count
    );
    size_t index;
    for (index = 1; index < candidate_count; ++index) {
        size_t candidate = candidate_indexes[index];
        template_score score = workbook_score(
            &workbooks[candidate], relevant_sheets, relevant_count
        );
        if (score_larger(score, best_score)) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

static int collect_ordered_sheet_names(
    const xls_workbook *workbooks,
    const size_t *candidate_indexes,
    size_t candidate_count,
    size_t preferred_workbook,
    char ***names,
    size_t *name_count,
    xls_error *error
)
{
    size_t pass;
    for (pass = 0; pass < candidate_count; ++pass) {
        size_t candidate_position;
        const xls_workbook *workbook;
        size_t sheet_index;
        if (pass == 0) {
            candidate_position = 0;
            while (candidate_position < candidate_count
                && candidate_indexes[candidate_position] != preferred_workbook) {
                ++candidate_position;
            }
            if (candidate_position == candidate_count) {
                continue;
            }
        } else {
            candidate_position = pass - 1;
            if (candidate_indexes[candidate_position] == preferred_workbook) {
                candidate_position = candidate_count - 1;
            }
            if (candidate_position >= candidate_count
                || candidate_indexes[candidate_position] == preferred_workbook) {
                continue;
            }
        }
        workbook = &workbooks[candidate_indexes[candidate_position]];
        for (sheet_index = 0; sheet_index < workbook->sheet_count; ++sheet_index) {
            const char *name = workbook->sheets[sheet_index].name;
            if (!string_list_contains(*names, *name_count, name)
                && !string_list_append(names, name_count, name, error)) {
                return 0;
            }
        }
    }
    return 1;
}

static sheet_dimensions dominant_dimensions(
    const xls_workbook *workbooks,
    const size_t *candidate_indexes,
    size_t candidate_count,
    const char *sheet_name
)
{
    dimension_vote *votes = (dimension_vote *)calloc(
        candidate_count, sizeof(*votes)
    );
    size_t vote_count = 0;
    size_t index;
    size_t best = 0;
    sheet_dimensions result;
    memset(&result, 0, sizeof(result));
    if (votes == NULL) {
        return result;
    }
    for (index = 0; index < candidate_count; ++index) {
        const xls_sheet *sheet = xls_workbook_find_sheet(
            &workbooks[candidate_indexes[index]], sheet_name
        );
        sheet_dimensions dimensions = effective_dimensions(sheet);
        size_t vote;
        for (vote = 0; vote < vote_count; ++vote) {
            if (dimensions_equal(votes[vote].dimensions, dimensions)) {
                ++votes[vote].count;
                break;
            }
        }
        if (vote == vote_count) {
            votes[vote_count].dimensions = dimensions;
            votes[vote_count].count = 1;
            votes[vote_count].first_workbook_index = candidate_indexes[index];
            ++vote_count;
        }
    }
    for (index = 1; index < vote_count; ++index) {
        if (votes[index].count > votes[best].count
            || (votes[index].count == votes[best].count
                && dimensions_larger(
                    votes[index].dimensions, votes[best].dimensions
                ))
            || (votes[index].count == votes[best].count
                && dimensions_equal(
                    votes[index].dimensions, votes[best].dimensions
                )
                && votes[index].first_workbook_index
                    < votes[best].first_workbook_index)) {
            best = index;
        }
    }
    if (vote_count > 0) {
        result = votes[best].dimensions;
    }
    free(votes);
    return result;
}

int xls_validate_workbooks(
    const xls_workbook *workbooks,
    size_t workbook_count,
    xls_validation_report *report,
    xls_error *error
)
{
    size_t *candidate_indexes;
    size_t candidate_count = 0;
    char **ordered_names = NULL;
    size_t ordered_name_count = 0;
    size_t preliminary_template;
    size_t index;
    if (report == NULL || (workbooks == NULL && workbook_count > 0)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid validation arguments");
        return 0;
    }
    memset(report, 0, sizeof(*report));
    report->readiness = XLS_MERGE_BLOCKED;
    candidate_indexes = workbook_count == 0
        ? NULL
        : (size_t *)malloc(workbook_count * sizeof(*candidate_indexes));
    if (workbook_count > 0 && candidate_indexes == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "校验工作簿时内存不足");
        return 0;
    }
    for (index = 0; index < workbook_count; ++index) {
        if (workbooks[index].sheet_count == 0) {
            if (!add_issue(
                report,
                XLS_ISSUE_EMPTY_WORKBOOK,
                index,
                workbooks[index].filename,
                "",
                "工作簿中没有可用工作表，已跳过",
                error
            )) {
                free(candidate_indexes);
                xls_validation_report_free(report);
                return 0;
            }
        } else {
            candidate_indexes[candidate_count++] = index;
        }
    }
    if (candidate_count == 0) {
        free(candidate_indexes);
        xls_error_clear(error);
        return 1;
    }
    preliminary_template = choose_template(
        workbooks, candidate_indexes, candidate_count, NULL, 0
    );
    if (!collect_ordered_sheet_names(
        workbooks,
        candidate_indexes,
        candidate_count,
        preliminary_template,
        &ordered_names,
        &ordered_name_count,
        error
    )) {
        free(candidate_indexes);
        xls_validation_report_free(report);
        return 0;
    }
    for (index = 0; index < ordered_name_count; ++index) {
        const char *sheet_name = ordered_names[index];
        int all_present = 1;
        size_t candidate;
        sheet_dimensions dominant;
        int all_dimensions_match = 1;
        for (candidate = 0; candidate < candidate_count; ++candidate) {
            size_t workbook_index = candidate_indexes[candidate];
            if (xls_workbook_find_sheet(
                &workbooks[workbook_index], sheet_name
            ) == NULL) {
                char message[512];
                all_present = 0;
                (void)snprintf(
                    message,
                    sizeof(message),
                    "缺少工作表“%s”，已跳过该工作表",
                    sheet_name
                );
                if (!add_issue(
                    report,
                    XLS_ISSUE_MISSING_SHEET,
                    workbook_index,
                    workbooks[workbook_index].filename,
                    sheet_name,
                    message,
                    error
                )) {
                    goto failure;
                }
            }
        }
        if (!all_present) {
            if (!string_list_append(
                &report->skipped_sheet_names,
                &report->skipped_sheet_count,
                sheet_name,
                error
            )) {
                goto failure;
            }
            continue;
        }
        dominant = dominant_dimensions(
            workbooks, candidate_indexes, candidate_count, sheet_name
        );
        for (candidate = 0; candidate < candidate_count; ++candidate) {
            size_t workbook_index = candidate_indexes[candidate];
            const xls_sheet *sheet = xls_workbook_find_sheet(
                &workbooks[workbook_index], sheet_name
            );
            sheet_dimensions dimensions = effective_dimensions(sheet);
            if (dimensions_equal(dimensions, dominant)) {
                continue;
            }
            all_dimensions_match = 0;
            if (dimensions.rows != dominant.rows) {
                char message[512];
                (void)snprintf(
                    message,
                    sizeof(message),
                    "工作表“%s”有效行数不一致（多数文件为 %zu 行，当前文件为 %zu 行）",
                    sheet_name,
                    dominant.rows,
                    dimensions.rows
                );
                if (!add_issue(
                    report,
                    XLS_ISSUE_ROW_COUNT_MISMATCH,
                    workbook_index,
                    workbooks[workbook_index].filename,
                    sheet_name,
                    message,
                    error
                )) {
                    goto failure;
                }
            }
            if (dimensions.columns != dominant.columns) {
                char message[512];
                (void)snprintf(
                    message,
                    sizeof(message),
                    "工作表“%s”有效列数不一致（多数文件为 %zu 列，当前文件为 %zu 列）",
                    sheet_name,
                    dominant.columns,
                    dimensions.columns
                );
                if (!add_issue(
                    report,
                    XLS_ISSUE_COLUMN_COUNT_MISMATCH,
                    workbook_index,
                    workbooks[workbook_index].filename,
                    sheet_name,
                    message,
                    error
                )) {
                    goto failure;
                }
            }
        }
        if (all_dimensions_match) {
            if (!string_list_append(
                &report->common_sheet_names,
                &report->common_sheet_count,
                sheet_name,
                error
            )) {
                goto failure;
            }
        } else if (!string_list_append(
            &report->skipped_sheet_names,
            &report->skipped_sheet_count,
            sheet_name,
            error
        )) {
            goto failure;
        }
    }
    if (report->common_sheet_count > 0) {
        size_t template_index = choose_template(
            workbooks,
            candidate_indexes,
            candidate_count,
            report->common_sheet_names,
            report->common_sheet_count
        );
        report->readiness = XLS_MERGE_READY;
        report->template_workbook_index = template_index;
        report->has_template_workbook = 1;
        report->mergeable_workbook_indexes = (size_t *)malloc(
            candidate_count * sizeof(*report->mergeable_workbook_indexes)
        );
        if (report->mergeable_workbook_indexes == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "记录可合并工作簿时内存不足");
            goto failure;
        }
        report->mergeable_workbook_indexes[0] = template_index;
        report->mergeable_workbook_count = 1;
        for (index = 0; index < candidate_count; ++index) {
            if (candidate_indexes[index] != template_index) {
                report->mergeable_workbook_indexes[
                    report->mergeable_workbook_count++
                ] = candidate_indexes[index];
            }
        }
    }
    for (index = 0; index < ordered_name_count; ++index) {
        free(ordered_names[index]);
    }
    free(ordered_names);
    free(candidate_indexes);
    xls_error_clear(error);
    return 1;

failure:
    for (index = 0; index < ordered_name_count; ++index) {
        free(ordered_names[index]);
    }
    free(ordered_names);
    free(candidate_indexes);
    xls_validation_report_free(report);
    return 0;
}

void xls_validation_report_free(xls_validation_report *report)
{
    size_t index;
    if (report == NULL) {
        return;
    }
    for (index = 0; index < report->common_sheet_count; ++index) {
        free(report->common_sheet_names[index]);
    }
    for (index = 0; index < report->skipped_sheet_count; ++index) {
        free(report->skipped_sheet_names[index]);
    }
    for (index = 0; index < report->issue_count; ++index) {
        free(report->issues[index].filename);
        free(report->issues[index].sheet_name);
        free(report->issues[index].message);
    }
    free(report->common_sheet_names);
    free(report->skipped_sheet_names);
    free(report->mergeable_workbook_indexes);
    free(report->issues);
    memset(report, 0, sizeof(*report));
}
