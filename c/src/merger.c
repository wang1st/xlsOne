#include "internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum cell_fingerprint {
    FINGERPRINT_STRONG_NUMERIC,
    FINGERPRINT_INTEGER_WIDE,
    FINGERPRINT_INTEGER_CODE,
    FINGERPRINT_LABEL,
    FINGERPRINT_DATE,
    FINGERPRINT_EMPTY,
    FINGERPRINT_MIXED
} cell_fingerprint;

typedef struct merge_input {
    const char *filename;
    const char *filepath;
    const xls_cell *cell;
} merge_input;

typedef struct neighbor_context {
    double numeric_tendency;
    double label_tendency;
    double column_metric_tendency;
} neighbor_context;

static const char *const amount_patterns[] = {
    "金额", "收入", "支出", "预算", "决算", "合计", "余额",
    "本期", "本年", "上期", "上年", "万元", "元", "amount",
    "total", "revenue", "expense", "balance"
};
static const char *const weak_amount_patterns[] = {
    "数", "数量", "人数", "个数", "count", "qty", "quantity"
};
static const char *const code_patterns[] = {
    "代码", "编码", "编号", "序号", "证号", "账号", "科目号",
    "code", "id", "number"
};
static const char *const label_patterns[] = {
    "名称", "姓名", "单位", "说明", "备注", "部门", "类别", "事项",
    "name", "label", "description", "note"
};

static int contains_any(
    const char *text,
    const char *const *patterns,
    size_t pattern_count
)
{
    size_t index;
    if (text == NULL) {
        return 0;
    }
    for (index = 0; index < pattern_count; ++index) {
        if (strstr(text, patterns[index]) != NULL
            || xls_string_contains_ascii_ci(text, patterns[index])) {
            return 1;
        }
    }
    return 0;
}

static int cell_has_number(const xls_cell *cell)
{
    return cell != NULL && (cell->flags & XLS_CELL_HAS_NUMERIC_VALUE) != 0u;
}

static int format_looks_numeric(const char *format_code)
{
    if (format_code == NULL || format_code[0] == '\0'
        || strchr(format_code, '@') != NULL) {
        return 0;
    }
    if (xls_string_contains_ascii_ci(format_code, "yy")
        || xls_string_contains_ascii_ci(format_code, "dd")
        || xls_string_contains_ascii_ci(format_code, "hh")
        || xls_string_contains_ascii_ci(format_code, "ss")
        || strstr(format_code, "年") != NULL
        || strstr(format_code, "月") != NULL
        || strstr(format_code, "日") != NULL) {
        return 0;
    }
    return strchr(format_code, '0') != NULL
        || strchr(format_code, '#') != NULL
        || strchr(format_code, '$') != NULL
        || strchr(format_code, '%') != NULL
        || strstr(format_code, "¥") != NULL;
}

static int number_is_integer(double value)
{
    return fabs(value - floor(value)) < 0.0000001;
}

static int is_code_length(size_t length)
{
    return length == 3 || length == 6 || length == 9 || length == 11
        || length == 12 || length == 15 || length == 18;
}

static cell_fingerprint fingerprint(const xls_cell *cell)
{
    size_t length;
    if (cell == NULL || xls_string_empty(cell->value)) {
        return FINGERPRINT_EMPTY;
    }
    if ((cell->flags & XLS_CELL_IS_DATE) != 0u) {
        return FINGERPRINT_DATE;
    }
    if (cell_has_number(cell)) {
        int integer_format;
        if (!number_is_integer(cell->numeric_value)) {
            return FINGERPRINT_STRONG_NUMERIC;
        }
        integer_format = cell->format_code != NULL
            && strchr(cell->format_code, '.') == NULL
            && strchr(cell->format_code, '$') == NULL
            && strchr(cell->format_code, '%') == NULL
            && strstr(cell->format_code, "¥") == NULL;
        if (!integer_format
            && (strchr(cell->value, '.') != NULL
                || strchr(cell->value, ',') != NULL)) {
            return FINGERPRINT_STRONG_NUMERIC;
        }
        length = strlen(cell->value);
        if (xls_string_all_ascii_digits(cell->value) && is_code_length(length)) {
            return FINGERPRINT_INTEGER_CODE;
        }
        if (cell->format_code != NULL
            && strcmp(cell->format_code, "@") == 0
            && xls_string_all_ascii_digits(cell->value)
            && length >= 2) {
            return FINGERPRINT_INTEGER_CODE;
        }
        return FINGERPRINT_INTEGER_WIDE;
    }
    if (xls_string_has_non_ascii(cell->value)) {
        return FINGERPRINT_LABEL;
    }
    {
        const unsigned char *cursor = (const unsigned char *)cell->value;
        int alpha = 1;
        while (*cursor != '\0') {
            if (!isalpha(*cursor) && !isspace(*cursor)) {
                alpha = 0;
                break;
            }
            ++cursor;
        }
        return alpha ? FINGERPRINT_LABEL : FINGERPRINT_MIXED;
    }
}

