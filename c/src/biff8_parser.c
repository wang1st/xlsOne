#include "biff8_parser.h"
#include "compound_file.h"
#include "internal.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct biff_sheet_ref {
    uint32_t offset;
    char *name;
} biff_sheet_ref;

typedef struct biff_format {
    uint16_t id;
    char *code;
} biff_format;

typedef struct biff_globals {
    biff_sheet_ref *sheets;
    size_t sheet_count;
    char **shared_strings;
    size_t shared_string_count;
    biff_format *formats;
    size_t format_count;
    uint16_t *xf_format_ids;
    size_t xf_count;
} biff_globals;

typedef struct biff_range {
    size_t first_row;
    size_t last_row;
    size_t first_column;
    size_t last_column;
} biff_range;

typedef struct sst_segment {
    const unsigned char *data;
    size_t size;
} sst_segment;

typedef struct sst_cursor {
    const sst_segment *segments;
    size_t segment_count;
    size_t segment_index;
    size_t offset;
} sst_cursor;

static uint8_t biff_u8(const unsigned char *data)
{
    return data[0];
}

static uint16_t biff_u16(const unsigned char *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t biff_u32(const unsigned char *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8u)
        | ((uint32_t)data[2] << 16u)
        | ((uint32_t)data[3] << 24u);
}

