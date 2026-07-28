#include "internal.h"
#include "biff8_parser.h"
#include "zip_archive.h"

#include <ctype.h>
#include <expat.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct workbook_sheet_ref {
    char *name;
    char *relationship_id;
} workbook_sheet_ref;

typedef struct relationship {
    char *id;
    char *target;
} relationship;

typedef struct number_format {
    int id;
    char *code;
} number_format;

typedef struct style_table {
    number_format *formats;
    size_t format_count;
    int *cell_format_ids;
    size_t cell_format_count;
} style_table;

typedef struct string_list {
    char **items;
    size_t count;
} string_list;

typedef struct text_buffer {
    char *data;
    size_t length;
    size_t capacity;
} text_buffer;

typedef struct merge_range {
    size_t first_row;
    size_t first_column;
    size_t last_row;
    size_t last_column;
} merge_range;

static const char *local_name(const char *name)
{
    const char *separator;
    if (name == NULL) {
        return "";
    }
    separator = strrchr(name, '|');
    return separator == NULL ? name : separator + 1;
}

static const char *attribute_value(const char **attributes, const char *name)
{
    size_t index;
    if (attributes == NULL) {
        return NULL;
    }
    for (index = 0; attributes[index] != NULL; index += 2) {
        if (strcmp(local_name(attributes[index]), name) == 0) {
            return attributes[index + 1];
        }
    }
    return NULL;
}

static int text_append(
    text_buffer *buffer,
    const char *text,
    size_t length,
    xls_error *error
)
{
    size_t required;
    size_t capacity;
    char *replacement;
    if (length == 0) {
        return 1;
    }
    if (buffer->length > SIZE_MAX - length - 1) {
        xls_set_error(error, XLS_ERROR_MEMORY, "XML 文本过大");
        return 0;
    }
    required = buffer->length + length + 1;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        replacement = (char *)realloc(buffer->data, capacity);
        if (replacement == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "追加 XML 文本时内存不足");
            return 0;
        }
        buffer->data = replacement;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static void text_clear(text_buffer *buffer)
{
    buffer->length = 0;
    if (buffer->data != NULL) {
        buffer->data[0] = '\0';
    }
}

static void text_free(text_buffer *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static int parse_xml(
    const unsigned char *data,
    size_t size,
    XML_StartElementHandler start,
    XML_EndElementHandler end,
    XML_CharacterDataHandler characters,
    void *context,
    xls_error *error,
    const char *description
)
{
    XML_Parser parser;
    enum XML_Status status;
    if (size > (size_t)INT_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "%s 过大", description);
        return 0;
    }
    parser = XML_ParserCreateNS(NULL, '|');
    if (parser == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "创建 XML 解析器失败");
        return 0;
    }
    XML_SetUserData(parser, context);
    XML_SetElementHandler(parser, start, end);
    XML_SetCharacterDataHandler(parser, characters);
    status = XML_Parse(parser, (const char *)data, (int)size, XML_TRUE);
    if (status != XML_STATUS_OK) {
        xls_set_error(
            error,
            XLS_ERROR_FORMAT,
            "%s 解析失败（第 %lu 行）：%s",
            description,
            XML_GetCurrentLineNumber(parser),
            XML_ErrorString(XML_GetErrorCode(parser))
        );
        XML_ParserFree(parser);
        return 0;
    }
    XML_ParserFree(parser);
    return 1;
}

typedef struct workbook_context {
    workbook_sheet_ref *items;
    size_t count;
    xls_error *error;
    int failed;
} workbook_context;

static void XMLCALL workbook_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    workbook_context *context = (workbook_context *)user_data;
    workbook_sheet_ref *replacement;
    workbook_sheet_ref *item;
    const char *name;
    const char *relationship_id;
    if (context->failed || strcmp(local_name(element), "sheet") != 0) {
        return;
    }
    name = attribute_value(attributes, "name");
    relationship_id = attribute_value(attributes, "id");
    if (name == NULL || relationship_id == NULL) {
        return;
    }
    replacement = (workbook_sheet_ref *)realloc(
        context->items, (context->count + 1) * sizeof(*context->items)
    );
    if (replacement == NULL) {
        xls_set_error(context->error, XLS_ERROR_MEMORY, "解析工作表清单时内存不足");
        context->failed = 1;
        return;
    }
    context->items = replacement;
    item = &context->items[context->count++];
    memset(item, 0, sizeof(*item));
    item->name = xls_strdup(name);
    item->relationship_id = xls_strdup(relationship_id);
    if (item->name == NULL || item->relationship_id == NULL) {
        xls_set_error(context->error, XLS_ERROR_MEMORY, "复制工作表清单时内存不足");
        context->failed = 1;
    }
}

