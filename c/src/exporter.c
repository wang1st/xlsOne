#include "internal.h"
#include "zip_archive.h"

#include <ctype.h>
#include <expat.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct byte_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

typedef struct sheet_reference {
    char *name;
    char *relationship_id;
    char *target;
} sheet_reference;

typedef struct sheet_reference_list {
    sheet_reference *items;
    size_t count;
    xls_error *error;
    int failed;
} sheet_reference_list;

typedef struct central_record {
    char *name;
    uint32_t crc;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
    uint16_t method;
} central_record;

static int buffer_reserve(
    byte_buffer *buffer,
    size_t additional,
    xls_error *error
)
{
    size_t required;
    size_t capacity;
    unsigned char *replacement;
    if (additional > SIZE_MAX - buffer->size) {
        xls_set_error(error, XLS_ERROR_MEMORY, "导出数据过大");
        return 0;
    }
    required = buffer->size + additional;
    if (required <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    replacement = (unsigned char *)realloc(buffer->data, capacity);
    if (replacement == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "导出时内存不足");
        return 0;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_append(
    byte_buffer *buffer,
    const void *data,
    size_t size,
    xls_error *error
)
{
    if (!buffer_reserve(buffer, size, error)) {
        return 0;
    }
    if (size > 0) {
        memcpy(buffer->data + buffer->size, data, size);
        buffer->size += size;
    }
    return 1;
}

static int buffer_append_string(
    byte_buffer *buffer,
    const char *text,
    xls_error *error
)
{
    return buffer_append(buffer, text, strlen(text), error);
}

static int buffer_append_u16(
    byte_buffer *buffer,
    uint16_t value,
    xls_error *error
)
{
    unsigned char bytes[2];
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
    return buffer_append(buffer, bytes, sizeof(bytes), error);
}

static int buffer_append_u32(
    byte_buffer *buffer,
    uint32_t value,
    xls_error *error
)
{
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8u) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16u) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24u) & 0xffu);
    return buffer_append(buffer, bytes, sizeof(bytes), error);
}

static void buffer_free(byte_buffer *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static const char *xml_local_name(const char *name)
{
    const char *separator = strrchr(name, '|');
    return separator == NULL ? name : separator + 1;
}

static const char *xml_attribute(const char **attributes, const char *name)
{
    size_t index;
    for (index = 0; attributes != NULL && attributes[index] != NULL; index += 2) {
        if (strcmp(xml_local_name(attributes[index]), name) == 0) {
            return attributes[index + 1];
        }
    }
    return NULL;
}

static void XMLCALL workbook_sheet_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    sheet_reference_list *list = (sheet_reference_list *)user_data;
    sheet_reference *replacement;
    sheet_reference *item;
    const char *name;
    const char *relationship_id;
    if (list->failed || strcmp(xml_local_name(element), "sheet") != 0) {
        return;
    }
    name = xml_attribute(attributes, "name");
    relationship_id = xml_attribute(attributes, "id");
    if (name == NULL || relationship_id == NULL) {
        return;
    }
    replacement = (sheet_reference *)realloc(
        list->items, (list->count + 1) * sizeof(*list->items)
    );
    if (replacement == NULL) {
        xls_set_error(list->error, XLS_ERROR_MEMORY, "解析导出模板时内存不足");
        list->failed = 1;
        return;
    }
    list->items = replacement;
    item = &list->items[list->count++];
    memset(item, 0, sizeof(*item));
    item->name = xls_strdup(name);
    item->relationship_id = xls_strdup(relationship_id);
    if (item->name == NULL || item->relationship_id == NULL) {
        xls_set_error(list->error, XLS_ERROR_MEMORY, "复制模板工作表信息时内存不足");
        list->failed = 1;
    }
}