static double biff_f64(const unsigned char *data)
{
    uint64_t bits = (uint64_t)biff_u32(data)
        | ((uint64_t)biff_u32(data + 4) << 32u);
    double result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static int utf8_append(
    char **text,
    size_t *length,
    size_t *capacity,
    uint32_t codepoint
)
{
    unsigned char encoded[4];
    size_t encoded_size;
    char *replacement;
    if (codepoint <= 0x7f) {
        encoded[0] = (unsigned char)codepoint;
        encoded_size = 1;
    } else if (codepoint <= 0x7ff) {
        encoded[0] = (unsigned char)(0xc0u | (codepoint >> 6u));
        encoded[1] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        encoded_size = 2;
    } else if (codepoint <= 0xffff) {
        encoded[0] = (unsigned char)(0xe0u | (codepoint >> 12u));
        encoded[1] = (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        encoded_size = 3;
    } else {
        encoded[0] = (unsigned char)(0xf0u | (codepoint >> 18u));
        encoded[1] = (unsigned char)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        encoded_size = 4;
    }
    if (*length + encoded_size + 1 > *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        while (new_capacity < *length + encoded_size + 1) {
            new_capacity *= 2;
        }
        replacement = (char *)realloc(*text, new_capacity);
        if (replacement == NULL) {
            return 0;
        }
        *text = replacement;
        *capacity = new_capacity;
    }
    memcpy(*text + *length, encoded, encoded_size);
    *length += encoded_size;
    (*text)[*length] = '\0';
    return 1;
}

static char *decode_biff_string(
    const unsigned char *data,
    size_t character_count,
    int wide
)
{
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    size_t index;
    for (index = 0; index < character_count; ++index) {
        uint32_t codepoint = wide
            ? biff_u16(data + index * 2)
            : data[index];
        if (!utf8_append(&result, &length, &capacity, codepoint)) {
            free(result);
            return NULL;
        }
    }
    return result == NULL ? xls_strdup("") : result;
}

static char *parse_xl_unicode(
    const unsigned char *data,
    size_t size,
    size_t offset,
    size_t *next_offset
)
{
    uint16_t character_count;
    uint8_t flags;
    size_t cursor;
    uint16_t rich_runs = 0;
    uint32_t extension_bytes = 0;
    size_t byte_count;
    char *value;
    if (offset + 3 > size) {
        return NULL;
    }
    character_count = biff_u16(data + offset);
    flags = biff_u8(data + offset + 2);
    cursor = offset + 3;
    if ((flags & 0x08u) != 0u) {
        if (cursor + 2 > size) {
            return NULL;
        }
        rich_runs = biff_u16(data + cursor);
        cursor += 2;
    }
    if ((flags & 0x04u) != 0u) {
        if (cursor + 4 > size) {
            return NULL;
        }
        extension_bytes = biff_u32(data + cursor);
        cursor += 4;
    }
    byte_count = (size_t)character_count
        * (((flags & 0x01u) != 0u) ? 2u : 1u);
    if (cursor > size || byte_count > size - cursor) {
        return NULL;
    }
    value = decode_biff_string(
        data + cursor,
        character_count,
        (flags & 0x01u) != 0u
    );
    cursor += byte_count;
    if ((size_t)rich_runs * 4u > size - cursor) {
        free(value);
        return NULL;
    }
    cursor += (size_t)rich_runs * 4u;
    if ((size_t)extension_bytes > size - cursor) {
        free(value);
        return NULL;
    }
    cursor += extension_bytes;
    if (next_offset != NULL) {
        *next_offset = cursor;
    }
    return value;
}

static const char *default_format(uint16_t id)
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
    case 14: return "yyyy-MM-dd";
    case 15: return "d-mmm-yy";
    case 16: return "d-mmm";
    case 17: return "mmm-yy";
    case 18: return "h:mm AM/PM";
    case 19: return "h:mm:ss AM/PM";
    case 20: return "h:mm";
    case 21: return "h:mm:ss";
    case 22: return "m/d/yy h:mm";
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

static const char *format_code(const biff_globals *globals, uint16_t xf_index)
{
    uint16_t format_id;
    size_t index;
    if ((size_t)xf_index >= globals->xf_count) {
        return NULL;
    }
    format_id = globals->xf_format_ids[xf_index];
    for (index = 0; index < globals->format_count; ++index) {
        if (globals->formats[index].id == format_id) {
            return globals->formats[index].code;
        }
    }
    return default_format(format_id);
}

static int format_is_date(const char *code)
{
    int quoted = 0;
    int bracketed = 0;
    const unsigned char *cursor = (const unsigned char *)code;
    if (code == NULL
        || xls_string_contains_ascii_ci(code, "general")
        || strchr(code, '@') != NULL) {
        return 0;
    }
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

static char *date_display(double number)
{
    int year;
    unsigned int month;
    unsigned int day;
    char buffer[32];
    civil_from_days((long)floor(number) - 25569, &year, &month, &day);
    (void)snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
    return xls_strdup(buffer);
}

static char *number_display(double number)
{
    char buffer[128];
    (void)snprintf(
        buffer,
        sizeof(buffer),
        fabs(number - round(number)) < 0.0000001 ? "%.0f" : "%.15g",
        number
    );
    return xls_strdup(buffer);
}

static int set_numeric_cell(
    xls_sheet *sheet,
    size_t row,
    size_t column,
    double number,
    uint16_t xf_index,
    const biff_globals *globals,
    xls_error *error
)
{
    const char *code = format_code(globals, xf_index);
    int is_date = format_is_date(code);
    char *display = is_date ? date_display(number) : number_display(number);
    char *raw = number_display(number);
    int result;
    if (display == NULL || raw == NULL) {
        free(display);
        free(raw);
        xls_set_error(error, XLS_ERROR_MEMORY, "格式化 BIFF8 数值时内存不足");
        return 0;
    }
    result = xls_cell_set(
        xls_sheet_cell(sheet, row, column),
        display,
        raw,
        &number,
        code,
        is_date,
        error
    );
    free(display);
    free(raw);
    return result;
}

static int reserve_cell(
    xls_sheet *sheet,
    size_t row,
    size_t column,
    xls_error *error
)
{
    size_t rows = sheet->row_count > row ? sheet->row_count : row + 1;
    size_t columns = sheet->column_count > column
        ? sheet->column_count
        : column + 1;
    if (rows == sheet->row_count && columns == sheet->column_count) {
        return 1;
    }
    return xls_sheet_resize(sheet, rows, columns, error);
}

static int globals_add_sheet(
    biff_globals *globals,
    const unsigned char *body,
    size_t length,
    xls_error *error
)
{
    biff_sheet_ref *replacement;
    biff_sheet_ref *sheet;
    size_t name_length;
    int wide;
    if (length < 8) {
        return 1;
    }
    replacement = (biff_sheet_ref *)realloc(
        globals->sheets,
        (globals->sheet_count + 1) * sizeof(*globals->sheets)
    );
    if (replacement == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 工作表时内存不足");
        return 0;
    }
    globals->sheets = replacement;
    sheet = &globals->sheets[globals->sheet_count++];
    memset(sheet, 0, sizeof(*sheet));
    sheet->offset = biff_u32(body);
    name_length = body[6];
    wide = (body[7] & 0x01u) != 0u;
    if (8 + name_length * (wide ? 2u : 1u) <= length) {
        sheet->name = decode_biff_string(body + 8, name_length, wide);
    }
    if (sheet->name == NULL) {
        sheet->name = xls_strdup("Sheet");
    }
    if (sheet->name == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "复制 BIFF8 工作表名称时内存不足");
        return 0;
    }
    return 1;
}

static int globals_add_format(
    biff_globals *globals,
    const unsigned char *body,
    size_t length,
    xls_error *error
)
{
    char *code;
    biff_format *replacement;
    if (length < 5) {
        return 1;
    }
    code = parse_xl_unicode(body, length, 2, NULL);
    if (code == NULL) {
        return 1;
    }
    replacement = (biff_format *)realloc(
        globals->formats,
        (globals->format_count + 1) * sizeof(*globals->formats)
    );
    if (replacement == NULL) {
        free(code);
        xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 格式时内存不足");
        return 0;
    }
    globals->formats = replacement;
    globals->formats[globals->format_count].id = biff_u16(body);
    globals->formats[globals->format_count].code = code;
    ++globals->format_count;
    return 1;
}

static int globals_add_xf(
    biff_globals *globals,
    const unsigned char *body,
    size_t length,
    xls_error *error
)
{
    uint16_t *replacement;
    if (length < 4) {
        return 1;
    }
    replacement = (uint16_t *)realloc(
        globals->xf_format_ids,
        (globals->xf_count + 1) * sizeof(*globals->xf_format_ids)
    );
    if (replacement == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 XF 时内存不足");
        return 0;
    }
    globals->xf_format_ids = replacement;
    globals->xf_format_ids[globals->xf_count++] = biff_u16(body + 2);
    return 1;
}

static int sst_read_bytes(
    sst_cursor *cursor,
    unsigned char *output,
    size_t count
)
{
    size_t remaining = count;
    while (remaining > 0) {
        size_t available;
        size_t chunk;
        if (cursor->segment_index >= cursor->segment_count) {
            return 0;
        }
        available = cursor->segments[cursor->segment_index].size
            - cursor->offset;
        if (available == 0) {
            ++cursor->segment_index;
            cursor->offset = 0;
            continue;
        }
        chunk = available < remaining ? available : remaining;
        if (output != NULL) {
            memcpy(
                output + count - remaining,
                cursor->segments[cursor->segment_index].data + cursor->offset,
                chunk
            );
        }
        cursor->offset += chunk;
        remaining -= chunk;
    }
    return 1;
}

static int sst_read_u8(sst_cursor *cursor, uint8_t *value)
{
    return sst_read_bytes(cursor, value, 1);
}

static int sst_read_u16(sst_cursor *cursor, uint16_t *value)
{
    unsigned char bytes[2];
    if (!sst_read_bytes(cursor, bytes, sizeof(bytes))) {
        return 0;
    }
    *value = biff_u16(bytes);
    return 1;
}

static int sst_read_u32(sst_cursor *cursor, uint32_t *value)
{
    unsigned char bytes[4];
    if (!sst_read_bytes(cursor, bytes, sizeof(bytes))) {
        return 0;
    }
    *value = biff_u32(bytes);
    return 1;
}

static int sst_prepare_character(
    sst_cursor *cursor,
    int *wide,
    size_t character_bytes
)
{
    while (cursor->segment_index < cursor->segment_count) {
        size_t available = cursor->segments[cursor->segment_index].size
            - cursor->offset;
        if (available >= character_bytes) {
            return 1;
        }
        if (available != 0) {
            return 0;
        }
        ++cursor->segment_index;
        cursor->offset = 0;
        if (cursor->segment_index >= cursor->segment_count) {
            return 0;
        }
        {
            uint8_t continuation_flags;
            if (!sst_read_u8(cursor, &continuation_flags)) {
                return 0;
            }
            *wide = (continuation_flags & 0x01u) != 0u;
            character_bytes = *wide ? 2u : 1u;
        }
    }
    return 0;
}

static char *sst_read_string(sst_cursor *cursor)
{
    uint16_t character_count;
    uint8_t flags;
    uint16_t rich_runs = 0;
    uint32_t extension_bytes = 0;
    int wide;
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    uint16_t index;
    if (!sst_read_u16(cursor, &character_count)
        || !sst_read_u8(cursor, &flags)) {
        return NULL;
    }
    wide = (flags & 0x01u) != 0u;
    if ((flags & 0x08u) != 0u && !sst_read_u16(cursor, &rich_runs)) {
        return NULL;
    }
    if ((flags & 0x04u) != 0u
        && !sst_read_u32(cursor, &extension_bytes)) {
        return NULL;
    }
    for (index = 0; index < character_count; ++index) {
        unsigned char bytes[2];
        size_t byte_count = wide ? 2u : 1u;
        uint32_t codepoint;
        if (!sst_prepare_character(cursor, &wide, byte_count)) {
            free(result);
            return NULL;
        }
        byte_count = wide ? 2u : 1u;
        if (!sst_read_bytes(cursor, bytes, byte_count)) {
            free(result);
            return NULL;
        }
        codepoint = wide ? biff_u16(bytes) : bytes[0];
        if (!utf8_append(
            &result, &length, &capacity, codepoint
        )) {
            free(result);
            return NULL;
        }
    }
    if (!sst_read_bytes(cursor, NULL, (size_t)rich_runs * 4u)
        || !sst_read_bytes(cursor, NULL, extension_bytes)) {
        free(result);
        return NULL;
    }
    return result == NULL ? xls_strdup("") : result;
}

static int parse_sst_segments(
    biff_globals *globals,
    const sst_segment *segments,
    size_t segment_count,
    xls_error *error
)
{
    uint32_t unique_count;
    uint32_t index;
    sst_cursor cursor;
    if (segment_count == 0 || segments[0].size < 8) {
        return 1;
    }
    unique_count = biff_u32(segments[0].data + 4);
    if (unique_count > 1000000) {
        xls_set_error(error, XLS_ERROR_FORMAT, "BIFF8 共享字符串数量异常");
        return 0;
    }
    globals->shared_strings = unique_count == 0
        ? NULL
        : (char **)calloc(unique_count, sizeof(*globals->shared_strings));
    if (unique_count > 0 && globals->shared_strings == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 共享字符串时内存不足");
        return 0;
    }
    memset(&cursor, 0, sizeof(cursor));
    cursor.segments = segments;
    cursor.segment_count = segment_count;
    cursor.offset = 8;
    for (index = 0; index < unique_count; ++index) {
        char *value = sst_read_string(&cursor);
        if (value == NULL) {
            break;
        }
        globals->shared_strings[globals->shared_string_count++] = value;
    }
    return 1;
}

static int parse_globals(
    const unsigned char *data,
    size_t size,
    biff_globals *globals,
    xls_error *error
)
{
    size_t offset = 0;
    while (offset + 4 <= size) {
        uint16_t id = biff_u16(data + offset);
        uint16_t length = biff_u16(data + offset + 2);
        const unsigned char *body = data + offset + 4;
        if ((size_t)length > size - offset - 4) {
            break;
        }
        if (id == 0x0085) {
            if (!globals_add_sheet(globals, body, length, error)) {
                return 0;
            }
        } else if (id == 0x00fc) {
            size_t continuation_offset = offset + 4u + length;
            size_t continuation_count = 0;
            sst_segment *segments;
            size_t segment_index;
            while (continuation_offset + 4 <= size
                && biff_u16(data + continuation_offset) == 0x003c) {
                uint16_t continuation_length
                    = biff_u16(data + continuation_offset + 2);
                if ((size_t)continuation_length
                    > size - continuation_offset - 4) {
                    break;
                }
                ++continuation_count;
                continuation_offset += 4u + continuation_length;
            }
            segments = (sst_segment *)calloc(
                continuation_count + 1, sizeof(*segments)
            );
            if (segments == NULL) {
                xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 CONTINUE 时内存不足");
                return 0;
            }
            segments[0].data = body;
            segments[0].size = length;
            continuation_offset = offset + 4u + length;
            for (segment_index = 0;
                 segment_index < continuation_count;
                 ++segment_index) {
                uint16_t continuation_length
                    = biff_u16(data + continuation_offset + 2);
                segments[segment_index + 1].data
                    = data + continuation_offset + 4;
                segments[segment_index + 1].size = continuation_length;
                continuation_offset += 4u + continuation_length;
            }
            if (!parse_sst_segments(
                globals, segments, continuation_count + 1, error
            )) {
                free(segments);
                return 0;
            }
            free(segments);
            offset = continuation_offset;
            continue;
        } else if (id == 0x041e) {
            if (!globals_add_format(globals, body, length, error)) {
                return 0;
            }
        } else if (id == 0x00e0) {
            if (!globals_add_xf(globals, body, length, error)) {
                return 0;
            }
        } else if (id == 0x000a) {
            break;
        }
        offset += 4u + length;
    }
    return 1;
}

static double decode_rk(uint32_t rk)
{
    int divide_by_100 = (rk & 0x01u) != 0u;
    int is_integer = (rk & 0x02u) != 0u;
    double value;
    if (is_integer) {
        int32_t signed_value = (int32_t)(rk & UINT32_C(0xfffffffc));
        value = (double)(signed_value >> 2);
    } else {
        uint64_t bits = (uint64_t)(rk & UINT32_C(0xfffffffc)) << 32u;
        memcpy(&value, &bits, sizeof(value));
    }
    return divide_by_100 ? value / 100.0 : value;
}

static const char *biff_error(uint8_t code)
{
    switch (code) {
    case 0x00: return "#NULL!";
    case 0x07: return "#DIV/0!";
    case 0x0f: return "#VALUE!";
    case 0x17: return "#REF!";
    case 0x1d: return "#NAME?";
    case 0x24: return "#NUM!";
    case 0x2a: return "#N/A";
    default: return "#ERROR";
    }
}

static int parse_biff_sheet(
    const unsigned char *data,
    size_t size,
    const biff_sheet_ref *reference,
    const biff_globals *globals,
    xls_sheet *sheet,
    xls_error *error
)
{
    size_t offset = reference->offset;
    biff_range *ranges = NULL;
    size_t range_count = 0;
    if (!xls_sheet_init(sheet, reference->name, 0, 0, error)) {
        return 0;
    }
    while (offset + 4 <= size) {
        uint16_t id = biff_u16(data + offset);
        uint16_t length = biff_u16(data + offset + 2);
        const unsigned char *body = data + offset + 4;
        if ((size_t)length > size - offset - 4) {
            break;
        }
        if (id == 0x000a) {
            break;
        } else if (id == 0x0200) {
            size_t max_row = 0;
            size_t max_column = 0;
            if (length >= 14) {
                uint32_t row_end = biff_u32(body + 4);
                uint16_t column_end = biff_u16(body + 10);
                max_row = row_end == 0 ? 0 : (size_t)row_end;
                max_column = column_end;
            } else if (length >= 10) {
                uint16_t row_end = biff_u16(body + 2);
                uint16_t column_end = biff_u16(body + 6);
                max_row = row_end;
                max_column = column_end;
            }
            if ((max_row > sheet->row_count
                    || max_column > sheet->column_count)
                && !xls_sheet_resize(
                    sheet,
                    max_row > sheet->row_count ? max_row : sheet->row_count,
                    max_column > sheet->column_count
                        ? max_column
                        : sheet->column_count,
                    error
                )) {
                goto failure;
            }
        } else if (id == 0x00fd && length >= 10) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            uint16_t xf = biff_u16(body + 4);
            uint32_t string_index = biff_u32(body + 6);
            const char *value = string_index < globals->shared_string_count
                ? globals->shared_strings[string_index]
                : "";
            if (!reserve_cell(sheet, row, column, error)
                || !xls_cell_set(
                    xls_sheet_cell(sheet, row, column),
                    value,
                    NULL,
                    NULL,
                    format_code(globals, xf),
                    0,
                    error
                )) {
                goto failure;
            }
        } else if ((id == 0x0204 || id == 0x00d6) && length >= 8) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            uint16_t xf = biff_u16(body + 4);
            char *value = parse_xl_unicode(body, length, 6, NULL);
            int ok;
            if (value == NULL) {
                value = xls_strdup("");
            }
            ok = value != NULL
                && reserve_cell(sheet, row, column, error)
                && xls_cell_set(
                    xls_sheet_cell(sheet, row, column),
                    value,
                    NULL,
                    NULL,
                    format_code(globals, xf),
                    0,
                    error
                );
            free(value);
            if (!ok) {
                goto failure;
            }
        } else if (id == 0x0203 && length >= 14) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            if (!reserve_cell(sheet, row, column, error)
                || !set_numeric_cell(
                    sheet,
                    row,
                    column,
                    biff_f64(body + 6),
                    biff_u16(body + 4),
                    globals,
                    error
                )) {
                goto failure;
            }
        } else if (id == 0x027e && length >= 10) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            if (!reserve_cell(sheet, row, column, error)
                || !set_numeric_cell(
                    sheet,
                    row,
                    column,
                    decode_rk(biff_u32(body + 6)),
                    biff_u16(body + 4),
                    globals,
                    error
                )) {
                goto failure;
            }
        } else if (id == 0x00bd && length >= 6) {
            size_t row = biff_u16(body);
            size_t first_column = biff_u16(body + 2);
            size_t last_column = biff_u16(body + length - 2);
            size_t column;
            for (column = first_column; column <= last_column; ++column) {
                size_t record_offset = 4 + (column - first_column) * 6;
                if (record_offset + 6 > (size_t)length - 2) {
                    break;
                }
                if (!reserve_cell(sheet, row, column, error)
                    || !set_numeric_cell(
                        sheet,
                        row,
                        column,
                        decode_rk(biff_u32(body + record_offset + 2)),
                        biff_u16(body + record_offset),
                        globals,
                        error
                    )) {
                    goto failure;
                }
            }
        } else if (id == 0x0205 && length >= 8) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            const char *value = body[7] != 0
                ? biff_error(body[6])
                : body[6] == 0 ? "FALSE" : "TRUE";
            if (!reserve_cell(sheet, row, column, error)
                || !xls_cell_set(
                    xls_sheet_cell(sheet, row, column),
                    value,
                    NULL,
                    NULL,
                    format_code(globals, biff_u16(body + 4)),
                    0,
                    error
                )) {
                goto failure;
            }
        } else if (id == 0x0006 && length >= 14) {
            size_t row = biff_u16(body);
            size_t column = biff_u16(body + 2);
            uint16_t xf = biff_u16(body + 4);
            if (!reserve_cell(sheet, row, column, error)) {
                goto failure;
            }
            if (biff_u16(body + 12) == 0xffffu) {
                const char *value;
                if (body[6] == 0) {
                    value = "";
                } else if (body[6] == 1) {
                    value = body[8] == 0 ? "FALSE" : "TRUE";
                } else if (body[6] == 2) {
                    value = biff_error(body[8]);
                } else {
                    value = "";
                }
                if (!xls_cell_set(
                    xls_sheet_cell(sheet, row, column),
                    value,
                    NULL,
                    NULL,
                    format_code(globals, xf),
                    0,
                    error
                )) {
                    goto failure;
                }
            } else if (!set_numeric_cell(
                sheet,
                row,
                column,
                biff_f64(body + 6),
                xf,
                globals,
                error
            )) {
                goto failure;
            }
        } else if (id == 0x00e5 && length >= 2) {
            uint16_t count = biff_u16(body);
            uint16_t range_index;
            for (range_index = 0; range_index < count; ++range_index) {
                size_t range_offset = 2 + (size_t)range_index * 8;
                biff_range *replacement;
                if (range_offset + 8 > length) {
                    break;
                }
                replacement = (biff_range *)realloc(
                    ranges, (range_count + 1) * sizeof(*ranges)
                );
                if (replacement == NULL) {
                    xls_set_error(error, XLS_ERROR_MEMORY, "解析 BIFF8 合并区域时内存不足");
                    goto failure;
                }
                ranges = replacement;
                ranges[range_count].first_row = biff_u16(body + range_offset);
                ranges[range_count].last_row = biff_u16(body + range_offset + 2);
                ranges[range_count].first_column = biff_u16(body + range_offset + 4);
                ranges[range_count].last_column = biff_u16(body + range_offset + 6);
                ++range_count;
            }
        }
        offset += 4u + length;
    }
    {
        size_t range_index;
        for (range_index = 0; range_index < range_count; ++range_index) {
            biff_range *range = &ranges[range_index];
            if (!reserve_cell(
                sheet, range->last_row, range->last_column, error
            )) {
                goto failure;
            }
        }
    }
    free(ranges);
    return 1;

