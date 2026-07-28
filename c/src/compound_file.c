#include "compound_file.h"
#include "internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OLE_END_OF_CHAIN UINT32_C(0xfffffffe)
#define OLE_FREE_SECTOR UINT32_C(0xffffffff)
#define OLE_DIFAT_SECTOR UINT32_C(0xfffffffc)
#define OLE_FAT_SECTOR UINT32_C(0xfffffffd)

typedef struct ole_buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
} ole_buffer;

typedef struct ole_directory_entry {
    char name[65];
    unsigned char type;
    int starting_sector;
    uint64_t stream_size;
} ole_directory_entry;

static uint16_t ole_u16(const unsigned char *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t ole_u32(const unsigned char *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8u)
        | ((uint32_t)data[2] << 16u)
        | ((uint32_t)data[3] << 24u);
}

static uint64_t ole_u64(const unsigned char *data)
{
    return (uint64_t)ole_u32(data)
        | ((uint64_t)ole_u32(data + 4) << 32u);
}

static int ole_buffer_append(
    ole_buffer *buffer,
    const unsigned char *data,
    size_t size,
    xls_error *error
)
{
    size_t required;
    size_t capacity;
    unsigned char *replacement;
    if (size > SIZE_MAX - buffer->size) {
        xls_set_error(error, XLS_ERROR_MEMORY, "OLE 数据过大");
        return 0;
    }
    required = buffer->size + size;
    if (required > buffer->capacity) {
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
            xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE 链时内存不足");
            return 0;
        }
        buffer->data = replacement;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 1;
}

static int read_file(
    const char *path,
    unsigned char **data,
    size_t *size,
    xls_error *error
)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (file == NULL) {
        xls_set_error(error, XLS_ERROR_IO, "无法打开 .xls 文件: %s", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0
        || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_IO, "无法读取 .xls 文件大小");
        return 0;
    }
    if ((uintmax_t)length > (uintmax_t)SIZE_MAX) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_FORMAT, ".xls 文件过大");
        return 0;
    }
    *data = length == 0 ? NULL : (unsigned char *)malloc((size_t)length);
    if (length > 0 && *data == NULL) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_MEMORY, "读取 .xls 时内存不足");
        return 0;
    }
    if (length > 0
        && fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data);
        *data = NULL;
        fclose(file);
        xls_set_error(error, XLS_ERROR_IO, ".xls 文件读取不完整");
        return 0;
    }
    fclose(file);
    *size = (size_t)length;
    return 1;
}

static int sector_slice(
    const unsigned char *data,
    size_t data_size,
    size_t sector_size,
    uint32_t sector_id,
    const unsigned char **sector,
    xls_error *error
)
{
    size_t offset;
    if ((size_t)sector_id > (SIZE_MAX / sector_size) - 1) {
        xls_set_error(error, XLS_ERROR_FORMAT, "OLE sector 编号过大");
        return 0;
    }
    offset = ((size_t)sector_id + 1) * sector_size;
    if (offset > data_size || sector_size > data_size - offset) {
        xls_set_error(error, XLS_ERROR_FORMAT, "OLE sector 越界");
        return 0;
    }
    *sector = data + offset;
    return 1;
}

static int read_regular_chain(
    const unsigned char *data,
    size_t data_size,
    size_t sector_size,
    const uint32_t *fat,
    size_t fat_count,
    int start_sector,
    size_t sector_limit,
    uint64_t byte_limit,
    unsigned char **result,
    size_t *result_size,
    xls_error *error
)
{
    unsigned char *seen;
    ole_buffer output;
    uint32_t current;
    size_t sectors = 0;
    memset(&output, 0, sizeof(output));
    *result = NULL;
    *result_size = 0;
    if (start_sector < 0) {
        return 1;
    }
    seen = fat_count == 0
        ? NULL
        : (unsigned char *)calloc(fat_count, sizeof(*seen));
    if (fat_count > 0 && seen == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE 链时内存不足");
        return 0;
    }
    current = (uint32_t)start_sector;
    while (current < OLE_DIFAT_SECTOR) {
        const unsigned char *sector;
        if (current >= fat_count || seen[current]) {
            free(seen);
            free(output.data);
            xls_set_error(error, XLS_ERROR_FORMAT, "OLE sector 链损坏或出现循环");
            return 0;
        }
        seen[current] = 1;
        if (!sector_slice(
            data, data_size, sector_size, current, &sector, error
        ) || !ole_buffer_append(&output, sector, sector_size, error)) {
            free(seen);
            free(output.data);
            return 0;
        }
        ++sectors;
        if (sector_limit > 0 && sectors >= sector_limit) {
            break;
        }
        current = fat[current];
        if (current == OLE_END_OF_CHAIN
            || current == OLE_FREE_SECTOR
            || current == OLE_FAT_SECTOR
            || current == OLE_DIFAT_SECTOR) {
            break;
        }
    }
    free(seen);
    if (byte_limit < output.size) {
        output.size = (size_t)byte_limit;
    }
    *result = output.data;
    *result_size = output.size;
    return 1;
}