typedef struct relationship_context {
    relationship *items;
    size_t count;
    xls_error *error;
    int failed;
} relationship_context;

static char *normalize_relationship_target(const char *target)
{
    const char *body = target;
    char *result;
    size_t length;
    if (body == NULL) {
        return NULL;
    }
    while (*body == '/') {
        ++body;
    }
    if (strncmp(body, "xl/", 3) == 0) {
        return xls_strdup(body);
    }
    length = strlen(body);
    result = (char *)malloc(length + 4);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, "xl/", 3);
    memcpy(result + 3, body, length + 1);
    return result;
}

static void XMLCALL relationship_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    relationship_context *context = (relationship_context *)user_data;
    relationship *replacement;
    relationship *item;
    const char *id;
    const char *target;
    if (context->failed || strcmp(local_name(element), "Relationship") != 0) {
        return;
    }
    id = attribute_value(attributes, "Id");
    target = attribute_value(attributes, "Target");
    if (id == NULL || target == NULL) {
        return;
    }
    replacement = (relationship *)realloc(
        context->items, (context->count + 1) * sizeof(*context->items)
    );
    if (replacement == NULL) {
        xls_set_error(context->error, XLS_ERROR_MEMORY, "解析工作表关系时内存不足");
        context->failed = 1;
        return;
    }
    context->items = replacement;
    item = &context->items[context->count++];
    memset(item, 0, sizeof(*item));
    item->id = xls_strdup(id);
    item->target = normalize_relationship_target(target);
    if (item->id == NULL || item->target == NULL) {
        xls_set_error(context->error, XLS_ERROR_MEMORY, "复制工作表关系时内存不足");
        context->failed = 1;
    }
}

typedef struct shared_string_context {
    string_list strings;
    text_buffer current;
    int in_item;
    int in_text;
    xls_error *error;
    int failed;
} shared_string_context;

static void XMLCALL shared_string_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    shared_string_context *context = (shared_string_context *)user_data;
    const char *name = local_name(element);
    (void)attributes;
    if (strcmp(name, "si") == 0) {
        context->in_item = 1;
        text_clear(&context->current);
    } else if (context->in_item && strcmp(name, "t") == 0) {
        context->in_text = 1;
    }
}

static void XMLCALL shared_string_end(void *user_data, const char *element)
{
    shared_string_context *context = (shared_string_context *)user_data;
    const char *name = local_name(element);
    if (strcmp(name, "t") == 0) {
        context->in_text = 0;
    } else if (strcmp(name, "si") == 0 && context->in_item) {
        char **replacement = (char **)realloc(
            context->strings.items,
            (context->strings.count + 1) * sizeof(*context->strings.items)
        );
        if (replacement == NULL) {
            xls_set_error(context->error, XLS_ERROR_MEMORY, "解析共享字符串时内存不足");
            context->failed = 1;
            return;
        }
        context->strings.items = replacement;
        context->strings.items[context->strings.count]
            = xls_strdup(context->current.data);
        if (context->strings.items[context->strings.count] == NULL) {
            xls_set_error(context->error, XLS_ERROR_MEMORY, "复制共享字符串时内存不足");
            context->failed = 1;
            return;
        }
        ++context->strings.count;
        context->in_item = 0;
    }
}

static void XMLCALL shared_string_text(
    void *user_data,
    const char *text,
    int length
)
{
    shared_string_context *context = (shared_string_context *)user_data;
    if (!context->failed && context->in_item && context->in_text) {
        if (!text_append(
            &context->current, text, (size_t)length, context->error
        )) {
            context->failed = 1;
        }
    }
}

typedef struct styles_context {
    style_table table;
    int in_cell_formats;
    xls_error *error;
    int failed;
} styles_context;

