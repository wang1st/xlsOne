#include "internal.h"
#include "zip_archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_EOCD_SIGNATURE UINT32_C(0x06054b50)
#define ZIP_CENTRAL_SIGNATURE UINT32_C(0x02014b50)
#define ZIP_LOCAL_SIGNATURE UINT32_C(0x04034b50)
#define ZIP_MAX_ENTRY_SIZE (UINT32_C(512) * UINT32_C(1024) * UINT32_C(1024))
#define ZIP_MAX_ENTRIES UINT16_C(32768)

static uint16_t read_u16(const unsigned char *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t read_u32(const unsigned char *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8u)
        | ((uint32_t)data[2] << 16u)
        | ((uint32_t)data[3] << 24u);
}

static int file_read_all(
    const char *path,
    unsigned char **data,
    size_t *size,
    xls_error *error
)
{
    FILE *file;
    long file_size;
    size_t bytes_read;
    if (path == NULL || data == NULL || size == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid file read arguments");
        return 0;
    }
    *data = NULL;
    *size = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        xls_set_error(error, XLS_ERROR_IO, "无法打开文件: %s", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_IO, "无法读取文件大小: %s", path);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_IO, "无法定位文件: %s", path);
        return 0;
    }
    if ((unsigned long)file_size > (unsigned long)SIZE_MAX) {
        fclose(file);
        xls_set_error(error, XLS_ERROR_FORMAT, "文件过大: %s", path);
        return 0;
    }
    if (file_size > 0) {
        *data = (unsigned char *)malloc((size_t)file_size);
        if (*data == NULL) {
            fclose(file);
            xls_set_error(error, XLS_ERROR_MEMORY, "读取文件时内存不足");
            return 0;
        }
        bytes_read = fread(*data, 1, (size_t)file_size, file);
        if (bytes_read != (size_t)file_size) {
            free(*data);
            *data = NULL;
            fclose(file);
            xls_set_error(error, XLS_ERROR_IO, "文件读取不完整: %s", path);
            return 0;
        }
    }
    fclose(file);
    *size = (size_t)file_size;
    return 1;
}

static const xls_zip_entry *find_entry(
    const xls_zip_archive *archive,
    const char *name
)
{
    size_t index;
    if (archive == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < archive->entry_count; ++index) {
        if (strcmp(archive->entries[index].name, name) == 0) {
            return &archive->entries[index];
        }
    }
    return NULL;
}

int xls_zip_open(
    const char *path,
    xls_zip_archive *archive,
    xls_error *error
)
{
    size_t search_start;
    size_t offset;
    size_t eocd_offset = SIZE_MAX;
    uint16_t entry_count;
    uint32_t central_offset;
    size_t index;
    if (archive == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "archive must not be null");
        return 0;
    }
    memset(archive, 0, sizeof(*archive));
    if (!file_read_all(path, &archive->data, &archive->size, error)) {
        return 0;
    }
    if (archive->size < 22) {
        xls_zip_close(archive);
        xls_set_error(error, XLS_ERROR_FORMAT, "不是有效的 ZIP 文件");
        return 0;
    }
    search_start = archive->size > 65557 ? archive->size - 65557 : 0;
    offset = archive->size - 22;
    for (;;) {
        if (read_u32(archive->data + offset) == ZIP_EOCD_SIGNATURE) {
            eocd_offset = offset;
            break;
        }
        if (offset == search_start) {
            break;
        }
        --offset;
    }
    if (eocd_offset == SIZE_MAX || eocd_offset + 22 > archive->size) {
        xls_zip_close(archive);
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 中找不到中央目录");
        return 0;
    }
    entry_count = read_u16(archive->data + eocd_offset + 10);
    central_offset = read_u32(archive->data + eocd_offset + 16);
    if (entry_count > ZIP_MAX_ENTRIES
        || (size_t)central_offset >= archive->size) {
        xls_zip_close(archive);
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 中央目录无效");
        return 0;
    }
    if (entry_count > 0) {
        archive->entries = (xls_zip_entry *)calloc(
            entry_count, sizeof(*archive->entries)
        );
        if (archive->entries == NULL) {
            xls_zip_close(archive);
            xls_set_error(error, XLS_ERROR_MEMORY, "解析 ZIP 时内存不足");
            return 0;
        }
    }
    archive->entry_count = entry_count;
    offset = (size_t)central_offset;
    for (index = 0; index < entry_count; ++index) {
        uint16_t name_length;
        uint16_t extra_length;
        uint16_t comment_length;
        xls_zip_entry *entry;
        size_t next_offset;
        if (offset + 46 > archive->size
            || read_u32(archive->data + offset) != ZIP_CENTRAL_SIGNATURE) {
            xls_zip_close(archive);
            xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 中央目录损坏");
            return 0;
        }
        name_length = read_u16(archive->data + offset + 28);
        extra_length = read_u16(archive->data + offset + 30);
        comment_length = read_u16(archive->data + offset + 32);
        next_offset = offset + 46u + name_length + extra_length + comment_length;
        if (next_offset > archive->size) {
            xls_zip_close(archive);
            xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目名称越界");
            return 0;
        }
        entry = &archive->entries[index];
        entry->flags = read_u16(archive->data + offset + 8);
        entry->method = read_u16(archive->data + offset + 10);
        entry->compressed_size = read_u32(archive->data + offset + 20);
        entry->uncompressed_size = read_u32(archive->data + offset + 24);
        entry->local_header_offset = read_u32(archive->data + offset + 42);
        if (entry->uncompressed_size > ZIP_MAX_ENTRY_SIZE) {
            xls_zip_close(archive);
            xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目解压后过大");
            return 0;
        }
        entry->name = xls_strndup(
            (const char *)archive->data + offset + 46,
            name_length
        );
        if (entry->name == NULL) {
            xls_zip_close(archive);
            xls_set_error(error, XLS_ERROR_MEMORY, "复制 ZIP 条目名称时内存不足");
            return 0;
        }
        offset = next_offset;
    }
    xls_error_clear(error);
    return 1;
}