static int read_mini_chain(
    const unsigned char *mini_stream,
    size_t mini_stream_size,
    size_t mini_sector_size,
    const uint32_t *mini_fat,
    size_t mini_fat_count,
    int start_sector,
    uint64_t byte_limit,
    unsigned char **result,
    size_t *result_size,
    xls_error *error
)
{
    unsigned char *seen;
    ole_buffer output;
    uint32_t current;
    memset(&output, 0, sizeof(output));
    *result = NULL;
    *result_size = 0;
    if (start_sector < 0) {
        return 1;
    }
    seen = mini_fat_count == 0
        ? NULL
        : (unsigned char *)calloc(mini_fat_count, sizeof(*seen));
    if (mini_fat_count > 0 && seen == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE mini 链时内存不足");
        return 0;
    }
    current = (uint32_t)start_sector;
    while (current < OLE_END_OF_CHAIN) {
        size_t offset;
        size_t available;
        if (current >= mini_fat_count || seen[current]) {
            free(seen);
            free(output.data);
            xls_set_error(error, XLS_ERROR_FORMAT, "OLE mini sector 链损坏");
            return 0;
        }
        seen[current] = 1;
        if ((size_t)current > SIZE_MAX / mini_sector_size) {
            free(seen);
            free(output.data);
            xls_set_error(error, XLS_ERROR_FORMAT, "OLE mini sector 越界");
            return 0;
        }
        offset = (size_t)current * mini_sector_size;
        if (offset >= mini_stream_size) {
            free(seen);
            free(output.data);
            xls_set_error(error, XLS_ERROR_FORMAT, "OLE mini sector 越界");
            return 0;
        }
        available = mini_stream_size - offset;
        if (available > mini_sector_size) {
            available = mini_sector_size;
        }
        if (!ole_buffer_append(
            &output, mini_stream + offset, available, error
        )) {
            free(seen);
            free(output.data);
            return 0;
        }
        current = mini_fat[current];
        if (current == OLE_END_OF_CHAIN || current == OLE_FREE_SECTOR) {
            break;
        }
    }
    free(seen);
    if (byte_limit < output.size) {
        output.size = (size_t)byte_limit;
    }
    *result = output.data;
    *result_size = output.size;
    return 1;
}

static void decode_directory_name(
    const unsigned char *entry,
    char output[65]
)
{
    uint16_t byte_length = ole_u16(entry + 64);
    size_t characters;
    size_t index;
    if (byte_length < 2 || byte_length > 64) {
        output[0] = '\0';
        return;
    }
    characters = (size_t)(byte_length / 2u) - 1;
    if (characters > 64) {
        characters = 64;
    }
    for (index = 0; index < characters; ++index) {
        uint16_t character = ole_u16(entry + index * 2);
        output[index] = character <= 0x7f ? (char)character : '?';
    }
    output[characters] = '\0';
}