failure:
    free(ranges);
    xls_sheet_free(sheet);
    return 0;
}

static void globals_free(biff_globals *globals)
{
    size_t index;
    for (index = 0; index < globals->sheet_count; ++index) {
        free(globals->sheets[index].name);
    }
    for (index = 0; index < globals->shared_string_count; ++index) {
        free(globals->shared_strings[index]);
    }
    for (index = 0; index < globals->format_count; ++index) {
        free(globals->formats[index].code);
    }
    free(globals->sheets);
    free(globals->shared_strings);
    free(globals->formats);
    free(globals->xf_format_ids);
    memset(globals, 0, sizeof(*globals));
}

static const char *path_filename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        slash = backslash;
    }
    return slash == NULL ? path : slash + 1;
}

int xls_parse_biff8_file(
    const char *path,
    xls_workbook *workbook,
    xls_error *error
)
{
    unsigned char *stream = NULL;
    size_t stream_size = 0;
    biff_globals globals;
    size_t index;
    int success = 0;
    memset(&globals, 0, sizeof(globals));
    if (!xls_compound_read_workbook_stream(
        path, &stream, &stream_size, error
    ) || !parse_globals(stream, stream_size, &globals, error)) {
        goto cleanup;
    }
    if (globals.sheet_count == 0) {
        xls_set_error(error, XLS_ERROR_FORMAT, "无法从 .xls 文件中解析工作表");
        goto cleanup;
    }
    workbook->filename = xls_strdup(path_filename(path));
    workbook->filepath = xls_strdup(path);
    workbook->sheets = (xls_sheet *)calloc(
        globals.sheet_count, sizeof(*workbook->sheets)
    );
    if (workbook->filename == NULL || workbook->filepath == NULL
        || workbook->sheets == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "创建 BIFF8 工作簿时内存不足");
        goto cleanup;
    }
    for (index = 0; index < globals.sheet_count; ++index) {
        if (globals.sheets[index].offset >= stream_size) {
            continue;
        }
        if (!parse_biff_sheet(
            stream,
            stream_size,
            &globals.sheets[index],
            &globals,
            &workbook->sheets[workbook->sheet_count],
            error
        )) {
            goto cleanup;
        }
        ++workbook->sheet_count;
    }
    if (workbook->sheet_count == 0) {
        xls_set_error(error, XLS_ERROR_FORMAT, "无法从 .xls 文件中解析工作表");
        goto cleanup;
    }
    success = 1;
    xls_error_clear(error);

cleanup:
    globals_free(&globals);
    free(stream);
    if (!success) {
        xls_workbook_free(workbook);
    }
    return success;
}