static char *normalize_target(const char *target)
{
    const char *body = target;
    size_t length;
    char *result;
    while (body != NULL && *body == '/') {
        ++body;
    }
    if (body == NULL) {
        return NULL;
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

static void XMLCALL workbook_relationship_start(
    void *user_data,
    const char *element,
    const char **attributes
)
{
    sheet_reference_list *list = (sheet_reference_list *)user_data;
    const char *id;
    const char *target;
    size_t index;
    if (list->failed || strcmp(xml_local_name(element), "Relationship") != 0) {
        return;
    }
    id = xml_attribute(attributes, "Id");
    target = xml_attribute(attributes, "Target");
    if (id == NULL || target == NULL) {
        return;
    }
    for (index = 0; index < list->count; ++index) {
        if (strcmp(list->items[index].relationship_id, id) == 0) {
            list->items[index].target = normalize_target(target);
            if (list->items[index].target == NULL) {
                xls_set_error(list->error, XLS_ERROR_MEMORY, "复制模板关系时内存不足");
                list->failed = 1;
            }
            break;
        }
    }
}

static int parse_xml_entry(
    const xls_zip_archive *archive,
    const char *entry_name,
    XML_StartElementHandler start,
    void *context,
    xls_error *error
)
{
    unsigned char *data = NULL;
    size_t size = 0;
    XML_Parser parser;
    enum XML_Status status;
    if (!xls_zip_read(archive, entry_name, &data, &size, error)) {
        return 0;
    }
    if (size > (size_t)INT_MAX) {
        free(data);
        xls_set_error(error, XLS_ERROR_FORMAT, "模板 XML 过大");
        return 0;
    }
    parser = XML_ParserCreateNS(NULL, '|');
    if (parser == NULL) {
        free(data);
        xls_set_error(error, XLS_ERROR_MEMORY, "创建 XML 解析器失败");
        return 0;
    }
    XML_SetUserData(parser, context);
    XML_SetStartElementHandler(parser, start);
    status = XML_Parse(parser, (const char *)data, (int)size, XML_TRUE);
    if (status != XML_STATUS_OK) {
        xls_set_error(
            error,
            XLS_ERROR_FORMAT,
            "导出模板 XML 解析失败: %s",
            XML_ErrorString(XML_GetErrorCode(parser))
        );
        XML_ParserFree(parser);
        free(data);
        return 0;
    }
    XML_ParserFree(parser);
    free(data);
    return 1;
}

static void sheet_references_free(sheet_reference_list *list)
{
    size_t index;
    for (index = 0; index < list->count; ++index) {
        free(list->items[index].name);
        free(list->items[index].relationship_id);
        free(list->items[index].target);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int parse_cell_coordinate(
    const char *reference,
    size_t *row,
    size_t *column
)
{
    size_t index = 0;
    size_t column_value = 0;
    unsigned long row_value;
    char *end;
    while (reference[index] != '\0'
        && isalpha((unsigned char)reference[index])) {
        column_value = column_value * 26u
            + (size_t)(toupper((unsigned char)reference[index]) - 'A' + 1);
        ++index;
    }
    if (index == 0 || column_value == 0
        || !isdigit((unsigned char)reference[index])) {
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

static char *tag_attribute_copy(
    const char *tag,
    size_t tag_length,
    const char *attribute
)
{
    size_t attribute_length = strlen(attribute);
    size_t index;
    for (index = 0; index + attribute_length + 2 < tag_length; ++index) {
        size_t value_start;
        size_t value_end;
        if (index > 0
            && !isspace((unsigned char)tag[index - 1])
            && tag[index - 1] != '<') {
            continue;
        }
        if (memcmp(tag + index, attribute, attribute_length) != 0
            || tag[index + attribute_length] != '=') {
            continue;
        }
        value_start = index + attribute_length + 1;
        if (tag[value_start] != '"' && tag[value_start] != '\'') {
            continue;
        }
        ++value_start;
        value_end = value_start;
        while (value_end < tag_length
            && tag[value_end] != tag[value_start - 1]) {
            ++value_end;
        }
        if (value_end < tag_length) {
            return xls_strndup(tag + value_start, value_end - value_start);
        }
    }
    return NULL;
}

static int append_xml_escaped(
    byte_buffer *output,
    const char *text,
    xls_error *error
)
{
    const char *cursor = text == NULL ? "" : text;
    const char *segment = cursor;
    while (*cursor != '\0') {
        const char *replacement = NULL;
        if (*cursor == '&') {
            replacement = "&amp;";
        } else if (*cursor == '<') {
            replacement = "&lt;";
        } else if (*cursor == '>') {
            replacement = "&gt;";
        } else if (*cursor == '"') {
            replacement = "&quot;";
        }
        if (replacement != NULL) {
            if (!buffer_append(
                output, segment, (size_t)(cursor - segment), error
            ) || !buffer_append_string(output, replacement, error)) {
                return 0;
            }
            segment = cursor + 1;
        }
        ++cursor;
    }
    return buffer_append(output, segment, (size_t)(cursor - segment), error);
}

static int append_rewritten_cell(
    byte_buffer *output,
    const char *qualified_cell_name,
    size_t qualified_cell_name_length,
    const char *reference,
    const char *style,
    const xls_merged_cell *cell,
    xls_error *error
)
{
    char numeric[128];
    const char *colon = memchr(
        qualified_cell_name, ':', qualified_cell_name_length
    );
    size_t prefix_length = colon == NULL
        ? 0
        : (size_t)(colon - qualified_cell_name + 1);
    if (!buffer_append_string(output, "<", error)
        || !buffer_append(
            output,
            qualified_cell_name,
            qualified_cell_name_length,
            error
        )
        || !buffer_append_string(output, " r=\"", error)
        || !append_xml_escaped(output, reference, error)
        || !buffer_append_string(output, "\"", error)) {
        return 0;
    }
    if (style != NULL && style[0] != '\0') {
        if (!buffer_append_string(output, " s=\"", error)
            || !append_xml_escaped(output, style, error)
            || !buffer_append_string(output, "\"", error)) {
            return 0;
        }
    }
    if (cell->kind == XLS_CELL_SUM) {
        (void)snprintf(numeric, sizeof(numeric), "%.15g", cell->sum);
        return buffer_append_string(output, "><", error)
            && buffer_append(
                output, qualified_cell_name, prefix_length, error
            )
            && buffer_append_string(output, "v>", error)
            && buffer_append_string(output, numeric, error)
            && buffer_append_string(output, "</", error)
            && buffer_append(
                output, qualified_cell_name, prefix_length, error
            )
            && buffer_append_string(output, "v></", error)
            && buffer_append(
                output,
                qualified_cell_name,
                qualified_cell_name_length,
                error
            )
            && buffer_append_string(output, ">", error);
    }
    if (xls_string_empty(cell->display_value)) {
        return buffer_append_string(output, "/>", error);
    }
    return buffer_append_string(output, " t=\"inlineStr\"><", error)
        && buffer_append(output, qualified_cell_name, prefix_length, error)
        && buffer_append_string(output, "is><", error)
        && buffer_append(output, qualified_cell_name, prefix_length, error)
        && buffer_append_string(output, "t>", error)
        && append_xml_escaped(output, cell->display_value, error)
        && buffer_append_string(output, "</", error)
        && buffer_append(output, qualified_cell_name, prefix_length, error)
        && buffer_append_string(output, "t></", error)
        && buffer_append(output, qualified_cell_name, prefix_length, error)
        && buffer_append_string(output, "is></", error)
        && buffer_append(
            output,
            qualified_cell_name,
            qualified_cell_name_length,
            error
        )
        && buffer_append_string(output, ">", error);
}

static const char *find_cell_tag(const char *cursor)
{
    for (;;) {
        const char *name;
        const char *name_end;
        const char *local;
        cursor = strchr(cursor, '<');
        if (cursor == NULL) {
            return NULL;
        }
        name = cursor + 1;
        if (*name == '/' || *name == '!' || *name == '?') {
            ++cursor;
            continue;
        }
        name_end = name;
        while (*name_end != '\0'
            && *name_end != '>'
            && *name_end != '/'
            && !isspace((unsigned char)*name_end)) {
            ++name_end;
        }
        local = name_end;
        while (local > name && local[-1] != ':') {
            --local;
        }
        if ((size_t)(name_end - local) == 1 && local[0] == 'c') {
            return cursor;
        }
        ++cursor;
    }
}

static const char *find_cell_end(const char *tag_end)
{
    const char *cursor = tag_end + 1;
    if (tag_end[-1] == '/') {
        return tag_end + 1;
    }
    for (;;) {
        const char *name;
        const char *name_end;
        const char *local;
        cursor = strstr(cursor, "</");
        if (cursor == NULL) {
            return NULL;
        }
        name = cursor + 2;
        name_end = name;
        while (*name_end != '\0'
            && *name_end != '>'
            && !isspace((unsigned char)*name_end)) {
            ++name_end;
        }
        local = name_end;
        while (local > name && local[-1] != ':') {
            --local;
        }
        if ((size_t)(name_end - local) == 1 && local[0] == 'c') {
            const char *closing_end = strchr(name_end, '>');
            return closing_end == NULL ? NULL : closing_end + 1;
        }
        cursor += 2;
    }
}

static const char *find_closing_local_tag(
    const char *cursor,
    const char *local_name,
    size_t *prefix_length
)
{
    const size_t local_length = strlen(local_name);
    for (;;) {
        const char *name;
        const char *name_end;
        const char *local;
        cursor = strstr(cursor, "</");
        if (cursor == NULL) {
            return NULL;
        }
        name = cursor + 2;
        name_end = name;
        while (*name_end != '\0'
            && *name_end != '>'
            && !isspace((unsigned char)*name_end)) {
            ++name_end;
        }
        local = name_end;
        while (local > name && local[-1] != ':') {
            --local;
        }
        if ((size_t)(name_end - local) == local_length
            && memcmp(local, local_name, local_length) == 0) {
            *prefix_length = local == name
                ? 0
                : (size_t)(local - name);
            return cursor;
        }
        cursor += 2;
    }
}

static int rewrite_worksheet(
    const unsigned char *xml,
    size_t xml_size,
    const xls_merged_sheet *sheet,
    const char *watermark,
    unsigned char **rewritten,
    size_t *rewritten_size,
    xls_error *error
)
{
    char *source;
    const char *cursor;
    byte_buffer output;
    memset(&output, 0, sizeof(output));
    source = xls_strndup((const char *)xml, xml_size);
    if (source == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "复制工作表 XML 时内存不足");
        return 0;
    }
    cursor = source;
    for (;;) {
        const char *tag = find_cell_tag(cursor);
        const char *tag_end;
        const char *cell_end;
        const char *qualified_name;
        const char *qualified_name_end;
        char *reference;
        char *style;
        size_t row;
        size_t column;
        const xls_merged_cell *cell;
        if (tag == NULL) {
            break;
        }
        tag_end = strchr(tag, '>');
        if (tag_end == NULL) {
            break;
        }
        qualified_name = tag + 1;
        qualified_name_end = qualified_name;
        while (qualified_name_end < tag_end
            && !isspace((unsigned char)*qualified_name_end)
            && *qualified_name_end != '>'
            && *qualified_name_end != '/') {
            ++qualified_name_end;
        }
        cell_end = find_cell_end(tag_end);
        if (cell_end == NULL) {
            break;
        }
        reference = tag_attribute_copy(tag, (size_t)(tag_end - tag + 1), "r");
        style = tag_attribute_copy(tag, (size_t)(tag_end - tag + 1), "s");
        cell = NULL;
        if (reference != NULL
            && parse_cell_coordinate(reference, &row, &column)) {
            cell = xls_merged_sheet_cell(sheet, row, column);
        }
        if (!buffer_append(
            &output, cursor, (size_t)(tag - cursor), error
        )) {
            free(reference);
            free(style);
            goto failure;
        }
        if (cell != NULL) {
            if (!append_rewritten_cell(
                &output,
                qualified_name,
                (size_t)(qualified_name_end - qualified_name),
                reference,
                style,
                cell,
                error
            )) {
                free(reference);
                free(style);
                goto failure;
            }
        } else if (!buffer_append(
            &output, tag, (size_t)(cell_end - tag), error
        )) {
            free(reference);
            free(style);
            goto failure;
        }
        free(reference);
        free(style);
        cursor = cell_end;
    }
    if (!xls_string_empty(watermark)) {
        size_t prefix_length = 0;
        const char *sheet_data_end = find_closing_local_tag(
            cursor,
            "sheetData",
            &prefix_length
        );
        if (sheet_data_end != NULL) {
            char row_number[64];
            const char *qualified_name = sheet_data_end + 2;
            size_t watermark_row = sheet->row_count + 1;
            (void)snprintf(
                row_number, sizeof(row_number), "%zu", watermark_row
            );
            if (!buffer_append(
                &output,
                cursor,
                (size_t)(sheet_data_end - cursor),
                error
            ) || !buffer_append_string(&output, "<", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "row r=\"", error)
                || !buffer_append_string(&output, row_number, error)
                || !buffer_append_string(
                    &output, "\"><", error
                )
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(
                    &output, "c r=\"A", error
                )
                || !buffer_append_string(&output, row_number, error)
                || !buffer_append_string(
                    &output, "\" t=\"inlineStr\"><", error
                )
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "is><", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "t>", error)
                || !append_xml_escaped(&output, watermark, error)
                || !buffer_append_string(&output, "</", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "t></", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "is></", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "c></", error)
                || !buffer_append(
                    &output, qualified_name, prefix_length, error
                )
                || !buffer_append_string(&output, "row>", error)) {
                goto failure;
            }
            cursor = sheet_data_end;
        }
    }
    if (!buffer_append_string(&output, cursor, error)
        || !buffer_append(&output, "", 1, error)) {
        goto failure;
    }
    --output.size;
    *rewritten = output.data;
    *rewritten_size = output.size;
    free(source);
    return 1;

failure:
    free(source);
    buffer_free(&output);
    return 0;
}

static int deflate_raw(
    const unsigned char *data,
    size_t size,
    unsigned char **compressed,
    size_t *compressed_size,
    xls_error *error
)
{
    z_stream stream;
    uLong bound;
    int result;
    if (size > UINT32_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目过大");
        return 0;
    }
    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(
        &stream,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY
    ) != Z_OK) {
        xls_set_error(error, XLS_ERROR_INTERNAL, "无法初始化 ZIP 压缩器");
        return 0;
    }
    bound = deflateBound(&stream, (uLong)size);
    *compressed = (unsigned char *)malloc((size_t)bound);
    if (*compressed == NULL) {
        deflateEnd(&stream);
        xls_set_error(error, XLS_ERROR_MEMORY, "压缩 ZIP 条目时内存不足");
        return 0;
    }
    stream.next_in = (Bytef *)data;
    stream.avail_in = (uInt)size;
    stream.next_out = *compressed;
    stream.avail_out = (uInt)bound;
    result = deflate(&stream, Z_FINISH);
    if (result != Z_STREAM_END) {
        free(*compressed);
        *compressed = NULL;
        deflateEnd(&stream);
        xls_set_error(error, XLS_ERROR_INTERNAL, "ZIP 压缩失败");
        return 0;
    }
    *compressed_size = (size_t)stream.total_out;
    deflateEnd(&stream);
    return 1;
}

static int write_zip(
    const char *output_path,
    char *const *names,
    unsigned char *const *entry_data,
    const size_t *entry_sizes,
    size_t entry_count,
    xls_error *error
)
{
    byte_buffer file_data;
    central_record *records;
    size_t record_count = 0;
    size_t index;
    uint32_t central_offset;
    uint32_t central_size;
    FILE *file;
    memset(&file_data, 0, sizeof(file_data));
    if (entry_count > UINT16_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目过多");
        return 0;
    }
    records = entry_count == 0
        ? NULL
        : (central_record *)calloc(entry_count, sizeof(*records));
    if (entry_count > 0 && records == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "创建 ZIP 目录时内存不足");
        return 0;
    }
    for (index = 0; index < entry_count; ++index) {
        unsigned char *compressed = NULL;
        size_t compressed_size = 0;
        size_t name_length = strlen(names[index]);
        central_record *record;
        if (name_length == 0 || names[index][name_length - 1] == '/') {
            continue;
        }
        if (name_length > UINT16_MAX
            || entry_sizes[index] > UINT32_MAX
            || file_data.size > UINT32_MAX
            || !deflate_raw(
                entry_data[index],
                entry_sizes[index],
                &compressed,
                &compressed_size,
                error
            )
            || compressed_size > UINT32_MAX) {
            free(compressed);
            goto failure;
        }
        record = &records[record_count++];
        record->name = names[index];
        record->crc = (uint32_t)crc32(
            0L, entry_data[index], (uInt)entry_sizes[index]
        );
        record->compressed_size = (uint32_t)compressed_size;
        record->uncompressed_size = (uint32_t)entry_sizes[index];
        record->local_header_offset = (uint32_t)file_data.size;
        record->method = 8;
        if (!buffer_append_u32(&file_data, UINT32_C(0x04034b50), error)
            || !buffer_append_u16(&file_data, 20, error)
            || !buffer_append_u16(&file_data, UINT16_C(1) << 11u, error)
            || !buffer_append_u16(&file_data, record->method, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u32(&file_data, record->crc, error)
            || !buffer_append_u32(&file_data, record->compressed_size, error)
            || !buffer_append_u32(&file_data, record->uncompressed_size, error)
            || !buffer_append_u16(&file_data, (uint16_t)name_length, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append(&file_data, names[index], name_length, error)
            || !buffer_append(
                &file_data, compressed, compressed_size, error
            )) {
            free(compressed);
            goto failure;
        }
        free(compressed);
    }
    if (file_data.size > UINT32_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 文件过大");
        goto failure;
    }
    central_offset = (uint32_t)file_data.size;
    for (index = 0; index < record_count; ++index) {
        central_record *record = &records[index];
        size_t name_length = strlen(record->name);
        if (!buffer_append_u32(&file_data, UINT32_C(0x02014b50), error)
            || !buffer_append_u16(&file_data, 20, error)
            || !buffer_append_u16(&file_data, 20, error)
            || !buffer_append_u16(&file_data, UINT16_C(1) << 11u, error)
            || !buffer_append_u16(&file_data, record->method, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u32(&file_data, record->crc, error)
            || !buffer_append_u32(&file_data, record->compressed_size, error)
            || !buffer_append_u32(&file_data, record->uncompressed_size, error)
            || !buffer_append_u16(&file_data, (uint16_t)name_length, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u16(&file_data, 0, error)
            || !buffer_append_u32(&file_data, 0, error)
            || !buffer_append_u32(
                &file_data, record->local_header_offset, error
            )
            || !buffer_append(
                &file_data, record->name, name_length, error
            )) {
            goto failure;
        }
    }
    if (file_data.size > UINT32_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 文件过大");
        goto failure;
    }
    central_size = (uint32_t)file_data.size - central_offset;
    if (!buffer_append_u32(&file_data, UINT32_C(0x06054b50), error)
        || !buffer_append_u16(&file_data, 0, error)
        || !buffer_append_u16(&file_data, 0, error)
        || !buffer_append_u16(&file_data, (uint16_t)record_count, error)
        || !buffer_append_u16(&file_data, (uint16_t)record_count, error)
        || !buffer_append_u32(&file_data, central_size, error)
        || !buffer_append_u32(&file_data, central_offset, error)
        || !buffer_append_u16(&file_data, 0, error)) {
        goto failure;
    }
    file = xls_fopen_utf8(output_path, "wb");
    if (file == NULL) {
        xls_set_error(error, XLS_ERROR_IO, "无法写入导出文件: %s", output_path);
        goto failure;
    }
    if (fwrite(file_data.data, 1, file_data.size, file) != file_data.size
        || fclose(file) != 0) {
        xls_set_error(error, XLS_ERROR_IO, "写入导出文件失败: %s", output_path);
        free(records);
        buffer_free(&file_data);
        return 0;
    }
    free(records);
    buffer_free(&file_data);
    return 1;

failure:
    free(records);
    buffer_free(&file_data);
    return 0;
}

static const xls_merged_sheet *find_merged_sheet(
    const xls_merged_sheet *sheets,
    size_t sheet_count,
    const char *name
)
{
    size_t index;
    for (index = 0; index < sheet_count; ++index) {
        if (strcmp(sheets[index].sheet_name, name) == 0) {
            return &sheets[index];
        }
    }
    return NULL;
}

int xls_export_csv(
    const xls_merged_sheet *sheet,
    const char *output_path,
    const char *watermark,
    xls_error *error
)
{
    FILE *file;
    size_t row;
    size_t column;
    if (sheet == NULL || output_path == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid CSV export arguments");
        return 0;
    }
    file = xls_fopen_utf8(output_path, "wb");
    if (file == NULL) {
        xls_set_error(error, XLS_ERROR_IO, "无法写入导出文件: %s", output_path);
        return 0;
    }
    if (!xls_string_empty(watermark)) {
        (void)fprintf(file, "# %s\n", watermark);
    }
    for (row = 0; row < sheet->row_count; ++row) {
        for (column = 0; column < sheet->column_count; ++column) {
            const xls_merged_cell *cell
                = xls_merged_sheet_cell(sheet, row, column);
            const char *cursor = cell == NULL || cell->display_value == NULL
                ? ""
                : cell->display_value;
            if (column > 0) {
                (void)fputc(',', file);
            }
            (void)fputc('"', file);
            while (*cursor != '\0') {
                if (*cursor == '"') {
                    (void)fputc('"', file);
                }
                (void)fputc(*cursor++, file);
            }
            (void)fputc('"', file);
        }
        (void)fputc('\n', file);
    }
    if (fclose(file) != 0) {
        xls_set_error(error, XLS_ERROR_IO, "写入 CSV 失败: %s", output_path);
        return 0;
    }
    xls_error_clear(error);
    return 1;
}

int xls_export_xlsx(
    const char *template_path,
    const xls_merged_sheet *sheets,
    size_t sheet_count,
    const char *output_path,
    const char *watermark,
    xls_error *error
)
{
    xls_zip_archive archive;
    sheet_reference_list references;
    char **names = NULL;
    unsigned char **entries = NULL;
    size_t *sizes = NULL;
    size_t index;
    int success = 0;
    if (template_path == NULL || output_path == NULL
        || (sheets == NULL && sheet_count > 0)) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid XLSX export arguments");
        return 0;
    }
    memset(&archive, 0, sizeof(archive));
    memset(&references, 0, sizeof(references));
    references.error = error;
    if (!xls_zip_open(template_path, &archive, error)
        || !parse_xml_entry(
            &archive,
            "xl/workbook.xml",
            workbook_sheet_start,
            &references,
            error
        )
        || references.failed
        || !parse_xml_entry(
            &archive,
            "xl/_rels/workbook.xml.rels",
            workbook_relationship_start,
            &references,
            error
        )
        || references.failed) {
        goto cleanup;
    }
    names = (char **)calloc(archive.entry_count, sizeof(*names));
    entries = (unsigned char **)calloc(archive.entry_count, sizeof(*entries));
    sizes = (size_t *)calloc(archive.entry_count, sizeof(*sizes));
    if (archive.entry_count > 0
        && (names == NULL || entries == NULL || sizes == NULL)) {
        xls_set_error(error, XLS_ERROR_MEMORY, "准备导出工作簿时内存不足");
        goto cleanup;
    }
    for (index = 0; index < archive.entry_count; ++index) {
        const xls_merged_sheet *merged = NULL;
        size_t reference_index;
        names[index] = xls_strdup(archive.entries[index].name);
        if (names[index] == NULL
            || !xls_zip_read(
                &archive,
                archive.entries[index].name,
                &entries[index],
                &sizes[index],
                error
            )) {
            goto cleanup;
        }
        for (reference_index = 0;
             reference_index < references.count;
             ++reference_index) {
            if (references.items[reference_index].target != NULL
                && strcmp(
                    references.items[reference_index].target,
                    archive.entries[index].name
                ) == 0) {
                merged = find_merged_sheet(
                    sheets,
                    sheet_count,
                    references.items[reference_index].name
                );
                break;
            }
        }
        if (merged != NULL) {
            unsigned char *rewritten = NULL;
            size_t rewritten_size = 0;
            if (!rewrite_worksheet(
                entries[index],
                sizes[index],
                merged,
                watermark,
                &rewritten,
                &rewritten_size,
                error
            )) {
                goto cleanup;
            }
            free(entries[index]);
            entries[index] = rewritten;
            sizes[index] = rewritten_size;
        }
    }
    if (!write_zip(
        output_path,
        names,
        entries,
        sizes,
        archive.entry_count,
        error
    )) {
        goto cleanup;
    }
    success = 1;
    xls_error_clear(error);

cleanup:
    if (entries != NULL) {
        for (index = 0; index < archive.entry_count; ++index) {
            free(entries[index]);
        }
    }
    if (names != NULL) {
        for (index = 0; index < archive.entry_count; ++index) {
            free(names[index]);
        }
    }
    free(names);
    free(entries);
    free(sizes);
    sheet_references_free(&references);
    xls_zip_close(&archive);
    return success;
}