static int input_semantic(
    const merge_input *inputs,
    size_t input_count,
    const char *const *patterns,
    size_t pattern_count
)
{
    size_t index;
    for (index = 0; index < input_count; ++index) {
        if (inputs[index].cell != NULL
            && contains_any(
                inputs[index].cell->value,
                patterns,
                pattern_count
            )) {
            return 1;
        }
    }
    return 0;
}

static int input_weak_amount_semantic(
    const merge_input *inputs,
    size_t input_count
)
{
    size_t index;
    for (index = 0; index < input_count; ++index) {
        const char *value = inputs[index].cell == NULL
            ? NULL
            : inputs[index].cell->value;
        if (value == NULL
            || contains_any(
                value,
                code_patterns,
                sizeof(code_patterns) / sizeof(code_patterns[0])
            )
            || contains_any(
                value,
                label_patterns,
                sizeof(label_patterns) / sizeof(label_patterns[0])
            )) {
            continue;
        }
        if (contains_any(
            value,
            weak_amount_patterns,
            sizeof(weak_amount_patterns) / sizeof(weak_amount_patterns[0])
        )) {
            return 1;
        }
    }
    return 0;
}

static int source_copy(
    xls_source_entry *target,
    const merge_input *input,
    xls_error *error
)
{
    memset(target, 0, sizeof(*target));
    target->filename = xls_strdup(input->filename);
    target->filepath = xls_strdup(input->filepath);
    target->value = xls_strdup(input->cell == NULL ? "" : input->cell->value);
    if (target->filename == NULL || target->filepath == NULL || target->value == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying merge source");
        return 0;
    }
    if (input->cell == NULL) {
        target->state = XLS_SOURCE_MISSING;
        return 1;
    }
    target->state = xls_string_empty(input->cell->value)
        ? XLS_SOURCE_EMPTY
        : XLS_SOURCE_VALUE;
    if ((input->cell->flags & XLS_CELL_HAS_RAW_VALUE) != 0u) {
        target->raw_value = xls_strdup(input->cell->raw_value);
        if (target->raw_value == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying raw source");
            return 0;
        }
    }
    if (cell_has_number(input->cell)) {
        target->numeric_value = input->cell->numeric_value;
        target->has_numeric_value = 1;
    }
    return 1;
}

static void source_free(xls_source_entry *source)
{
    if (source == NULL) {
        return;
    }
    free(source->filename);
    free(source->filepath);
    free(source->value);
    free(source->raw_value);
    memset(source, 0, sizeof(*source));
}

static size_t common_prefix_characters(
    const merge_input *inputs,
    size_t input_count
)
{
    const char *first = NULL;
    size_t prefix_characters;
    size_t index;
    for (index = 0; index < input_count; ++index) {
        if (inputs[index].cell != NULL
            && !xls_string_empty(inputs[index].cell->value)) {
            first = inputs[index].cell->value;
            break;
        }
    }
    if (first == NULL) {
        return 0;
    }
    prefix_characters = xls_utf8_length(first);
    for (index = 0; index < input_count; ++index) {
        const char *value;
        size_t candidate = 0;
        size_t maximum;
        if (inputs[index].cell == NULL
            || xls_string_empty(inputs[index].cell->value)) {
            continue;
        }
        value = inputs[index].cell->value;
        maximum = xls_utf8_length(value);
        if (maximum > prefix_characters) {
            maximum = prefix_characters;
        }
        while (candidate < maximum) {
            size_t first_bytes = xls_utf8_prefix_bytes(first, candidate + 1);
            size_t value_bytes = xls_utf8_prefix_bytes(value, candidate + 1);
            if (first_bytes != value_bytes
                || memcmp(first, value, first_bytes) != 0) {
                break;
            }
            ++candidate;
        }
        prefix_characters = candidate;
    }
    return prefix_characters;
}