int xls_zip_contains(const xls_zip_archive *archive, const char *name)
{
    return find_entry(archive, name) != NULL;
}

int xls_zip_read(
    const xls_zip_archive *archive,
    const char *name,
    unsigned char **data,
    size_t *size,
    xls_error *error
)
{
    const xls_zip_entry *entry;
    size_t header_offset;
    uint16_t name_length;
    uint16_t extra_length;
    size_t payload_offset;
    unsigned char *output;
    if (archive == NULL || name == NULL || data == NULL || size == NULL) {
        xls_set_error(error, XLS_ERROR_ARGUMENT, "invalid ZIP read arguments");
        return 0;
    }
    *data = NULL;
    *size = 0;
    entry = find_entry(archive, name);
    if (entry == NULL) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目不存在: %s", name);
        return 0;
    }
    header_offset = entry->local_header_offset;
    if (header_offset + 30 > archive->size
        || read_u32(archive->data + header_offset) != ZIP_LOCAL_SIGNATURE) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 本地文件头无效: %s", name);
        return 0;
    }
    name_length = read_u16(archive->data + header_offset + 26);
    extra_length = read_u16(archive->data + header_offset + 28);
    payload_offset = header_offset + 30u + name_length + extra_length;
    if (payload_offset > archive->size
        || entry->compressed_size > archive->size - payload_offset) {
        xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 条目数据越界: %s", name);
        return 0;
    }
    output = (unsigned char *)malloc((size_t)entry->uncompressed_size + 1);
    if (output == NULL) {
        xls_set_error(error, XLS_ERROR_MEMORY, "解压 ZIP 条目时内存不足");
        return 0;
    }
    if (entry->method == 0) {
        if (entry->compressed_size != entry->uncompressed_size) {
            free(output);
            xls_set_error(error, XLS_ERROR_FORMAT, "ZIP 存储条目大小不一致");
            return 0;
        }
        memcpy(
            output,
            archive->data + payload_offset,
            entry->uncompressed_size
        );
    } else if (entry->method == 8) {
        z_stream stream;
        int inflate_result;
        memset(&stream, 0, sizeof(stream));
        stream.next_in = (Bytef *)(archive->data + payload_offset);
        stream.avail_in = entry->compressed_size;
        stream.next_out = output;
        stream.avail_out = entry->uncompressed_size;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            free(output);
            xls_set_error(error, XLS_ERROR_INTERNAL, "无法初始化 ZIP 解压器");
            return 0;
        }
        inflate_result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (inflate_result != Z_STREAM_END
            || stream.total_out != entry->uncompressed_size) {
            free(output);
            xls_set_error(error, XLS_ERROR_FORMAT, "ZIP deflate 解压失败: %s", name);
            return 0;
        }
    } else {
        free(output);
        xls_set_error(
            error,
            XLS_ERROR_UNSUPPORTED,
            "暂不支持 ZIP 压缩方法 %u: %s",
            (unsigned int)entry->method,
            name
        );
        return 0;
    }
    output[entry->uncompressed_size] = '\0';
    *data = output;
    *size = entry->uncompressed_size;
    xls_error_clear(error);
    return 1;
}

void xls_zip_close(xls_zip_archive *archive)
{
    size_t index;
    if (archive == NULL) {
        return;
    }
    for (index = 0; index < archive->entry_count; ++index) {
        free(archive->entries[index].name);
    }
    free(archive->entries);
    free(archive->data);
    memset(archive, 0, sizeof(*archive));
}
