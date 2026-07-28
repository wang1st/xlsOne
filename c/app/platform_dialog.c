#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>

static char *utf8_from_wide(const wchar_t *wide)
{
    int size = WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL
    );
    char *result;
    if (size <= 0) {
        return NULL;
    }
    result = (char *)malloc((size_t)size);
    if (result == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, result, size, NULL, NULL
    ) <= 0) {
        free(result);
        return NULL;
    }
    return result;
}

static int append_path(char ***paths, size_t *count, char *path)
{
    char **replacement = (char **)realloc(
        *paths, (*count + 1) * sizeof(**paths)
    );
    if (replacement == NULL) {
        free(path);
        return 0;
    }
    *paths = replacement;
    (*paths)[(*count)++] = path;
    return 1;
}

int xls_platform_open_files(char ***paths, size_t *path_count)
{
    wchar_t buffer[32768];
    OPENFILENAMEW dialog;
    wchar_t *cursor;
    wchar_t *directory;
    *paths = NULL;
    *path_count = 0;
    memset(buffer, 0, sizeof(buffer));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    dialog.lpstrFilter = L"Excel 工作簿 (*.xlsx;*.xls)\0*.xlsx;*.xls\0"
        L"所有文件 (*.*)\0*.*\0";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST
        | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) {
        return 0;
    }
    directory = buffer;
    cursor = directory + wcslen(directory) + 1;
    if (*cursor == L'\0') {
        char *path = utf8_from_wide(directory);
        return path != NULL && append_path(paths, path_count, path);
    }
    while (*cursor != L'\0') {
        size_t directory_length = wcslen(directory);
        size_t filename_length = wcslen(cursor);
        wchar_t *full = (wchar_t *)malloc(
            (directory_length + filename_length + 2) * sizeof(*full)
        );
        char *utf8;
        if (full == NULL) {
            xls_platform_free_paths(*paths, *path_count);
            *paths = NULL;
            *path_count = 0;
            return 0;
        }
        memcpy(full, directory, directory_length * sizeof(*full));
        full[directory_length] = L'\\';
        memcpy(
            full + directory_length + 1,
            cursor,
            (filename_length + 1) * sizeof(*full)
        );
        utf8 = utf8_from_wide(full);
        free(full);
        if (utf8 == NULL || !append_path(paths, path_count, utf8)) {
            xls_platform_free_paths(*paths, *path_count);
            *paths = NULL;
            *path_count = 0;
            return 0;
        }
        cursor += filename_length + 1;
    }
    return *path_count > 0;
}

int xls_platform_save_file(char **path)
{
    wchar_t buffer[32768] = L"xlsOne-汇总.xlsx";
    OPENFILENAMEW dialog;
    *path = NULL;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    dialog.lpstrFilter = L"Excel 工作簿 (*.xlsx)\0*.xlsx\0"
        L"CSV 文件 (*.csv)\0*.csv\0";
    dialog.lpstrDefExt = L"xlsx";
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&dialog)) {
        return 0;
    }
    *path = utf8_from_wide(buffer);
    return *path != NULL;
}

#else

static char *read_command_output(const char *command)
{
    FILE *pipe = popen(command, "r");
    char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int character;
    if (pipe == NULL) {
        return NULL;
    }
    while ((character = fgetc(pipe)) != EOF) {
        char *replacement;
        if (length + 1 >= capacity) {
            size_t new_capacity = capacity == 0 ? 256 : capacity * 2;
            replacement = (char *)realloc(result, new_capacity);
            if (replacement == NULL) {
                free(result);
                (void)pclose(pipe);
                return NULL;
            }
            result = replacement;
            capacity = new_capacity;
        }
        result[length++] = (char)character;
    }
    if (pclose(pipe) != 0 && length == 0) {
        free(result);
        return NULL;
    }
    if (result == NULL) {
        return NULL;
    }
    while (length > 0
        && (result[length - 1] == '\n' || result[length - 1] == '\r')) {
        --length;
    }
    result[length] = '\0';
    return result;
}

static int split_paths(char *output, char ***paths, size_t *path_count)
{
    char *cursor = output;
    *paths = NULL;
    *path_count = 0;
    while (cursor != NULL && *cursor != '\0') {
        char *end = strchr(cursor, '\n');
        char **replacement;
        char *copy;
        size_t length = end == NULL
            ? strlen(cursor)
            : (size_t)(end - cursor);
        while (length > 0 && cursor[length - 1] == '\r') {
            --length;
        }
        copy = (char *)malloc(length + 1);
        if (copy == NULL) {
            xls_platform_free_paths(*paths, *path_count);
            return 0;
        }
        memcpy(copy, cursor, length);
        copy[length] = '\0';
        replacement = (char **)realloc(
            *paths, (*path_count + 1) * sizeof(**paths)
        );
        if (replacement == NULL) {
            free(copy);
            xls_platform_free_paths(*paths, *path_count);
            return 0;
        }
        *paths = replacement;
        (*paths)[(*path_count)++] = copy;
        cursor = end == NULL ? NULL : end + 1;
    }
    return *path_count > 0;
}

int xls_platform_open_files(char ***paths, size_t *path_count)
{
    char *output;
#if defined(__APPLE__)
    output = read_command_output(
        "osascript "
        "-e 'set picked to choose file with prompt \"选择 Excel 工作簿\" "
        "of type {\"org.openxmlformats.spreadsheetml.sheet\", "
        "\"com.microsoft.excel.xls\"} with multiple selections allowed' "
        "-e 'set output to \"\"' "
        "-e 'repeat with itemPath in picked' "
        "-e 'set output to output & POSIX path of itemPath & linefeed' "
        "-e 'end repeat' "
        "-e 'return output'"
    );
#else
    output = read_command_output(
        "zenity --file-selection --multiple --separator='\\n' "
        "--title='选择 Excel 工作簿' "
        "--file-filter='Excel 工作簿 | *.xlsx *.xls' 2>/dev/null"
    );
#endif
    if (output == NULL) {
        *paths = NULL;
        *path_count = 0;
        return 0;
    }
    {
        int result = split_paths(output, paths, path_count);
        free(output);
        return result;
    }
}

int xls_platform_save_file(char **path)
{
#if defined(__APPLE__)
    *path = read_command_output(
        "osascript "
        "-e 'set targetFile to choose file name with prompt \"导出汇总工作簿\" "
        "default name \"xlsOne-汇总.xlsx\"' "
        "-e 'return POSIX path of targetFile'"
    );
#else
    *path = read_command_output(
        "zenity --file-selection --save --confirm-overwrite "
        "--filename='xlsOne-汇总.xlsx' "
        "--title='导出汇总工作簿' 2>/dev/null"
    );
#endif
    return *path != NULL && (*path)[0] != '\0';
}

#endif

void xls_platform_free_paths(char **paths, size_t path_count)
{
    size_t index;
    for (index = 0; index < path_count; ++index) {
        free(paths[index]);
    }
    free(paths);
}