static char *label_display(
    const merge_input *inputs,
    size_t input_count,
    size_t distinct_count
)
{
    const char *first = "";
    size_t first_length = 0;
    size_t total_length = 0;
    size_t valid_count = 0;
    size_t index;
    size_t prefix_characters;
    size_t prefix_bytes;
    size_t standard_length;
    char *result;
    if (distinct_count <= 1) {
        for (index = 0; index < input_count; ++index) {
            if (inputs[index].cell != NULL
                && !xls_string_empty(inputs[index].cell->value)) {
                return xls_strdup(inputs[index].cell->value);
            }
        }
        return xls_strdup("");
    }
    for (index = 0; index < input_count; ++index) {
        if (inputs[index].cell != NULL
            && !xls_string_empty(inputs[index].cell->value)) {
            size_t length = xls_utf8_length(inputs[index].cell->value);
            if (valid_count == 0) {
                first = inputs[index].cell->value;
                first_length = length;
            }
            total_length += length;
            ++valid_count;
        }
    }
    if (valid_count == 0) {
        return xls_strdup("");
    }
    prefix_characters = common_prefix_characters(inputs, input_count);
    prefix_bytes = xls_utf8_prefix_bytes(first, prefix_characters);
    standard_length = (size_t)floor(
        (double)total_length / (double)valid_count + 0.5
    );
    if (standard_length < prefix_characters) {
        standard_length = prefix_characters;
    }
    if (standard_length < first_length && valid_count == 1) {
        standard_length = first_length;
    }
    result = (char *)malloc(prefix_bytes + standard_length - prefix_characters + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, first, prefix_bytes);
    memset(
        result + prefix_bytes,
        '_',
        standard_length - prefix_characters
    );
    result[prefix_bytes + standard_length - prefix_characters] = '\0';
    return result;
}