static void XMLCALL styles_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    styles_context *context = (styles_context *)user_data;
    const char *name = local_name(element);
    if (context->failed) {
        return;
    }
    if (strcmp(name, "numFmt") == 0) {
        const char *id_text = attribute_value(attributes, "numFmtId");
        const char *code = attribute_value(attributes, "formatCode");
        number_format *replacement;
        if (id_text == NULL || code == NULL) {
            return;
        }
        replacement = (number_format *)realloc(
            context->table.formats,
            (context->table.format_count + 1) * sizeof(*context->table.formats)
        );
        if (replacement == NULL) {
            xls_set_error(context->error, XLS_ERROR_MEMORY, "解析数字格式时内存不足");
            context->failed = 1;
            return;
        }
        context->table.formats = replacement;
        context->table.formats[context->table.format_count].id = atoi(id_text);
        context->table.formats[context->table.format_count].code = xls_strdup(code);
        if (context->table.formats[context->table.format_count].code == NULL) {
            xls_set_error(context->error, XLS_ERROR_MEMORY, "复制数字格式时内存不足");
            context->failed = 1;
            return;
        }
        ++context->table.format_count;
    } else if (strcmp(name, "cellXfs") == 0) {
        context->in_cell_formats = 1;
    } else if (context->in_cell_formats && strcmp(name, "xf") == 0) {
        const char *format_id = attribute_value(attributes, "numFmtId");
        int *replacement = (int *)realloc(
            context->table.cell_format_ids,
            (context->table.cell_format_count + 1)
                * sizeof(*context->table.cell_format_ids)
        );
        if (replacement == NULL) {
            xls_set_error(context->error, XLS_ERROR_MEMORY, "解析单元格格式时内存不足");
            context->failed = 1;
            return;
        }
        context->table.cell_format_ids = replacement;
        context->table.cell_format_ids[context->table.cell_format_count++]
            = format_id == NULL ? 0 : atoi(format_id);
    }
}

static void XMLCALL styles_end(void *user_data, const char *element)
{
    styles_context *context = (styles_context *)user_data;
    if (strcmp(local_name(element), "cellXfs") == 0) {
        context->in_cell_formats = 0;
    }
}

static const char *builtin_format_code(int id)
{
    switch (id) {
    case 0: return "General";
    case 1: return "0";
    case 2: return "0.00";
    case 3: return "#,##0";
    case 4: return "#,##0.00";
    case 9: return "0%";
    case 10: return "0.00%";
    case 11: return "0.00E+00";
    case 12: return "# ?/?";
    case 13: return "# ?" "?/??";
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
        return "yyyy-MM-dd";
    case 37: return "#,##0 ;(#,##0)";
    case 38: return "#,##0 ;[Red](#,##0)";
    case 39: return "#,##0.00;(#,##0.00)";
    case 40: return "#,##0.00;[Red](#,##0.00)";
    case 45: return "mm:ss";
    case 46: return "[h]:mm:ss";
    case 47: return "mmss.0";
    case 48: return "##0.0E+0";
    case 49: return "@";
    default: return NULL;
    }
}

static int style_format_id(const style_table *styles, int style_index)
{
    if (style_index < 0
        || (size_t)style_index >= styles->cell_format_count) {
        return -1;
    }
    return styles->cell_format_ids[style_index];
}

static const char *style_format_code(
    const style_table *styles,
    int style_index
)
{
    int id = style_format_id(styles, style_index);
    size_t index;
    for (index = 0; index < styles->format_count; ++index) {
        if (styles->formats[index].id == id) {
            return styles->formats[index].code;
        }
    }
    return builtin_format_code(id);
}