static int ascii_equal_ci(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        char lhs = *left >= 'A' && *left <= 'Z' ? (char)(*left + 32) : *left;
        char rhs = *right >= 'A' && *right <= 'Z' ? (char)(*right + 32) : *right;
        if (lhs != rhs) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

int xls_compound_read_workbook_stream(
    const char *path,
    unsigned char **stream,
    size_t *stream_size,
    xls_error *error
)
{
    static const unsigned char magic[8] = {
        0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1
    };
    unsigned char *data = NULL;
    size_t data_size = 0;
    size_t sector_size;
    size_t mini_sector_size;
    uint32_t fat_sector_count;
    int first_directory_sector;
    uint32_t mini_stream_cutoff;
    int first_mini_fat_sector;
    uint32_t mini_fat_sector_count;
    int first_difat_sector;
    uint32_t difat_sector_count;
    uint32_t *fat_sector_ids = NULL;
    size_t fat_id_count = 0;
    uint32_t *fat = NULL;
    size_t fat_count = 0;
    unsigned char *directory_data = NULL;
    size_t directory_size = 0;
    ole_directory_entry *entries = NULL;
    size_t entry_count = 0;
    unsigned char *mini_fat_data = NULL;
    size_t mini_fat_size = 0;
    uint32_t *mini_fat = NULL;
    size_t mini_fat_count = 0;
    unsigned char *mini_stream = NULL;
    size_t mini_stream_size = 0;
    size_t index;
    const ole_directory_entry *workbook_entry = NULL;
    const ole_directory_entry *root_entry = NULL;
    int success = 0;
    *stream = NULL;
    *stream_size = 0;
    if (!read_file(path, &data, &data_size, error)) {
        return 0;
    }
    if (data_size < 512 || memcmp(data, magic, sizeof(magic)) != 0) {
        xls_set_error(error, XLS_ERROR_FORMAT, "不是有效的 OLE Compound File");
        goto cleanup;
    }
    {
        uint16_t sector_shift = ole_u16(data + 30);
        uint16_t mini_shift = ole_u16(data + 32);
        if (sector_shift >= sizeof(size_t) * 8
            || mini_shift >= sizeof(size_t) * 8) {
            xls_set_error(error, XLS_ERROR_FORMAT, "OLE sector shift 无效");
            goto cleanup;
        }
        sector_size = (size_t)1u << sector_shift;
        mini_sector_size = (size_t)1u << mini_shift;
    }
    if (sector_size != 512 && sector_size != 4096) {
        xls_set_error(error, XLS_ERROR_UNSUPPORTED, "暂不支持该 OLE sector size");
        goto cleanup;
    }
    fat_sector_count = ole_u32(data + 44);
    first_directory_sector = ole_u32(data + 48) == OLE_END_OF_CHAIN
        ? -1
        : (int)ole_u32(data + 48);
    mini_stream_cutoff = ole_u32(data + 56);
    first_mini_fat_sector = ole_u32(data + 60) == OLE_END_OF_CHAIN
        ? -1
        : (int)ole_u32(data + 60);
    mini_fat_sector_count = ole_u32(data + 64);
    first_difat_sector = ole_u32(data + 68) == OLE_END_OF_CHAIN
        ? -1
        : (int)ole_u32(data + 68);
    difat_sector_count = ole_u32(data + 72);
    if (fat_sector_count > data_size / sector_size
        || difat_sector_count > data_size / sector_size) {
        xls_set_error(error, XLS_ERROR_FORMAT, "OLE FAT 计数无效");
        goto cleanup;
    }
    fat_sector_ids = fat_sector_count == 0
        ? NULL
        : (uint32_t *)malloc(fat_sector_count * sizeof(*fat_sector_ids));
    if (fat_sector_count > 0 && fat_sector_ids == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE DIFAT 时内存不足");
        goto cleanup;
    }
    for (index = 0; index < 109 && fat_id_count < fat_sector_count; ++index) {
        uint32_t value = ole_u32(data + 76 + index * 4);
        if (value != OLE_FREE_SECTOR) {
            fat_sector_ids[fat_id_count++] = value;
        }
    }
    {
        int current = first_difat_sector;
        uint32_t count;
        for (count = 0; count < difat_sector_count && current >= 0; ++count) {
            const unsigned char *sector;
            size_t entries_per_sector = sector_size / 4 - 1;
            if (!sector_slice(
                data, data_size, sector_size, (uint32_t)current, &sector, error
            )) {
                goto cleanup;
            }
            for (index = 0;
                 index < entries_per_sector && fat_id_count < fat_sector_count;
                 ++index) {
                uint32_t value = ole_u32(sector + index * 4);
                if (value != OLE_FREE_SECTOR) {
                    fat_sector_ids[fat_id_count++] = value;
                }
            }
            {
                uint32_t next = ole_u32(sector + entries_per_sector * 4);
                current = next == OLE_END_OF_CHAIN ? -1 : (int)next;
            }
        }
    }
    if (fat_id_count != fat_sector_count) {
        xls_set_error(error, XLS_ERROR_FORMAT, "OLE DIFAT 不完整");
        goto cleanup;
    }
    fat_count = fat_id_count * (sector_size / 4);
    fat = fat_count == 0 ? NULL : (uint32_t *)malloc(fat_count * sizeof(*fat));
    if (fat_count > 0 && fat == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE FAT 时内存不足");
        goto cleanup;
    }
    for (index = 0; index < fat_id_count; ++index) {
        const unsigned char *sector;
        size_t value_index;
        if (!sector_slice(
            data,
            data_size,
            sector_size,
            fat_sector_ids[index],
            &sector,
            error
        )) {
            goto cleanup;
        }
        for (value_index = 0; value_index < sector_size / 4; ++value_index) {
            fat[index * (sector_size / 4) + value_index]
                = ole_u32(sector + value_index * 4);
        }
    }
    if (!read_regular_chain(
        data,
        data_size,
        sector_size,
        fat,
        fat_count,
        first_directory_sector,
        0,
        UINT64_MAX,
        &directory_data,
        &directory_size,
        error
    )) {
        goto cleanup;
    }
    entry_count = directory_size / 128;
    entries = entry_count == 0
        ? NULL
        : (ole_directory_entry *)calloc(entry_count, sizeof(*entries));
    if (entry_count > 0 && entries == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "解析 OLE 目录时内存不足");
        goto cleanup;
    }
    for (index = 0; index < entry_count; ++index) {
        const unsigned char *raw = directory_data + index * 128;
        decode_directory_name(raw, entries[index].name);
        entries[index].type = raw[66];
        entries[index].starting_sector
            = ole_u32(raw + 116) == OLE_END_OF_CHAIN
                ? -1
                : (int)ole_u32(raw + 116);
        entries[index].stream_size = ole_u64(raw + 120);
        if (entries[index].type == 5) {
            root_entry = &entries[index];
        } else if (entries[index].type == 2
            && (ascii_equal_ci(entries[index].name, "Workbook")
                || ascii_equal_ci(entries[index].name, "Book"))) {
            workbook_entry = &entries[index];
        }
    }
    if (workbook_entry == NULL) {
        xls_set_error(error, XLS_ERROR_FORMAT, "未找到工作簿数据流");
        goto cleanup;
    }
    if (first_mini_fat_sector >= 0 && mini_fat_sector_count > 0) {
        if (!read_regular_chain(
            data,
            data_size,
            sector_size,
            fat,
            fat_count,
            first_mini_fat_sector,
            mini_fat_sector_count,
            UINT64_MAX,
            &mini_fat_data,
            &mini_fat_size,
            error
        )) {
            goto cleanup;
        }
        mini_fat_count = mini_fat_size / 4;
        mini_fat = mini_fat_count == 0
            ? NULL
            : (uint32_t *)malloc(mini_fat_count * sizeof(*mini_fat));
        if (mini_fat_count > 0 && mini_fat == NULL) {
            xls_set_error(error, XLS_ERROR_MEMORY, "读取 OLE mini FAT 时内存不足");
            goto cleanup;
        }
        for (index = 0; index < mini_fat_count; ++index) {
            mini_fat[index] = ole_u32(mini_fat_data + index * 4);
        }
    }
    if (root_entry != NULL
        && root_entry->starting_sector >= 0
        && root_entry->stream_size > 0) {
        if (!read_regular_chain(
            data,
            data_size,
            sector_size,
            fat,
            fat_count,
            root_entry->starting_sector,
            0,
            root_entry->stream_size,
            &mini_stream,
            &mini_stream_size,
            error
        )) {
            goto cleanup;
        }
    }
    if (workbook_entry->stream_size > SIZE_MAX) {
        xls_set_error(error, XLS_ERROR_FORMAT, "工作簿数据流过大");
        goto cleanup;
    }
    if (workbook_entry->stream_size < mini_stream_cutoff
        && mini_fat_count > 0
        && mini_stream_size > 0) {
        success = read_mini_chain(
            mini_stream,
            mini_stream_size,
            mini_sector_size,
            mini_fat,
            mini_fat_count,
            workbook_entry->starting_sector,
            workbook_entry->stream_size,
            stream,
            stream_size,
            error
        );
    } else {
        success = read_regular_chain(
            data,
            data_size,
            sector_size,
            fat,
            fat_count,
            workbook_entry->starting_sector,
            0,
            workbook_entry->stream_size,
            stream,
            stream_size,
            error
        );
    }
    if (success) {
        xls_error_clear(error);
    }

cleanup:
    free(mini_stream);
    free(mini_fat);
    free(mini_fat_data);
    free(entries);
    free(directory_data);
    free(fat);
    free(fat_sector_ids);
    free(data);
    return success;
}