static size_t distinct_value_count(
    const merge_input *inputs,
    size_t input_count
)
{
    size_t index;
    size_t previous;
    size_t count = 0;
    for (index = 0; index < input_count; ++index) {
        const char *value;
        int found = 0;
        if (inputs[index].cell == NULL
            || xls_string_empty(inputs[index].cell->value)) {
            continue;
        }
        value = inputs[index].cell->value;
        for (previous = 0; previous < index; ++previous) {
            if (inputs[previous].cell != NULL
                && strcmp(inputs[previous].cell->value, value) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            ++count;
        }
    }
    return count;
}

static const char *first_format_code(
    const merge_input *inputs,
    size_t input_count
)
{
    size_t index;
    for (index = 0; index < input_count; ++index) {
        if (inputs[index].cell != NULL
            && (inputs[index].cell->flags & XLS_CELL_HAS_FORMAT_CODE) != 0u) {
            return inputs[index].cell->format_code;
        }
    }
    return NULL;
}

static int code_like_sequence(
    const merge_input *inputs,
    size_t input_count
)
{
    const char *first = NULL;
    size_t first_length = 0;
    size_t valid_count = 0;
    size_t index;
    size_t prefix;
    for (index = 0; index < input_count; ++index) {
        const xls_cell *cell = inputs[index].cell;
        if (cell == NULL || xls_string_empty(cell->value)) {
            continue;
        }
        if (!cell_has_number(cell)
            || !number_is_integer(cell->numeric_value)
            || !xls_string_all_ascii_digits(cell->value)) {
            return 0;
        }
        if (first == NULL) {
            first = cell->value;
            first_length = strlen(first);
        } else if (strlen(cell->value) != first_length) {
            return 0;
        }
        ++valid_count;
    }
    if (valid_count <= 1 || !is_code_length(first_length)) {
        return 0;
    }
    prefix = common_prefix_characters(inputs, input_count);
    return prefix * 2 >= first_length;
}

static int merged_cell_begin(
    xls_merged_cell *result,
    const merge_input *inputs,
    size_t input_count,
    xls_error *error
)
{
    size_t index;
    result->sources = input_count == 0
        ? NULL
        : (xls_source_entry *)calloc(input_count, sizeof(*result->sources));
    if (input_count > 0 && result->sources == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while allocating sources");
        return 0;
    }
    result->source_count = input_count;
    for (index = 0; index < input_count; ++index) {
        if (!source_copy(&result->sources[index], &inputs[index], error)) {
            return 0;
        }
    }
    return 1;
}

static int merged_cell_finish(
    xls_merged_cell *result,
    xls_cell_kind kind,
    char *display,
    double sum,
    size_t mixed_count,
    const char *single_value,
    const char *format_code,
    const char *reason,
    int suspicious,
    xls_error *error
)
{
    result->kind = kind;
    result->display_value = display;
    result->sum = sum;
    result->mixed_count = mixed_count;
    result->single_value = xls_strdup(single_value);
    result->format_code = xls_strdup(format_code);
    result->decision.auto_detected_type = kind;
    result->decision.confidence = 0.8;
    result->decision.is_suspicious = suspicious ? 1u : 0u;
    result->decision.reason = xls_strdup(reason);
    if (result->display_value == NULL
        || result->single_value == NULL
        || result->format_code == NULL
        || result->decision.reason == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while finalizing merged cell");
        return 0;
    }
    return 1;
}

static int merge_one_cell(
    const merge_input *inputs,
    const merge_input *left_inputs,
    size_t input_count,
    neighbor_context context,
    size_t row,
    size_t column,
    xls_merged_cell *result,
    xls_error *error
)
{
    size_t index;
    size_t valid_count = 0;
    size_t blank_count = 0;
    size_t distinct_count;
    int all_numeric = 1;
    int all_dates = 1;
    int all_zero = 1;
    int all_integers = 1;
    int identical_nonzero = 1;
    int strong_numeric = 0;
    double total = 0.0;
    double first_numeric = 0.0;
    int has_first_numeric = 0;
    const char *format_code;
    int code_semantic;
    int amount_semantic;
    int weak_amount_semantic;
    int label_semantic;
    int context_numeric;
    int column_metric;
    int metric_semantic;

    memset(result, 0, sizeof(*result));
    if (!merged_cell_begin(result, inputs, input_count, error)) {
        return 0;
    }
    for (index = 0; index < input_count; ++index) {
        const xls_cell *cell = inputs[index].cell;
        if (cell == NULL || xls_string_empty(cell->value)) {
            ++blank_count;
            continue;
        }
        ++valid_count;
        if (!cell_has_number(cell)) {
            all_numeric = 0;
            all_zero = 0;
            all_integers = 0;
            identical_nonzero = 0;
        } else {
            total += cell->numeric_value;
            all_zero = all_zero && fabs(cell->numeric_value) < 0.0000001;
            all_integers = all_integers && number_is_integer(cell->numeric_value);
            if (!has_first_numeric) {
                first_numeric = cell->numeric_value;
                has_first_numeric = 1;
            } else if (fabs(cell->numeric_value - first_numeric) >= 0.0000001) {
                identical_nonzero = 0;
            }
            if (fingerprint(cell) == FINGERPRINT_STRONG_NUMERIC) {
                strong_numeric = 1;
            }
        }
        all_dates = all_dates && (cell->flags & XLS_CELL_IS_DATE) != 0u;
    }
    format_code = first_format_code(inputs, input_count);
    distinct_count = distinct_value_count(inputs, input_count);

    if (valid_count == 0) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL, xls_strdup(""), 0.0, 0, "", format_code,
            "所有来源为空或缺失", 0, error
        );
    }
    if (row == 0) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL,
            label_display(inputs, input_count, distinct_count),
            0.0, 0, "", format_code,
            "首行按表头处理，强制视为标签列",
            distinct_count > 1, error
        );
    }

    code_semantic = input_semantic(
        left_inputs, input_count,
        code_patterns, sizeof(code_patterns) / sizeof(code_patterns[0])
    );
    amount_semantic = input_semantic(
        left_inputs, input_count,
        amount_patterns, sizeof(amount_patterns) / sizeof(amount_patterns[0])
    );
    weak_amount_semantic = input_weak_amount_semantic(left_inputs, input_count);
    label_semantic = input_semantic(
        left_inputs, input_count,
        label_patterns, sizeof(label_patterns) / sizeof(label_patterns[0])
    );
    context_numeric = context.numeric_tendency >= 0.55
        && context.numeric_tendency > context.label_tendency;
    column_metric = context.column_metric_tendency >= 0.55;
    metric_semantic = amount_semantic
        || (!code_semantic && !label_semantic
            && ((weak_amount_semantic ? 1 : 0)
                + (context_numeric ? 1 : 0)
                + (column_metric ? 1 : 0) >= 2));

    if (valid_count == 1) {
        const xls_cell *only = NULL;
        int is_code_like;
        int zero_with_blank_bias;
        for (index = 0; index < input_count; ++index) {
            if (inputs[index].cell != NULL
                && !xls_string_empty(inputs[index].cell->value)) {
                only = inputs[index].cell;
                break;
            }
        }
        is_code_like = fingerprint(only) == FINGERPRINT_INTEGER_CODE
            || (only->format_code != NULL && strcmp(only->format_code, "@") == 0);
        zero_with_blank_bias = cell_has_number(only)
            && fabs(only->numeric_value) < 0.0000001
            && blank_count > 0
            && !code_semantic
            && !label_semantic
            && context.label_tendency < 0.65;
        if (cell_has_number(only)
            && !is_code_like
            && !code_semantic
            && (!label_semantic || metric_semantic)
            && (format_looks_numeric(only->format_code)
                || format_looks_numeric(format_code)
                || amount_semantic
                || metric_semantic
                || context_numeric
                || zero_with_blank_bias)) {
            return merged_cell_finish(
                result, XLS_CELL_SUM,
                xls_format_number(only->numeric_value, only->format_code),
                only->numeric_value, 0, "", only->format_code,
                "仅有一个非空数值，格式或上下文支持按求和处理", 0, error
            );
        }
        return merged_cell_finish(
            result, XLS_CELL_SINGLE, xls_strdup(only->value),
            0.0, 0, only->value, only->format_code,
            input_count == 1 ? "只有一个有效来源" : "仅有一个非空来源值，按单值显示",
            0, error
        );
    }

    identical_nonzero = identical_nonzero
        && all_numeric
        && all_integers
        && fabs(first_numeric) >= 0.0000001;
    if (all_zero && !code_semantic && !label_semantic) {
        return merged_cell_finish(
            result, XLS_CELL_SUM, xls_format_number(0.0, format_code),
            0.0, 0, "", format_code,
            "所有非空来源均为 0，按可累加单元格求和处理", 0, error
        );
    }
    if (all_dates) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL,
            label_display(inputs, input_count, distinct_count),
            0.0, 0, "", format_code,
            "日期单元格按标签处理", distinct_count > 1, error
        );
    }
    if (identical_nonzero && blank_count == 0 && !metric_semantic) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL,
            label_display(inputs, input_count, distinct_count),
            0.0, 0, "", format_code,
            "所有来源为相同非零整数，且无明确可累加语义，按标签处理",
            0, error
        );
    }
    if (code_semantic && code_like_sequence(inputs, input_count)
        && !amount_semantic) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL,
            label_display(inputs, input_count, distinct_count),
            0.0, 0, "", format_code,
            "左邻列命中编码语义，按标签处理", distinct_count > 1, error
        );
    }
    if (all_numeric
        && !code_semantic
        && (!label_semantic || metric_semantic)
        && (!code_like_sequence(inputs, input_count)
            || blank_count > 0 || metric_semantic)
        && (distinct_count > 1
            || metric_semantic
            || blank_count > 0
            || format_code != NULL
            || strong_numeric
            || context_numeric)) {
        return merged_cell_finish(
            result, XLS_CELL_SUM, xls_format_number(total, format_code),
            total, 0, "", format_code,
            blank_count > 0
                ? "部分来源为空或缺失，非空来源均为数值，空值按 0 参与求和"
                : distinct_count == 1 && metric_semantic
                    ? "相同整数命中计量语义并得到同列上下文支持，按求和处理"
                    : "所有有效来源均为数值，按求和处理",
            0, error
        );
    }
    if (column == 0 && all_numeric
        && !code_like_sequence(inputs, input_count)
        && distinct_count > 1
        && (strong_numeric || context_numeric)) {
        return merged_cell_finish(
            result, XLS_CELL_SUM, xls_format_number(total, format_code),
            total, 0, "", format_code,
            "首列以数值格式为主且上下文支持，按求和处理", 0, error
        );
    }
    if (distinct_count == 1 || column == 0
        || (!all_numeric
            && ((format_code != NULL
                    && (strcmp(format_code, "@") == 0
                        || strcmp(format_code, ";;;") == 0))
                || label_semantic
                || context.label_tendency >= 0.5))) {
        return merged_cell_finish(
            result, XLS_CELL_LABEL,
            label_display(inputs, input_count, distinct_count),
            0.0, 0, "", format_code,
            distinct_count == 1
                ? "所有有效来源值一致，按标签处理"
                : column == 0
                    ? "首列采用保守策略，按标签处理"
                    : "文本格式或上下文偏向标签，按标签处理",
            distinct_count > 1, error
        );
    }
    {
        char buffer[64];
        (void)snprintf(buffer, sizeof(buffer), "%zu条", distinct_count);
        return merged_cell_finish(
            result, XLS_CELL_MIXED, xls_strdup(buffer),
            0.0, distinct_count, "", NULL,
            "存在多个不同的非聚合值", 0, error
        );
    }
}