static int style_is_date(const style_table *styles, int style_index)
{
    int id = style_format_id(styles, style_index);
    const char *code;
    const unsigned char *cursor;
    int quoted = 0;
    int bracketed = 0;
    if ((id >= 14 && id <= 22) || (id >= 45 && id <= 47)) {
        return 1;
    }
    code = style_format_code(styles, style_index);
    if (code == NULL) {
        return 0;
    }
    cursor = (const unsigned char *)code;
    while (*cursor != '\0') {
        if (*cursor == '"') {
            quoted = !quoted;
        } else if (!quoted && *cursor == '[') {
            bracketed = 1;
        } else if (!quoted && *cursor == ']') {
            bracketed = 0;
        } else if (!quoted && !bracketed
            && strchr("ymdhsYMDHS", *cursor) != NULL) {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static int coordinate_parse(
    const char *reference,
    size_t *row,
    size_t *column
)
{
    size_t index = 0;
    size_t column_value = 0;
    unsigned long row_value;
    char *end;
    if (reference == NULL || row == NULL || column == NULL) {
        return 0;
    }
    while (reference[index] != '\0' && isalpha((unsigned char)reference[index])) {
        column_value = column_value * 26u
            + (size_t)(toupper((unsigned char)reference[index]) - 'A' + 1);
        ++index;
    }
    if (index == 0 || column_value == 0 || !isdigit((unsigned char)reference[index])) {
        return 0;
    }
    row_value = strtoul(reference + index, &end, 10);
    if (row_value == 0 || end == reference + index || *end != '\0') {
        return 0;
    }
    *row = (size_t)(row_value - 1);
    *column = column_value - 1;
    return 1;
}

static int range_parse(const char *reference, merge_range *range)
{
    const char *separator;
    char *first;
    int ok;
    size_t row1 = 0;
    size_t column1 = 0;
    size_t row2 = 0;
    size_t column2 = 0;
    if (reference == NULL || range == NULL) {
        return 0;
    }
    separator = strchr(reference, ':');
    if (separator == NULL) {
        return 0;
    }
    first = xls_strndup(reference, (size_t)(separator - reference));
    if (first == NULL) {
        return 0;
    }
    ok = coordinate_parse(first, &row1, &column1)
        && coordinate_parse(separator + 1, &row2, &column2);
    free(first);
    if (!ok) {
        return 0;
    }
    range->first_row = row1 < row2 ? row1 : row2;
    range->last_row = row1 > row2 ? row1 : row2;
    range->first_column = column1 < column2 ? column1 : column2;
    range->last_column = column1 > column2 ? column1 : column2;
    return 1;
}

static void civil_from_days(
    long days_since_1970,
    int *year,
    unsigned int *month,
    unsigned int *day
)
{
    long era;
    unsigned long day_of_era;
    unsigned long year_of_era;
    long computed_year;
    unsigned long day_of_year;
    unsigned long month_prime;
    days_since_1970 += 719468;
    era = (days_since_1970 >= 0 ? days_since_1970 : days_since_1970 - 146096)
        / 146097;
    day_of_era = (unsigned long)(days_since_1970 - era * 146097);
    year_of_era = (day_of_era - day_of_era / 1460
        + day_of_era / 36524 - day_of_era / 146096) / 365;
    computed_year = (long)year_of_era + era * 400;
    day_of_year = day_of_era
        - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    month_prime = (5 * day_of_year + 2) / 153;
    *day = (unsigned int)(day_of_year - (153 * month_prime + 2) / 5 + 1);
    *month = (unsigned int)(month_prime < 10 ? month_prime + 3 : month_prime - 9);
    computed_year += *month <= 2;
    *year = (int)computed_year;
}

static char *excel_date(double serial)
{
    int year;
    unsigned int month;
    unsigned int day;
    char buffer[32];
    long days_since_1970 = (long)floor(serial) - 25569;
    civil_from_days(days_since_1970, &year, &month, &day);
    (void)snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
    return xls_strdup(buffer);
}

typedef struct worksheet_context {
    xls_sheet *sheet;
    const string_list *shared_strings;
    const style_table *styles;
    merge_range *ranges;
    size_t range_count;
    size_t current_row;
    size_t current_column;
    int current_style;
    char current_type[16];
    int in_cell;
    int in_value;
    int in_inline_text;
    text_buffer raw_value;
    text_buffer inline_text;
    xls_error *error;
    int failed;
} worksheet_context;

static int worksheet_reserve_cell(
    worksheet_context *context,
    size_t row,
    size_t column
)
{
    size_t rows = context->sheet->row_count;
    size_t columns = context->sheet->column_count;
    if (row < rows && column < columns) {
        return 1;
    }
    if (rows <= row) {
        rows = row + 1;
    }
    if (columns <= column) {
        columns = column + 1;
    }
    if (!xls_sheet_resize(context->sheet, rows, columns, context->error)) {
        context->failed = 1;
        return 0;
    }
    return 1;
}

static int worksheet_commit_cell(worksheet_context *context)
{
    const char *raw = context->raw_value.data == NULL
        ? ""
        : context->raw_value.data;
    const char *format_code
        = style_format_code(context->styles, context->current_style);
    const char *display = raw;
    char *owned_display = NULL;
    double numeric = 0.0;
    const double *numeric_pointer = NULL;
    int is_date = 0;
    xls_cell *cell;
    if (!worksheet_reserve_cell(
        context, context->current_row, context->current_column
    )) {
        return 0;
    }
    if (strcmp(context->current_type, "s") == 0) {
        long index = strtol(raw, NULL, 10);
        if (index >= 0 && (size_t)index < context->shared_strings->count) {
            display = context->shared_strings->items[index];
        } else {
            display = "";
        }
    } else if (strcmp(context->current_type, "inlineStr") == 0) {
        display = context->inline_text.data == NULL
            ? ""
            : context->inline_text.data;
    } else if (strcmp(context->current_type, "b") == 0) {
        display = strcmp(raw, "1") == 0 ? "TRUE" : "FALSE";
    } else if (strcmp(context->current_type, "str") == 0) {
        display = raw;
    } else {
        char *end = NULL;
        numeric = strtod(raw, &end);
        if (end != raw && end != NULL && *end == '\0' && isfinite(numeric)) {
            numeric_pointer = &numeric;
            if (style_is_date(context->styles, context->current_style)) {
                owned_display = excel_date(numeric);
                if (owned_display == NULL) {
                    xls_set_error(context->error, XLS_ERROR_MEMORY, "格式化日期时内存不足");
                    context->failed = 1;
                    return 0;
                }
                display = owned_display;
                is_date = 1;
            } else if (strchr(raw, 'e') != NULL || strchr(raw, 'E') != NULL) {
                owned_display = xls_format_number(numeric, NULL);
                if (owned_display == NULL) {
                    xls_set_error(context->error, XLS_ERROR_MEMORY, "格式化数值时内存不足");
                    context->failed = 1;
                    return 0;
                }
                display = owned_display;
            }
        }
    }
    cell = xls_sheet_cell(
        context->sheet, context->current_row, context->current_column
    );
    if (!xls_cell_set(
        cell, display, raw, numeric_pointer, format_code, is_date, context->error
    )) {
        free(owned_display);
        context->failed = 1;
        return 0;
    }
    free(owned_display);
    return 1;
}

static void XMLCALL worksheet_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    worksheet_context *context = (worksheet_context *)user_data;
    const char *name = local_name(element);
    if (context->failed) {
        return;
    }
    if (strcmp(name, "c") == 0) {
        const char *reference = attribute_value(attributes, "r");
        const char *type = attribute_value(attributes, "t");
        const char *style = attribute_value(attributes, "s");
        if (!coordinate_parse(
            reference, &context->current_row, &context->current_column
        )) {
            return;
        }
        context->current_style = style == NULL ? -1 : atoi(style);
        (void)snprintf(
            context->current_type,
            sizeof(context->current_type),
            "%s",
            type == NULL ? "" : type
        );
        text_clear(&context->raw_value);
        text_clear(&context->inline_text);
        context->in_cell = 1;
    } else if (strcmp(name, "mergeCell") == 0) {
        const char *reference = attribute_value(attributes, "ref");
        merge_range range;
        if (range_parse(reference, &range)) {
            merge_range *replacement = (merge_range *)realloc(
                context->ranges,
                (context->range_count + 1) * sizeof(*context->ranges)
            );
            if (replacement == NULL) {
                xls_set_error(context->error, XLS_ERROR_MEMORY, "解析合并单元格时内存不足");
                context->failed = 1;
                return;
            }
            context->ranges = replacement;
            context->ranges[context->range_count++] = range;
        }
    } else if (context->in_cell && strcmp(name, "v") == 0) {
        context->in_value = 1;
    } else if (context->in_cell && strcmp(name, "t") == 0) {
        context->in_inline_text = 1;
    }
}

static void XMLCALL worksheet_end(void *user_data, const char *element)
{
    worksheet_context *context = (worksheet_context *)user_data;
    const char *name = local_name(element);
    if (strcmp(name, "v") == 0) {
        context->in_value = 0;
    } else if (strcmp(name, "t") == 0) {
        context->in_inline_text = 0;
    } else if (strcmp(name, "c") == 0 && context->in_cell) {
        (void)worksheet_commit_cell(context);
        context->in_cell = 0;
    }
}

static void XMLCALL worksheet_text(
    void *user_data,
    const char *text,
    int length
)
{
    worksheet_context *context = (worksheet_context *)user_data;
    if (context->failed || !context->in_cell) {
        return;
    }
    if (context->in_value) {
        if (!text_append(
            &context->raw_value, text, (size_t)length, context->error
        )) {
            context->failed = 1;
        }
    } else if (context->in_inline_text) {
        if (!text_append(
            &context->inline_text, text, (size_t)length, context->error
        )) {
            context->failed = 1;
        }
    }
}

static int copy_cell(
    const xls_cell *source,
    xls_cell *target,
    xls_error *error
)
{
    const double *numeric = (source->flags & XLS_CELL_HAS_NUMERIC_VALUE) != 0u
        ? &source->numeric_value
        : NULL;
    return xls_cell_set(
        target,
        source->value,
        (source->flags & XLS_CELL_HAS_RAW_VALUE) != 0u
            ? source->raw_value
            : NULL,
        numeric,
        (source->flags & XLS_CELL_HAS_FORMAT_CODE) != 0u
            ? source->format_code
            : NULL,
        (source->flags & XLS_CELL_IS_DATE) != 0u,
        error
    );
}

static int apply_merge_ranges(worksheet_context *context)
{
    size_t range_index;
    for (range_index = 0; range_index < context->range_count; ++range_index) {
        const merge_range *range = &context->ranges[range_index];
        const xls_cell *source;
        size_t row;
        size_t column;
        if (!worksheet_reserve_cell(
            context, range->last_row, range->last_column
        )) {
            return 0;
        }
        source = xls_sheet_cell_const(
            context->sheet, range->first_row, range->first_column
        );
        if (source == NULL) {
            continue;
        }
        for (row = range->first_row; row <= range->last_row; ++row) {
            for (column = range->first_column;
                 column <= range->last_column;
                 ++column) {
                xls_cell *target = xls_sheet_cell(context->sheet, row, column);
                if (target != NULL
                    && xls_string_empty(target->value)
                    && target->flags == 0u
                    && !copy_cell(source, target, context->error)) {
                    context->failed = 1;
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void string_list_free(string_list *strings)
{
    size_t index;
    for (index = 0; index < strings->count; ++index) {
        free(strings->items[index]);
    }
    free(strings->items);
    memset(strings, 0, sizeof(*strings));
}

static void style_table_free(style_table *styles)
{
    size_t index;
    for (index = 0; index < styles->format_count; ++index) {
        free(styles->formats[index].code);
    }
    free(styles->formats);
    free(styles->cell_format_ids);
    memset(styles, 0, sizeof(*styles));
}

static const char *relationship_target(
    const relationship_context *relationships,
    const char *id
)
{
    size_t index;
    for (index = 0; index < relationships->count; ++index) {
        if (strcmp(relationships->items[index].id, id) == 0) {
            return relationships->items[index].target;
        }
    }
    return NULL;
}

static int zip_parse_xml(
    const xls_zip_archive *archive,
    const char *entry,
    XML_StartElementHandler start,
    XML_EndElementHandler end,
    XML_CharacterDataHandler characters,
    void *context,
    xls_error *error,
    const char *description
)
{
    unsigned char *data = NULL;
    size_t size = 0;
    int result;
    if (!xls_zip_read(archive, entry, &data, &size, error)) {
        return 0;
    }
    result = parse_xml(
        data, size, start, end, characters, context, error, description
    );
    free(data);
    return result;
}

static void parser_contexts_free(
    workbook_context *workbook,
    relationship_context *relationships,
    shared_string_context *shared,
    styles_context *styles
)
{
    size_t index;
    for (index = 0; index < workbook->count; ++index) {
        free(workbook->items[index].name);
        free(workbook->items[index].relationship_id);
    }
    free(workbook->items);
    for (index = 0; index < relationships->count; ++index) {
        free(relationships->items[index].id);
        free(relationships->items[index].target);
    }
    free(relationships->items);
    string_list_free(&shared->strings);
    text_free(&shared->current);
    style_table_free(&styles->table);
}

static const char *path_filename(const char *path)
{
    const char *slash;
    const char *backslash;
    if (path == NULL) {
        return "";
    }
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        slash = backslash;
    }
    return slash == NULL ? path : slash + 1;
}

static int parse_xlsx(
    const char *path,
    xls_workbook *result,
    xls_error *error
)
{
    xls_zip_archive archive;
    workbook_context workbook;
    relationship_context relationships;
    shared_string_context shared;
    styles_context styles;
    size_t index;
    int success = 0;
    memset(&archive, 0, sizeof(archive));
    memset(&workbook, 0, sizeof(workbook));
    memset(&relationships, 0, sizeof(relationships));
    memset(&shared, 0, sizeof(shared));
    memset(&styles, 0, sizeof(styles));
    workbook.error = error;
    relationships.error = error;
    shared.error = error;
    styles.error = error;

    if (!xls_zip_open(path, &archive, error)
        || !zip_parse_xml(
            &archive, "xl/workbook.xml",
            workbook_start, NULL, NULL, &workbook, error, "workbook.xml"
        )
        || workbook.failed
        || !zip_parse_xml(
            &archive, "xl/_rels/workbook.xml.rels",
            relationship_start, NULL, NULL, &relationships, error,
            "workbook.xml.rels"
        )
        || relationships.failed) {
        goto cleanup;
    }
    if (xls_zip_contains(&archive, "xl/sharedStrings.xml")) {
        if (!zip_parse_xml(
            &archive, "xl/sharedStrings.xml",
            shared_string_start, shared_string_end, shared_string_text,
            &shared, error, "sharedStrings.xml"
        ) || shared.failed) {
            goto cleanup;
        }
    }
    if (xls_zip_contains(&archive, "xl/styles.xml")) {
        if (!zip_parse_xml(
            &archive, "xl/styles.xml",
            styles_start, styles_end, NULL, &styles, error, "styles.xml"
        ) || styles.failed) {
            goto cleanup;
        }
    }
    result->filename = xls_strdup(path_filename(path));
    result->filepath = xls_strdup(path);
    if (result->filename == NULL || result->filepath == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "复制工作簿路径时内存不足");
        goto cleanup;
    }
    if (workbook.count > 0) {
        result->sheets = (xls_sheet *)calloc(
            workbook.count, sizeof(*result->sheets)
        );
        if (result->sheets == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "分配工作表时内存不足");
            goto cleanup;
        }
    }
    for (index = 0; index < workbook.count; ++index) {
        const char *target = relationship_target(
            &relationships, workbook.items[index].relationship_id
        );
        worksheet_context worksheet;
        if (target == NULL || !xls_zip_contains(&archive, target)) {
            continue;
        }
        memset(&worksheet, 0, sizeof(worksheet));
        worksheet.sheet = &result->sheets[result->sheet_count];
        worksheet.shared_strings = &shared.strings;
        worksheet.styles = &styles.table;
        worksheet.error = error;
        if (!xls_sheet_init(
            worksheet.sheet, workbook.items[index].name, 0, 0, error
        ) || !zip_parse_xml(
            &archive, target,
            worksheet_start, worksheet_end, worksheet_text,
            &worksheet, error, workbook.items[index].name
        ) || worksheet.failed
            || !apply_merge_ranges(&worksheet)) {
            text_free(&worksheet.raw_value);
            text_free(&worksheet.inline_text);
            free(worksheet.ranges);
            goto cleanup;
        }
        text_free(&worksheet.raw_value);
        text_free(&worksheet.inline_text);
        free(worksheet.ranges);
        ++result->sheet_count;
    }
    success = 1;
    xls_error_clear(error);

cleanup:
    xls_zip_close(&archive);
    parser_contexts_free(&workbook, &relationships, &shared, &styles);
    if (!success) {
        xls_workbook_free(result);
    }
    return success;
}

static int parse_delimited(
    const char *path,
    xls_workbook *result,
    xls_error *error
)
{
    FILE *file;
    char *line = NULL;
    size_t capacity = 0;
    size_t row = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        xls_set_error(error, XLS_ERROR_IO, "无法打开文件: %s", path);
        return 0;
    }
    result->filename = xls_strdup(path_filename(path));
    result->filepath = xls_strdup(path);
    result->sheets = (xls_sheet *)calloc(1, sizeof(*result->sheets));
    if (result->filename == NULL || result->filepath == NULL
        || result->sheets == NULL
        || !xls_sheet_init(result->sheets, "Sheet1", 0, 0, error)) {
        fclose(file);
        xls_workbook_free(result);
        return 0;
    }
    result->sheet_count = 1;
    for (;;) {
        int character;
        size_t length = 0;
        size_t column = 0;
        int quoted = 0;
        text_buffer value;
        memset(&value, 0, sizeof(value));
        while ((character = fgetc(file)) != EOF && character != '\n') {
            if (length + 1 >= capacity) {
                size_t new_capacity = capacity == 0 ? 256 : capacity * 2;
                char *replacement = (char *)realloc(line, new_capacity);
                if (replacement == NULL) {
                    text_free(&value);
                    free(line);
                    fclose(file);
                    xls_workbook_free(result);
                    xls_set_error(error, XLS_ERROR_MEMORY, "读取文本表格时内存不足");
                    return 0;
                }
                line = replacement;
                capacity = new_capacity;
            }
            line[length++] = (char)character;
        }
        if (character == EOF && length == 0) {
            text_free(&value);
            break;
        }
        if (length > 0 && line[length - 1] == '\r') {
            --length;
        }
        {
            size_t index;
            for (index = 0; index <= length; ++index) {
                char current = index == length ? '\0' : line[index];
                if (current == '"') {
                    quoted = !quoted;
                    continue;
                }
                if ((!quoted && (current == ',' || current == '\t'))
                    || current == '\0') {
                    if (!xls_sheet_resize(
                        result->sheets,
                        row + 1,
                        column + 1 > result->sheets->column_count
                            ? column + 1
                            : result->sheets->column_count,
                        error
                    ) || !xls_cell_set(
                        xls_sheet_cell(result->sheets, row, column),
                        value.data, NULL, NULL, NULL, 0, error
                    )) {
                        text_free(&value);
                        free(line);
                        fclose(file);
                        xls_workbook_free(result);
                        return 0;
                    }
                    text_clear(&value);
                    ++column;
                } else if (!text_append(&value, &current, 1, error)) {
                    text_free(&value);
                    free(line);
                    fclose(file);
                    xls_workbook_free(result);
                    return 0;
                }
            }
        }
        text_free(&value);
        ++row;
    }
    free(line);
    fclose(file);
    xls_error_clear(error);
    return 1;
}

static int suffix_equals(const char *path, const char *suffix)
{
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    size_t index;
    if (path_length < suffix_length) {
        return 0;
    }
    for (index = 0; index < suffix_length; ++index) {
        unsigned char left = (unsigned char)path[path_length - suffix_length + index];
        unsigned char right = (unsigned char)suffix[index];
        if (tolower(left) != tolower(right)) {
            return 0;
        }
    }
    return 1;
}

int xls_parse_file(
    const char *path,
    xls_workbook *workbook,
    xls_error *error
)
{
    if (path == NULL || workbook == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid parse arguments");
        return 0;
    }
    memset(workbook, 0, sizeof(*workbook));
    if (suffix_equals(path, ".xlsx")) {
        return parse_xlsx(path, workbook, error);
    }
    if (suffix_equals(path, ".csv") || suffix_equals(path, ".tsv")) {
        return parse_delimited(path, workbook, error);
    }
    if (suffix_equals(path, ".xls")) {
        return xls_parse_biff8_file(path, workbook, error);
    }
    xls_set_error(error, XLS_ERROR_UNSUPPORTED, "不支持的文件格式: %s", path);
    return 0;
}