static int is_metric_anchor(const xls_cell *cell)
{
    if (cell == NULL || xls_string_empty(cell->value)) {
        return 0;
    }
    if (contains_any(
        cell->value,
        code_patterns,
        sizeof(code_patterns) / sizeof(code_patterns[0])
    )) {
        return 0;
    }
    return contains_any(
        cell->value,
        amount_patterns,
        sizeof(amount_patterns) / sizeof(amount_patterns[0])
    ) || contains_any(
        cell->value,
        weak_amount_patterns,
        sizeof(weak_amount_patterns) / sizeof(weak_amount_patterns[0])
    );
}

static const xls_cell *nearest_label(
    const xls_sheet *sheet,
    size_t row,
    size_t column
)
{
    size_t distance;
    for (distance = 1; distance <= row; ++distance) {
        const xls_cell *cell = xls_sheet_cell_const(sheet, row - distance, column);
        if (cell != NULL && !xls_string_empty(cell->value) && !cell_has_number(cell)) {
            return cell;
        }
    }
    for (distance = column; distance > 0; --distance) {
        const xls_cell *cell = xls_sheet_cell_const(sheet, row, distance - 1);
        if (cell != NULL && !xls_string_empty(cell->value) && !cell_has_number(cell)) {
            return cell;
        }
    }
    if (column > 0) {
        for (distance = 1; distance <= row; ++distance) {
            const xls_cell *cell
                = xls_sheet_cell_const(sheet, row - distance, column - 1);
            if (cell != NULL
                && !xls_string_empty(cell->value)
                && !cell_has_number(cell)) {
                return cell;
            }
        }
    }
    return NULL;
}

static double column_metric_tendency(
    const xls_sheet *const *sheets,
    size_t sheet_count,
    size_t column
)
{
    size_t sheet_index;
    size_t labeled = 0;
    size_t metric = 0;
    for (sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
        size_t row;
        for (row = 0; row < sheets[sheet_index]->row_count; ++row) {
            const xls_cell *cell
                = xls_sheet_cell_const(sheets[sheet_index], row, column);
            if (cell == NULL || xls_string_empty(cell->value) || !cell_has_number(cell)) {
                continue;
            }
            {
                const xls_cell *label
                    = nearest_label(sheets[sheet_index], row, column);
                if (label != NULL) {
                    ++labeled;
                    if (is_metric_anchor(label)) {
                        ++metric;
                    }
                }
            }
            break;
        }
    }
    return labeled == 0 ? 0.0 : (double)metric / (double)labeled;
}

static neighbor_context build_neighbor_context(
    const xls_sheet *const *sheets,
    size_t sheet_count,
    size_t row,
    size_t column,
    double metric_tendency
)
{
    double numeric_score = 0.0;
    double label_score = 0.0;
    double total_weight = 0.0;
    size_t offset;
    for (offset = 1; offset <= 3; ++offset) {
        size_t positions[2];
        size_t position_count = 0;
        size_t position_index;
        if (row >= offset) {
            positions[position_count++] = row - offset;
        }
        positions[position_count++] = row + offset;
        for (position_index = 0; position_index < position_count; ++position_index) {
            size_t sheet_index;
            size_t numeric = 0;
            size_t label = 0;
            double weight = 1.0 / (double)offset;
            int exists = 0;
            for (sheet_index = 0; sheet_index < sheet_count; ++sheet_index) {
                const xls_cell *cell = xls_sheet_cell_const(
                    sheets[sheet_index], positions[position_index], column
                );
                cell_fingerprint value = fingerprint(cell);
                if (value == FINGERPRINT_EMPTY) {
                    continue;
                }
                exists = 1;
                if (value == FINGERPRINT_STRONG_NUMERIC
                    || value == FINGERPRINT_INTEGER_WIDE) {
                    ++numeric;
                } else if (value == FINGERPRINT_LABEL
                    || value == FINGERPRINT_DATE) {
                    ++label;
                }
            }
            if (exists) {
                if (numeric > label) {
                    numeric_score += weight;
                } else if (label > numeric) {
                    label_score += weight;
                }
                total_weight += weight;
            }
        }
    }
    {
        neighbor_context result;
        result.numeric_tendency = total_weight == 0.0
            ? 0.0
            : numeric_score / total_weight;
        result.label_tendency = total_weight == 0.0
            ? 0.0
            : label_score / total_weight;
        result.column_metric_tendency = metric_tendency;
        return result;
    }
}

static void merged_cell_free(xls_merged_cell *cell)
{
    size_t index;
    if (cell == NULL) {
        return;
    }
    for (index = 0; index < cell->source_count; ++index) {
        source_free(&cell->sources[index]);
    }
    free(cell->sources);
    free(cell->display_value);
    free(cell->single_value);
    free(cell->format_code);
    free(cell->decision.reason);
    memset(cell, 0, sizeof(*cell));
}

void xls_merged_sheet_free(xls_merged_sheet *sheet)
{
    size_t index;
    if (sheet == NULL) {
        return;
    }
    for (index = 0; index < sheet->row_count * sheet->column_count; ++index) {
        merged_cell_free(&sheet->cells[index]);
    }
    for (index = 0; index < sheet->source_file_count; ++index) {
        free(sheet->source_files[index]);
    }
    free(sheet->source_files);
    free(sheet->cells);
    free(sheet->sheet_name);
    memset(sheet, 0, sizeof(*sheet));
}

int xls_merge_sheet(
    const xls_workbook *workbooks,
    size_t workbook_count,
    const char *sheet_name,
    xls_merged_sheet *result,
    xls_error *error
)
{
    const xls_sheet **sheets;
    const xls_workbook **included_workbooks;
    double *metric_tendencies;
    size_t included_count = 0;
    size_t max_rows = 0;
    size_t max_columns = 0;
    size_t workbook_index;
    size_t row;
    size_t column;
    size_t cell_count;
    if (result == NULL || sheet_name == NULL
        || (workbooks == NULL && workbook_count > 0)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid merge arguments");
        return 0;
    }
    memset(result, 0, sizeof(*result));
    sheets = workbook_count == 0
        ? NULL
        : (const xls_sheet **)calloc(workbook_count, sizeof(*sheets));
    included_workbooks = workbook_count == 0
        ? NULL
        : (const xls_workbook **)calloc(workbook_count, sizeof(*included_workbooks));
    if (workbook_count > 0 && (sheets == NULL || included_workbooks == NULL)) {
        free(sheets);
        free(included_workbooks);
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while selecting sheets");
        return 0;
    }
    for (workbook_index = 0; workbook_index < workbook_count; ++workbook_index) {
        const xls_sheet *sheet
            = xls_workbook_find_sheet(&workbooks[workbook_index], sheet_name);
        if (sheet != NULL) {
            sheets[included_count] = sheet;
            included_workbooks[included_count] = &workbooks[workbook_index];
            ++included_count;
            if (sheet->row_count > max_rows) {
                max_rows = sheet->row_count;
            }
            if (sheet->column_count > max_columns) {
                max_columns = sheet->column_count;
            }
        }
    }
    result->sheet_name = xls_strdup(sheet_name);
    if (result->sheet_name == NULL) {
        free(sheets);
        free(included_workbooks);
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying result name");
        return 0;
    }
    result->source_file_count = included_count;
    if (included_count > 0) {
        result->source_files
            = (char **)calloc(included_count, sizeof(*result->source_files));
        if (result->source_files == NULL) {
            free(sheets);
            free(included_workbooks);
            xls_merged_sheet_free(result);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying source names");
            return 0;
        }
        for (workbook_index = 0; workbook_index < included_count; ++workbook_index) {
            result->source_files[workbook_index]
                = xls_strdup(included_workbooks[workbook_index]->filename);
            if (result->source_files[workbook_index] == NULL) {
                free(sheets);
                free(included_workbooks);
                xls_merged_sheet_free(result);
                xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while copying source name");
                return 0;
            }
        }
    }
    if (!xls_multiply_size(max_rows, max_columns, &cell_count)) {
        free(sheets);
        free(included_workbooks);
        xls_merged_sheet_free(result);
        xls_set_error(error, XLS_ERROR_MEMORY, "merged sheet dimensions overflow");
        return 0;
    }
    result->row_count = max_rows;
    result->column_count = max_columns;
    if (cell_count > 0) {
        result->cells = (xls_merged_cell *)calloc(cell_count, sizeof(*result->cells));
        if (result->cells == NULL) {
            free(sheets);
            free(included_workbooks);
            xls_merged_sheet_free(result);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while allocating merged cells");
            return 0;
        }
    }
    metric_tendencies = max_columns == 0
        ? NULL
        : (double *)calloc(max_columns, sizeof(*metric_tendencies));
    if (max_columns > 0 && metric_tendencies == NULL) {
        free(sheets);
        free(included_workbooks);
        xls_merged_sheet_free(result);
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while analyzing columns");
        return 0;
    }
    for (column = 0; column < max_columns; ++column) {
        metric_tendencies[column]
            = column_metric_tendency(sheets, included_count, column);
    }
    for (row = 0; row < max_rows; ++row) {
        for (column = 0; column < max_columns; ++column) {
            merge_input *inputs;
            merge_input *left_inputs;
            inputs = (merge_input *)calloc(included_count, sizeof(*inputs));
            left_inputs = (merge_input *)calloc(included_count, sizeof(*left_inputs));
            if (inputs == NULL || left_inputs == NULL) {
                free(inputs);
                free(left_inputs);
                free(metric_tendencies);
                free(sheets);
                free(included_workbooks);
                xls_merged_sheet_free(result);
                xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while merging cell");
                return 0;
            }
            for (workbook_index = 0;
                 workbook_index < included_count;
                 ++workbook_index) {
                inputs[workbook_index].filename
                    = included_workbooks[workbook_index]->filename;
                inputs[workbook_index].filepath
                    = included_workbooks[workbook_index]->filepath;
                inputs[workbook_index].cell
                    = xls_sheet_cell_const(sheets[workbook_index], row, column);
                left_inputs[workbook_index].filename
                    = included_workbooks[workbook_index]->filename;
                left_inputs[workbook_index].filepath
                    = included_workbooks[workbook_index]->filepath;
                left_inputs[workbook_index].cell = column == 0
                    ? NULL
                    : xls_sheet_cell_const(
                        sheets[workbook_index], row, column - 1
                    );
            }
            if (!merge_one_cell(
                inputs, left_inputs, included_count,
                build_neighbor_context(
                    sheets, included_count, row, column,
                    metric_tendencies[column]
                ),
                row, column,
                &result->cells[row * max_columns + column],
                error
            )) {
                free(inputs);
                free(left_inputs);
                free(metric_tendencies);
                free(sheets);
                free(included_workbooks);
                xls_merged_sheet_free(result);
                return 0;
            }
            free(inputs);
            free(left_inputs);
        }
    }
    free(metric_tendencies);
    free(sheets);
    free(included_workbooks);
    xls_error_clear(error);
    return 1;
}

int xls_merge_first_sheets(
    const xls_workbook *workbooks,
    size_t workbook_count,
    xls_merged_sheet *result,
    xls_error *error
)
{
    const char *sheet_name = "Sheet1";
    if (workbook_count > 0
        && workbooks != NULL
        && workbooks[0].sheet_count > 0
        && workbooks[0].sheets[0].name != NULL) {
        sheet_name = workbooks[0].sheets[0].name;
    }
    return xls_merge_sheet(workbooks, workbook_count, sheet_name, result, error);
}

const xls_merged_cell *xls_merged_sheet_cell(
    const xls_merged_sheet *sheet,
    size_t row,
    size_t column
)
{
    if (sheet == NULL || row >= sheet->row_count || column >= sheet->column_count) {
        return NULL;
    }
    return &sheet->cells[row * sheet->column_count + column];
}

xls_merged_cell *xls_merged_sheet_cell_mutable(
    xls_merged_sheet *sheet,
    size_t row,
    size_t column
)
{
    if (sheet == NULL || row >= sheet->row_count || column >= sheet->column_count) {
        return NULL;
    }
    return &sheet->cells[row * sheet->column_count + column];
}

static size_t source_distinct_count(
    const xls_source_entry *sources,
    size_t source_count
)
{
    size_t index;
    size_t previous;
    size_t count = 0;
    for (index = 0; index < source_count; ++index) {
        int found = 0;
        if (sources[index].state != XLS_SOURCE_VALUE
            || xls_string_empty(sources[index].value)) {
            continue;
        }
        for (previous = 0; previous < index; ++previous) {
            if (sources[previous].state == XLS_SOURCE_VALUE
                && strcmp(sources[previous].value, sources[index].value) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            ++count;
        }
    }
    return count;
}

static char *source_label_display(
    const xls_source_entry *sources,
    size_t source_count,
    size_t distinct_count
)
{
    merge_input *inputs;
    xls_cell *cells;
    char *display;
    size_t index;
    if (source_count == 0) {
        return xls_strdup("");
    }
    inputs = (merge_input *)calloc(source_count, sizeof(*inputs));
    cells = (xls_cell *)calloc(source_count, sizeof(*cells));
    if (inputs == NULL || cells == NULL) {
        free(inputs);
        free(cells);
        return NULL;
    }
    for (index = 0; index < source_count; ++index) {
        inputs[index].filename = sources[index].filename;
        inputs[index].filepath = sources[index].filepath;
        if (sources[index].state == XLS_SOURCE_MISSING) {
            continue;
        }
        cells[index].value = sources[index].value;
        inputs[index].cell = &cells[index];
    }
    display = label_display(inputs, source_count, distinct_count);
    free(inputs);
    free(cells);
    return display;
}

int xls_merged_cell_set_kind(
    xls_merged_cell *cell,
    xls_cell_kind kind,
    xls_error *error
)
{
    char *display = NULL;
    char *single = NULL;
    double sum = 0.0;
    size_t distinct_count;
    size_t mixed_count = 0;
    size_t index;
    if (cell == NULL
        || kind < XLS_CELL_LABEL
        || kind > XLS_CELL_SINGLE) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid merged cell override");
        return 0;
    }
    distinct_count = source_distinct_count(cell->sources, cell->source_count);
    if (kind == XLS_CELL_SUM) {
        for (index = 0; index < cell->source_count; ++index) {
            if (cell->sources[index].has_numeric_value != 0u) {
                sum += cell->sources[index].numeric_value;
            }
        }
        display = xls_format_number(sum, cell->format_code);
    } else if (kind == XLS_CELL_LABEL) {
        display = source_label_display(
            cell->sources,
            cell->source_count,
            distinct_count
        );
    } else if (kind == XLS_CELL_MIXED) {
        char buffer[64];
        mixed_count = distinct_count;
        (void)snprintf(buffer, sizeof(buffer), "%zu条", mixed_count);
        display = xls_strdup(buffer);
    } else {
        const char *value = "";
        for (index = 0; index < cell->source_count; ++index) {
            if (cell->sources[index].state == XLS_SOURCE_VALUE) {
                value = cell->sources[index].value;
                break;
            }
        }
        display = xls_strdup(value);
        single = xls_strdup(value);
    }
    if (display == NULL) {
        free(single);
        xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while applying cell override");
        return 0;
    }
    if (single == NULL) {
        single = xls_strdup("");
        if (single == NULL) {
            free(display);
            xls_set_error(error, XLS_ERROR_MEMORY, "out of memory while applying cell override");
            return 0;
        }
    }
    free(cell->display_value);
    free(cell->single_value);
    cell->display_value = display;
    cell->single_value = single;
    cell->kind = kind;
    cell->sum = sum;
    cell->mixed_count = mixed_count;
    cell->is_overridden = kind == cell->decision.auto_detected_type ? 0u : 1u;
    xls_error_clear(error);
    return 1;
}

int xls_merged_cell_restore_automatic(
    xls_merged_cell *cell,
    xls_error *error
)
{
    if (cell == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "merged cell must not be null");
        return 0;
    }
    return xls_merged_cell_set_kind(
        cell,
        cell->decision.auto_detected_type,
        error
    );
}
