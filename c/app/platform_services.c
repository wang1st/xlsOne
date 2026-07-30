#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#elif !defined(COBJMACROS)
#define COBJMACROS
#endif

#include "platform_dialog.h"
#include "i18n.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shldisp.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winhttp.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#endif
#endif

#define XLSONE_HTTP_RESPONSE_LIMIT (1024u * 1024u)

#if !defined(XLSONE_VERSION)
#define XLSONE_VERSION "development"
#endif

#define XLSONE_WIDE_TEXT_IMPL(value) L##value
#define XLSONE_WIDE_TEXT(value) XLSONE_WIDE_TEXT_IMPL(value)

static char *duplicate_text(const char *text)
{
    const size_t length = text == NULL ? 0u : strlen(text);
    char *copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0u) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

#if defined(_WIN32)

static wchar_t *wide_from_utf8(const char *utf8)
{
    int count;
    wchar_t *wide;
    if (utf8 == NULL) {
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (count <= 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, count
    ) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *utf8_from_wide_service(const wchar_t *wide)
{
    int count;
    char *utf8;
    if (wide == NULL) {
        return NULL;
    }
    count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        return NULL;
    }
    utf8 = (char *)malloc((size_t)count);
    if (utf8 == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, utf8, count, NULL, NULL
    ) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static wchar_t *windows_extended_path(const wchar_t *absolute_path)
{
    const size_t length = wcslen(absolute_path);
    const int is_unc = length >= 2u
        && absolute_path[0] == L'\\'
        && absolute_path[1] == L'\\';
    const wchar_t *prefix = is_unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    const size_t prefix_length = wcslen(prefix);
    const wchar_t *suffix = is_unc ? absolute_path + 2 : absolute_path;
    const size_t suffix_length = wcslen(suffix);
    wchar_t *extended;
    if (wcsncmp(absolute_path, L"\\\\?\\", 4u) == 0) {
        return _wcsdup(absolute_path);
    }
    extended = (wchar_t *)malloc(
        (prefix_length + suffix_length + 1u) * sizeof(*extended)
    );
    if (extended == NULL) {
        return NULL;
    }
    memcpy(
        extended,
        prefix,
        prefix_length * sizeof(*extended)
    );
    memcpy(
        extended + prefix_length,
        suffix,
        (suffix_length + 1u) * sizeof(*extended)
    );
    return extended;
}

static int windows_same_directory(
    const wchar_t *left,
    const wchar_t *right
)
{
    size_t left_length;
    size_t right_length;
    if (left == NULL || right == NULL) {
        return 0;
    }
    left_length = wcslen(left);
    right_length = wcslen(right);
    while (left_length > 3u
        && (left[left_length - 1u] == L'\\'
            || left[left_length - 1u] == L'/')) {
        --left_length;
    }
    while (right_length > 3u
        && (right[right_length - 1u] == L'\\'
            || right[right_length - 1u] == L'/')) {
        --right_length;
    }
    return left_length == right_length
        && _wcsnicmp(left, right, left_length) == 0;
}

static int windows_select_browser_item(
    IWebBrowser2 *browser,
    const wchar_t *file_name
)
{
    IDispatch *document = NULL;
    IShellFolderViewDual *view = NULL;
    Folder *folder = NULL;
    FolderItem *item = NULL;
    BSTR item_name = NULL;
    VARIANT item_variant;
    SHANDLE_PTR window_handle = 0;
    HRESULT status;
    int selected = 0;
    status = IWebBrowser2_get_Document(browser, &document);
    if (FAILED(status) || document == NULL) {
        goto cleanup;
    }
    status = IDispatch_QueryInterface(
        document,
        &IID_IShellFolderViewDual,
        (void **)&view
    );
    if (FAILED(status) || view == NULL) {
        goto cleanup;
    }
    status = IShellFolderViewDual_get_Folder(view, &folder);
    if (FAILED(status) || folder == NULL) {
        goto cleanup;
    }
    item_name = SysAllocString(file_name);
    if (item_name == NULL) {
        goto cleanup;
    }
    status = Folder_ParseName(folder, item_name, &item);
    if (FAILED(status) || item == NULL) {
        goto cleanup;
    }
    VariantInit(&item_variant);
    V_VT(&item_variant) = VT_DISPATCH;
    V_DISPATCH(&item_variant) = (IDispatch *)item;
    status = IShellFolderViewDual_SelectItem(
        view,
        &item_variant,
        0x1d
    );
    V_VT(&item_variant) = VT_EMPTY;
    if (FAILED(status)) {
        goto cleanup;
    }
    if (SUCCEEDED(IWebBrowser2_get_HWND(
        browser, &window_handle
    ))) {
        HWND window = (HWND)(INT_PTR)window_handle;
        if (IsIconic(window)) {
            (void)ShowWindow(window, SW_RESTORE);
        }
        (void)SetForegroundWindow(window);
    }
    selected = 1;

cleanup:
    if (item != NULL) {
        FolderItem_Release(item);
    }
    if (item_name != NULL) {
        SysFreeString(item_name);
    }
    if (folder != NULL) {
        Folder_Release(folder);
    }
    if (view != NULL) {
        IShellFolderViewDual_Release(view);
    }
    if (document != NULL) {
        IDispatch_Release(document);
    }
    return selected;
}

static int windows_select_in_open_directory(
    const wchar_t *directory,
    const wchar_t *file_name
)
{
    IShellWindows *shell_windows = NULL;
    long window_count = 0;
    long index;
    HRESULT status;
    int selected = 0;
    status = CoCreateInstance(
        &CLSID_ShellWindows,
        NULL,
        CLSCTX_LOCAL_SERVER,
        &IID_IShellWindows,
        (void **)&shell_windows
    );
    if (FAILED(status) || shell_windows == NULL) {
        return 0;
    }
    if (FAILED(IShellWindows_get_Count(
        shell_windows, &window_count
    ))) {
        IShellWindows_Release(shell_windows);
        return 0;
    }
    for (index = 0; index < window_count && !selected; ++index) {
        VARIANT item_index;
        IDispatch *window_dispatch = NULL;
        IWebBrowser2 *browser = NULL;
        BSTR location = NULL;
        wchar_t location_path[32768];
        DWORD location_capacity = (DWORD)(
            sizeof(location_path) / sizeof(location_path[0])
        );
        VariantInit(&item_index);
        V_VT(&item_index) = VT_I4;
        V_I4(&item_index) = index;
        status = IShellWindows_Item(
            shell_windows, item_index, &window_dispatch
        );
        if (FAILED(status) || window_dispatch == NULL) {
            continue;
        }
        status = IDispatch_QueryInterface(
            window_dispatch,
            &IID_IWebBrowser2,
            (void **)&browser
        );
        IDispatch_Release(window_dispatch);
        if (FAILED(status) || browser == NULL) {
            continue;
        }
        status = IWebBrowser2_get_LocationURL(browser, &location);
        if (SUCCEEDED(status)
            && location != NULL
            && SUCCEEDED(PathCreateFromUrlW(
                location,
                location_path,
                &location_capacity,
                0u
            ))
            && windows_same_directory(
                location_path, directory
            )) {
            selected = windows_select_browser_item(
                browser, file_name
            );
        }
        if (location != NULL) {
            SysFreeString(location);
        }
        IWebBrowser2_Release(browser);
    }
    IShellWindows_Release(shell_windows);
    return selected;
}

static int windows_choose_file(
    char **path,
    const wchar_t *title,
    const wchar_t *filter
)
{
    wchar_t buffer[32768];
    OPENFILENAMEW dialog;
    *path = NULL;
    memset(buffer, 0, sizeof(buffer));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = title;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    dialog.lpstrFilter = filter;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) {
        return 0;
    }
    *path = utf8_from_wide_service(buffer);
    return *path != NULL;
}

static int windows_save_file(
    char **path,
    const wchar_t *title,
    const wchar_t *default_name,
    const wchar_t *filter,
    const wchar_t *extension
)
{
    wchar_t buffer[32768];
    OPENFILENAMEW dialog;
    *path = NULL;
    memset(buffer, 0, sizeof(buffer));
    (void)wcsncpy_s(
        buffer,
        sizeof(buffer) / sizeof(buffer[0]),
        default_name,
        _TRUNCATE
    );
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = title;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    dialog.lpstrFilter = filter;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&dialog)) {
        return 0;
    }
    *path = utf8_from_wide_service(buffer);
    return *path != NULL;
}

static char *read_windows_command(const wchar_t *command)
{
    SECURITY_ATTRIBUTES attributes;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HANDLE read_pipe = NULL;
    HANDLE write_pipe = NULL;
    wchar_t *mutable_command;
    char *output = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    DWORD read_count;
    char chunk[512];

    memset(&attributes, 0, sizeof(attributes));
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
        return NULL;
    }
    (void)SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    memset(&process, 0, sizeof(process));
    mutable_command = _wcsdup(command);
    if (mutable_command == NULL) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return NULL;
    }
    if (!CreateProcessW(
        NULL,
        mutable_command,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &startup,
        &process
    )) {
        free(mutable_command);
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return NULL;
    }
    free(mutable_command);
    CloseHandle(write_pipe);
    while (ReadFile(read_pipe, chunk, sizeof(chunk), &read_count, NULL)
        && read_count > 0u) {
        char *replacement;
        const size_t needed = length + (size_t)read_count + 1u;
        if (needed > capacity) {
            size_t new_capacity = capacity == 0u ? 1024u : capacity * 2u;
            while (new_capacity < needed) {
                new_capacity *= 2u;
            }
            replacement = (char *)realloc(output, new_capacity);
            if (replacement == NULL) {
                free(output);
                output = NULL;
                break;
            }
            output = replacement;
            capacity = new_capacity;
        }
        memcpy(output + length, chunk, (size_t)read_count);
        length += (size_t)read_count;
    }
    CloseHandle(read_pipe);
    (void)WaitForSingleObject(process.hProcess, 10000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (output == NULL) {
        return NULL;
    }
    while (length > 0u
        && (output[length - 1u] == '\r' || output[length - 1u] == '\n'
            || output[length - 1u] == ' ' || output[length - 1u] == '\t')) {
        --length;
    }
    output[length] = '\0';
    return output;
}

static int windows_license_path(wchar_t *path, size_t capacity)
{
    wchar_t app_data[MAX_PATH];
    wchar_t directory[MAX_PATH];
    if (SHGetFolderPathW(
        NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, app_data
    ) != S_OK) {
        return 0;
    }
    if (_snwprintf_s(
        directory,
        sizeof(directory) / sizeof(directory[0]),
        _TRUNCATE,
        L"%ls\\Z-Pulse",
        app_data
    ) < 0) {
        return 0;
    }
    if (!CreateDirectoryW(directory, NULL)
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 0;
    }
    if (_snwprintf_s(
        directory,
        sizeof(directory) / sizeof(directory[0]),
        _TRUNCATE,
        L"%ls\\Z-Pulse\\xlsOne",
        app_data
    ) < 0) {
        return 0;
    }
    if (!CreateDirectoryW(directory, NULL)
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 0;
    }
    return _snwprintf_s(
        path, capacity, _TRUNCATE, L"%ls\\license.dat", directory
    ) >= 0;
}

static int append_http_bytes(
    char **buffer,
    size_t *length,
    size_t *capacity,
    const char *bytes,
    size_t byte_count
)
{
    char *replacement;
    size_t new_capacity;
    if (byte_count > XLSONE_HTTP_RESPONSE_LIMIT - *length) {
        return 0;
    }
    if (*length + byte_count + 1u <= *capacity) {
        memcpy(*buffer + *length, bytes, byte_count);
        *length += byte_count;
        (*buffer)[*length] = '\0';
        return 1;
    }
    new_capacity = *capacity == 0u ? 4096u : *capacity;
    while (new_capacity < *length + byte_count + 1u) {
        new_capacity *= 2u;
    }
    replacement = (char *)realloc(*buffer, new_capacity);
    if (replacement == NULL) {
        return 0;
    }
    *buffer = replacement;
    *capacity = new_capacity;
    memcpy(*buffer + *length, bytes, byte_count);
    *length += byte_count;
    (*buffer)[*length] = '\0';
    return 1;
}

#else

static char *read_command_output_service(const char *command)
{
    FILE *pipe = popen(command, "r");
    char *result = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    int character;
    if (pipe == NULL) {
        return NULL;
    }
    while ((character = fgetc(pipe)) != EOF) {
        char *replacement;
        if (length + 1u >= capacity) {
            const size_t new_capacity = capacity == 0u ? 256u : capacity * 2u;
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
    if (pclose(pipe) != 0 && length == 0u) {
        free(result);
        return NULL;
    }
    if (result == NULL) {
        return NULL;
    }
    while (length > 0u
        && (result[length - 1u] == '\n' || result[length - 1u] == '\r')) {
        --length;
    }
    result[length] = '\0';
    return result;
}

static int run_child_process(char *const arguments[])
{
    pid_t child = fork();
    int wait_status = 0;
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execvp(arguments[0], arguments);
        _exit(127);
    }
    return child > 0
        && waitpid(child, &wait_status, 0) == child
        && WIFEXITED(wait_status)
        && WEXITSTATUS(wait_status) == 0;
}

#if defined(__APPLE__)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static int macos_bring_finder_front_window(void)
{
    ProcessSerialNumber process = {0u, 0u};
    while (GetNextProcess(&process) == noErr) {
        CFDictionaryRef information =
            ProcessInformationCopyDictionary(
                &process,
                (UInt32)kProcessDictionaryIncludeAllInformationMask
            );
        if (information != NULL) {
            CFTypeRef identifier = CFDictionaryGetValue(
                information, kCFBundleIdentifierKey
            );
            const int is_finder = identifier != NULL
                && CFGetTypeID(identifier) == CFStringGetTypeID()
                && CFStringCompare(
                    (CFStringRef)identifier,
                    CFSTR("com.apple.finder"),
                    0u
                ) == kCFCompareEqualTo;
            CFRelease(information);
            if (is_finder) {
                const OptionBits options =
                    kSetFrontProcessFrontWindowOnly
                    | kSetFrontProcessCausedByUser;
                return SetFrontProcessWithOptions(
                    &process, options
                ) == noErr;
            }
        }
    }
    return 0;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#else
static char *linux_file_uri(const char *absolute_path)
{
    static const char hexadecimal[] = "0123456789ABCDEF";
    const unsigned char *cursor =
        (const unsigned char *)absolute_path;
    const size_t input_length = strlen(absolute_path);
    char *uri = (char *)malloc(input_length * 3u + 8u);
    size_t output = 0u;
    if (uri == NULL) {
        return NULL;
    }
    memcpy(uri, "file://", 7u);
    output = 7u;
    while (*cursor != '\0') {
        const unsigned char character = *cursor++;
        if ((character >= (unsigned char)'a'
                && character <= (unsigned char)'z')
            || (character >= (unsigned char)'A'
                && character <= (unsigned char)'Z')
            || (character >= (unsigned char)'0'
                && character <= (unsigned char)'9')
            || character == (unsigned char)'/'
            || character == (unsigned char)'-'
            || character == (unsigned char)'_'
            || character == (unsigned char)'.'
            || character == (unsigned char)'~') {
            uri[output++] = (char)character;
        } else {
            uri[output++] = '%';
            uri[output++] = hexadecimal[character >> 4u];
            uri[output++] = hexadecimal[character & 0x0fu];
        }
    }
    uri[output] = '\0';
    return uri;
}

static int linux_show_item_with_file_manager(
    const char *absolute_path
)
{
    char *uri = linux_file_uri(absolute_path);
    char *gdbus_items = NULL;
    char *dbus_items = NULL;
    int shown = 0;
    if (uri == NULL) {
        return 0;
    }
    gdbus_items = (char *)malloc(strlen(uri) + 5u);
    dbus_items = (char *)malloc(strlen(uri) + 14u);
    if (gdbus_items != NULL) {
        (void)snprintf(
            gdbus_items,
            strlen(uri) + 5u,
            "['%s']",
            uri
        );
        {
            char *const arguments[] = {
                "gdbus",
                "call",
                "--session",
                "--dest",
                "org.freedesktop.FileManager1",
                "--object-path",
                "/org/freedesktop/FileManager1",
                "--method",
                "org.freedesktop.FileManager1.ShowItems",
                gdbus_items,
                "",
                NULL
            };
            shown = run_child_process(arguments);
        }
    }
    if (!shown && dbus_items != NULL) {
        (void)snprintf(
            dbus_items,
            strlen(uri) + 14u,
            "array:string:%s",
            uri
        );
        {
            char *const arguments[] = {
                "dbus-send",
                "--session",
                "--type=method_call",
                "--print-reply",
                "--dest=org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1.ShowItems",
                dbus_items,
                "string:",
                NULL
            };
            shown = run_child_process(arguments);
        }
    }
    free(dbus_items);
    free(gdbus_items);
    free(uri);
    return shown;
}

static int ascii_contains_case_insensitive(
    const char *text,
    const char *needle
)
{
    const size_t needle_length =
        needle == NULL ? 0u : strlen(needle);
    const char *cursor;
    if (text == NULL || needle_length == 0u) {
        return 0;
    }
    for (cursor = text; *cursor != '\0'; ++cursor) {
        size_t index;
        for (index = 0u; index < needle_length; ++index) {
            unsigned char left =
                (unsigned char)cursor[index];
            unsigned char right =
                (unsigned char)needle[index];
            if (left == '\0') {
                break;
            }
            if (left >= (unsigned char)'A'
                && left <= (unsigned char)'Z') {
                left = (unsigned char)(
                    left - (unsigned char)'A'
                    + (unsigned char)'a'
                );
            }
            if (right >= (unsigned char)'A'
                && right <= (unsigned char)'Z') {
                right = (unsigned char)(
                    right - (unsigned char)'A'
                    + (unsigned char)'a'
                );
            }
            if (left != right) {
                break;
            }
        }
        if (index == needle_length) {
            return 1;
        }
    }
    return 0;
}

static int linux_show_item_with_default_file_manager(
    const char *absolute_path
)
{
    char *desktop = read_command_output_service(
        "xdg-mime query default inode/directory 2>/dev/null"
    );
    int shown = 0;
    if (desktop == NULL || desktop[0] == '\0') {
        free(desktop);
        return 0;
    }
    if (ascii_contains_case_insensitive(desktop, "nemo")) {
        char *const arguments[] = {
            "nemo", "--select", (char *)absolute_path, NULL
        };
        shown = run_child_process(arguments);
    } else if (ascii_contains_case_insensitive(
        desktop, "nautilus"
    )) {
        char *const arguments[] = {
            "nautilus", "--select", (char *)absolute_path, NULL
        };
        shown = run_child_process(arguments);
    } else if (ascii_contains_case_insensitive(
        desktop, "dolphin"
    )) {
        char *const arguments[] = {
            "dolphin", "--select", (char *)absolute_path, NULL
        };
        shown = run_child_process(arguments);
    } else if (ascii_contains_case_insensitive(
        desktop, "caja"
    )) {
        char *const arguments[] = {
            "caja", "--select", (char *)absolute_path, NULL
        };
        shown = run_child_process(arguments);
    } else if (ascii_contains_case_insensitive(
        desktop, "thunar"
    )) {
        char *const arguments[] = {
            "thunar", (char *)absolute_path, NULL
        };
        shown = run_child_process(arguments);
    }
    free(desktop);
    return shown;
}
#endif

static int ensure_directory(const char *path)
{
    if (mkdir(path, 0700) == 0) {
        return 1;
    }
    return errno == EEXIST;
}

static int posix_license_path(char *path, size_t capacity)
{
    const char *home = getenv("HOME");
#if !defined(__APPLE__)
    const char *config_home = getenv("XDG_CONFIG_HOME");
#endif
    char parent[1024];
    char directory[1024];
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
#if defined(__APPLE__)
    if (snprintf(
        parent,
        sizeof(parent),
        "%s/Library/Application Support",
        home
    ) < 0
        || snprintf(
            directory,
            sizeof(directory),
            "%s/Library/Application Support/cn.z-pulse.xlsone",
            home
        ) < 0) {
        return 0;
    }
#else
    if (config_home != NULL && config_home[0] != '\0') {
        if (snprintf(parent, sizeof(parent), "%s", config_home) < 0
            || snprintf(directory, sizeof(directory), "%s/xlsOne", config_home) < 0) {
            return 0;
        }
    } else if (snprintf(parent, sizeof(parent), "%s/.config", home) < 0
        || snprintf(directory, sizeof(directory), "%s/.config/xlsOne", home) < 0) {
        return 0;
    }
#endif
    if (!ensure_directory(parent) || !ensure_directory(directory)) {
        return 0;
    }
    return snprintf(path, capacity, "%s/license.dat", directory) > 0;
}

static int capture_curl(
    const char *method,
    const char *url,
    const char *json_body,
    char **response_body,
    long *status_code
)
{
    int output_pipe[2];
    pid_t child;
    char *output = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    int wait_status = 0;
    char chunk[2048];
    ssize_t count;
    char *status_line;
    if (pipe(output_pipe) != 0) {
        return 0;
    }
    child = fork();
    if (child == 0) {
        int null_fd;
        (void)dup2(output_pipe[1], STDOUT_FILENO);
        null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (json_body != NULL) {
            execlp(
                "curl",
                "curl",
                "-sS",
                "--max-time",
                "12",
                "-X",
                method,
                "-H",
                "Content-Type: application/json",
                "-H",
                "Accept: application/json",
                "--data-binary",
                json_body,
                "-w",
                "\n%{http_code}",
                url,
                (char *)NULL
            );
        } else {
            execlp(
                "curl",
                "curl",
                "-sS",
                "--max-time",
                "12",
                "-X",
                method,
                "-H",
                "Accept: application/json",
                "-w",
                "\n%{http_code}",
                url,
                (char *)NULL
            );
        }
        _exit(127);
    }
    close(output_pipe[1]);
    if (child < 0) {
        close(output_pipe[0]);
        return 0;
    }
    while ((count = read(output_pipe[0], chunk, sizeof(chunk))) > 0) {
        char *replacement;
        size_t needed;
        if ((size_t)count > XLSONE_HTTP_RESPONSE_LIMIT - length) {
            free(output);
            output = NULL;
            break;
        }
        needed = length + (size_t)count + 1u;
        if (needed > capacity) {
            size_t new_capacity = capacity == 0u ? 4096u : capacity * 2u;
            while (new_capacity < needed) {
                new_capacity *= 2u;
            }
            replacement = (char *)realloc(output, new_capacity);
            if (replacement == NULL) {
                free(output);
                output = NULL;
                break;
            }
            output = replacement;
            capacity = new_capacity;
        }
        memcpy(output + length, chunk, (size_t)count);
        length += (size_t)count;
        output[length] = '\0';
    }
    close(output_pipe[0]);
    (void)waitpid(child, &wait_status, 0);
    if (output == NULL || !WIFEXITED(wait_status)
        || WEXITSTATUS(wait_status) != 0) {
        free(output);
        return 0;
    }
    status_line = strrchr(output, '\n');
    if (status_line == NULL) {
        free(output);
        return 0;
    }
    *status_code = strtol(status_line + 1, NULL, 10);
    *status_line = '\0';
    *response_body = output;
    return 1;
}

#endif

int xls_platform_open_license_file(char **path)
{
#if defined(_WIN32)
    wchar_t *title = wide_from_utf8(
        xls_i18n_translate("导入授权文件")
    );
    int result = windows_choose_file(
        path,
        title,
        L"授权文件 (*.license;*.txt)\0*.license;*.txt\0"
        L"所有文件 (*.*)\0*.*\0"
    );
    free(title);
    return result;
#elif defined(__APPLE__)
    char command[2048];
    if (snprintf(
        command,
        sizeof(command),
        "osascript "
        "-e 'set picked to choose file with prompt \"%s\" "
        "of type {\"public.data\", \"public.plain-text\"}' "
        "-e 'return POSIX path of picked'",
        xls_i18n_translate("导入授权文件")
    ) < 0) {
        return 0;
    }
    *path = read_command_output_service(command);
    return *path != NULL && (*path)[0] != '\0';
#else
    char command[2048];
    if (snprintf(
        command,
        sizeof(command),
        "zenity --file-selection --title='%s' "
        "--file-filter='License | *.license *.txt' 2>/dev/null",
        xls_i18n_translate("导入授权文件")
    ) < 0) {
        return 0;
    }
    *path = read_command_output_service(command);
    return *path != NULL && (*path)[0] != '\0';
#endif
}

int xls_platform_save_rules_file(char **path)
{
#if defined(_WIN32)
    wchar_t *title = wide_from_utf8(
        xls_i18n_translate("保存当前修正规则")
    );
    wchar_t *default_name = wide_from_utf8(
        xls_i18n_translate("xlsOne-修正规则.json")
    );
    int result = windows_save_file(
        path,
        title,
        default_name == NULL ? L"xlsOne-rules.json" : default_name,
        L"JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0",
        L"json"
    );
    free(title);
    free(default_name);
    return result;
#elif defined(__APPLE__)
    char command[2048];
    if (snprintf(
        command,
        sizeof(command),
        "osascript "
        "-e 'set targetFile to choose file name with prompt \"%s\" "
        "default name \"%s\"' "
        "-e 'return POSIX path of targetFile'",
        xls_i18n_translate("保存当前修正规则"),
        xls_i18n_translate("xlsOne-修正规则.json")
    ) < 0) {
        return 0;
    }
    *path = read_command_output_service(command);
    return *path != NULL && (*path)[0] != '\0';
#else
    char command[2048];
    if (snprintf(
        command,
        sizeof(command),
        "zenity --file-selection --save --confirm-overwrite "
        "--filename='%s' "
        "--title='%s' 2>/dev/null",
        xls_i18n_translate("xlsOne-修正规则.json"),
        xls_i18n_translate("保存当前修正规则")
    ) < 0) {
        return 0;
    }
    *path = read_command_output_service(command);
    return *path != NULL && (*path)[0] != '\0';
#endif
}

int xls_platform_open_url(const char *url)
{
#if defined(_WIN32)
    wchar_t *wide = wide_from_utf8(url);
    HINSTANCE result;
    if (wide == NULL) {
        return 0;
    }
    result = ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    free(wide);
    return (INT_PTR)result > 32;
#else
    pid_t child = fork();
    if (child == 0) {
        const pid_t launcher = fork();
        if (launcher < 0) {
            _exit(127);
        }
        if (launcher > 0) {
            _exit(0);
        }
#if defined(__APPLE__)
        execlp("open", "open", url, (char *)NULL);
#else
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
#endif
        _exit(127);
    }
    if (child < 0) {
        return 0;
    }
    {
        int wait_status = 0;
        return waitpid(child, &wait_status, 0) == child
            && WIFEXITED(wait_status)
            && WEXITSTATUS(wait_status) == 0;
    }
#endif
}

int xls_platform_absolute_path(
    const char *path,
    char **absolute_path
)
{
    *absolute_path = NULL;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
#if defined(_WIN32)
    {
        wchar_t *wide = wide_from_utf8(path);
        wchar_t *absolute_wide;
        DWORD required;
        DWORD written;
        if (wide == NULL) {
            return 0;
        }
        required = GetFullPathNameW(wide, 0u, NULL, NULL);
        if (required == 0u) {
            free(wide);
            return 0;
        }
        absolute_wide = (wchar_t *)malloc(
            (size_t)required * sizeof(*absolute_wide)
        );
        if (absolute_wide == NULL) {
            free(wide);
            return 0;
        }
        written = GetFullPathNameW(
            wide, required, absolute_wide, NULL
        );
        free(wide);
        if (written == 0u || written >= required) {
            free(absolute_wide);
            return 0;
        }
        *absolute_path = utf8_from_wide_service(absolute_wide);
        free(absolute_wide);
        return *absolute_path != NULL;
    }
#else
    *absolute_path = realpath(path, NULL);
    return *absolute_path != NULL;
#endif
}

int xls_platform_reveal_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
#if defined(_WIN32)
    {
        wchar_t *wide = wide_from_utf8(path);
        wchar_t *absolute_wide = NULL;
        wchar_t *attributes_path = NULL;
        wchar_t *directory = NULL;
        wchar_t *separator;
        const wchar_t *file_name;
        PIDLIST_ABSOLUTE item_identifier = NULL;
        DWORD required;
        DWORD written;
        DWORD attributes;
        HRESULT com_status;
        int uninitialize_com = 0;
        int success = 0;
        if (wide == NULL) {
            return 0;
        }
        required = GetFullPathNameW(wide, 0u, NULL, NULL);
        if (required == 0u) {
            free(wide);
            return 0;
        }
        absolute_wide = (wchar_t *)malloc(
            (size_t)required * sizeof(*absolute_wide)
        );
        if (absolute_wide == NULL) {
            free(wide);
            return 0;
        }
        written = GetFullPathNameW(
            wide, required, absolute_wide, NULL
        );
        free(wide);
        if (written == 0u || written >= required) {
            free(absolute_wide);
            return 0;
        }
        attributes_path = windows_extended_path(absolute_wide);
        attributes = attributes_path == NULL
            ? INVALID_FILE_ATTRIBUTES
            : GetFileAttributesW(attributes_path);
        free(attributes_path);
        if (attributes == INVALID_FILE_ATTRIBUTES
            || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            free(absolute_wide);
            return 0;
        }
        directory = _wcsdup(absolute_wide);
        separator = directory == NULL
            ? NULL
            : wcsrchr(directory, L'\\');
        if (separator == NULL && directory != NULL) {
            separator = wcsrchr(directory, L'/');
        }
        if (separator == NULL || separator[1] == L'\0') {
            free(directory);
            free(absolute_wide);
            return 0;
        }
        file_name = absolute_wide + (separator - directory) + 1;
        if (separator == directory + 2
            && directory[1] == L':') {
            separator[1] = L'\0';
        } else {
            *separator = L'\0';
        }
        com_status = CoInitializeEx(
            NULL,
            COINIT_APARTMENTTHREADED
        );
        uninitialize_com = SUCCEEDED(com_status);
        if (SUCCEEDED(com_status)
            || com_status == RPC_E_CHANGED_MODE) {
            success = windows_select_in_open_directory(
                directory, file_name
            );
            if (!success
                && SUCCEEDED(SHParseDisplayName(
                    absolute_wide,
                    NULL,
                    &item_identifier,
                    0u,
                    NULL
                ))) {
                success = SUCCEEDED(
                    SHOpenFolderAndSelectItems(
                        item_identifier, 0u, NULL, 0u
                    )
                );
            }
        }
        if (item_identifier != NULL) {
            CoTaskMemFree(item_identifier);
        }
        if (uninitialize_com) {
            CoUninitialize();
        }
        free(directory);
        free(absolute_wide);
        return success;
    }
#else
    {
        struct stat info;
        char *absolute_path = realpath(path, NULL);
        int success = 0;
        if (absolute_path == NULL
            || stat(absolute_path, &info) != 0
            || !S_ISREG(info.st_mode)) {
            free(absolute_path);
            return 0;
        }
#if defined(__APPLE__)
        {
            char *const arguments[] = {
                "open",
                "-g",
                "-R",
                absolute_path,
                NULL
            };
            success = run_child_process(arguments);
            if (success) {
                const struct timespec delay = {0, 120000000L};
                (void)nanosleep(&delay, NULL);
                (void)macos_bring_finder_front_window();
            }
        }
#else
        /*
         * Address the selected desktop's file manager directly first.
         * A different process can own org.freedesktop.FileManager1 and
         * forward ShowItems in a way that always creates a new window.
         * The native command reaches the running single-instance manager,
         * which can reuse an existing view of the parent directory.
         */
        success = linux_show_item_with_default_file_manager(
            absolute_path
        );
        if (!success) {
            success = linux_show_item_with_file_manager(absolute_path);
        }
        if (!success) {
            char *const nautilus_arguments[] = {
                "nautilus", "--select", absolute_path, NULL
            };
            success = run_child_process(nautilus_arguments);
        }
        if (!success) {
            char *const dolphin_arguments[] = {
                "dolphin", "--select", absolute_path, NULL
            };
            success = run_child_process(dolphin_arguments);
        }
        if (!success) {
            char *const caja_arguments[] = {
                "caja", "--select", absolute_path, NULL
            };
            success = run_child_process(caja_arguments);
        }
        if (!success) {
            char *const nemo_arguments[] = {
                "nemo", "--select", absolute_path, NULL
            };
            success = run_child_process(nemo_arguments);
        }
        if (!success) {
            char *directory = duplicate_text(absolute_path);
            char *separator;
            if (directory == NULL) {
                free(absolute_path);
                return 0;
            }
            separator = strrchr(directory, '/');
            if (separator == NULL) {
                directory[0] = '.';
                directory[1] = '\0';
            } else if (separator == directory) {
                directory[1] = '\0';
            } else {
                *separator = '\0';
            }
            {
                char *const arguments[] = {
                    "xdg-open", directory, NULL
                };
                success = run_child_process(arguments);
            }
            free(directory);
        }
#endif
        free(absolute_path);
        return success;
    }
#endif
}

int xls_platform_ensure_application_shortcuts(void)
{
#if defined(__APPLE__)
    uint32_t executable_capacity = 0u;
    char *executable = NULL;
    char *resolved = NULL;
    char *bundle_suffix;
    char *bundle_path = NULL;
    const struct passwd *account;
    char desktop_path[4096];
    CFURLRef bundle_url = NULL;
    int registered = 0;
    int shortcut_ready = 0;
    (void)_NSGetExecutablePath(NULL, &executable_capacity);
    if (executable_capacity == 0u) {
        return 0;
    }
    executable = (char *)malloc((size_t)executable_capacity);
    if (executable == NULL
        || _NSGetExecutablePath(
            executable, &executable_capacity
        ) != 0) {
        free(executable);
        return 0;
    }
    resolved = realpath(executable, NULL);
    free(executable);
    if (resolved == NULL) {
        return 0;
    }
    bundle_suffix = strstr(resolved, ".app/Contents/MacOS/");
    if (bundle_suffix != NULL) {
        const size_t bundle_length =
            (size_t)(bundle_suffix - resolved) + 4u;
        bundle_path = (char *)malloc(bundle_length + 1u);
        if (bundle_path != NULL) {
            memcpy(bundle_path, resolved, bundle_length);
            bundle_path[bundle_length] = '\0';
        }
    }
    free(resolved);
    if (bundle_path == NULL) {
        return 0;
    }
    account = getpwuid(getuid());
    if (account == NULL || account->pw_dir == NULL) {
        free(bundle_path);
        return 0;
    }
    if (strncmp(bundle_path, "/Applications/", 14u) != 0) {
        char user_applications[4096];
        const int user_applications_length = snprintf(
            user_applications,
            sizeof(user_applications),
            "%s/Applications/",
            account->pw_dir
        );
        if (user_applications_length <= 0
            || (size_t)user_applications_length
                >= sizeof(user_applications)
            || strncmp(
                bundle_path,
                user_applications,
                (size_t)user_applications_length
            ) != 0) {
            free(bundle_path);
            return 1;
        }
    }
    bundle_url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        (const UInt8 *)bundle_path,
        (CFIndex)strlen(bundle_path),
        true
    );
    if (bundle_url != NULL) {
        registered = LSRegisterURL(bundle_url, true) == noErr;
        CFRelease(bundle_url);
    }
    {
        const int desktop_path_length = snprintf(
            desktop_path,
            sizeof(desktop_path),
            "%s/Desktop/xlsOne.app",
            account->pw_dir
        );
        if (desktop_path_length > 0
            && (size_t)desktop_path_length < sizeof(desktop_path)) {
            struct stat existing;
            if (lstat(desktop_path, &existing) == 0) {
                shortcut_ready = 1;
            } else if (errno == ENOENT
                && symlink(bundle_path, desktop_path) == 0) {
                shortcut_ready = 1;
            }
        }
    }
    free(bundle_path);
    return registered && shortcut_ready;
#else
    return 1;
#endif
}

int xls_platform_http_request(
    const char *method,
    const char *url,
    const char *json_body,
    char **response_body,
    long *status_code
)
{
    *response_body = NULL;
    *status_code = 0;
#if defined(_WIN32)
    wchar_t *wide_url = wide_from_utf8(url);
    wchar_t *wide_method = wide_from_utf8(method);
    URL_COMPONENTSW parts;
    wchar_t host[256];
    wchar_t path[2048];
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    DWORD status_size = sizeof(DWORD);
    DWORD windows_status = 0;
    DWORD available = 0;
    char *body = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    int success = 0;
    if (wide_url == NULL || wide_method == NULL) {
        free(wide_url);
        free(wide_method);
        return 0;
    }
    memset(&parts, 0, sizeof(parts));
    memset(host, 0, sizeof(host));
    memset(path, 0, sizeof(path));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    if (!WinHttpCrackUrl(wide_url, 0, 0, &parts)) {
        goto cleanup;
    }
    session = WinHttpOpen(
        L"xlsOne/" XLSONE_WIDE_TEXT(XLSONE_VERSION),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (session == NULL) {
        goto cleanup;
    }
    (void)WinHttpSetTimeouts(session, 5000, 5000, 10000, 12000);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == NULL) {
        goto cleanup;
    }
    request = WinHttpOpenRequest(
        connection,
        wide_method,
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0
    );
    if (request == NULL) {
        goto cleanup;
    }
    if (!WinHttpSendRequest(
        request,
        json_body == NULL
            ? WINHTTP_NO_ADDITIONAL_HEADERS
            : L"Content-Type: application/json\r\nAccept: application/json",
        json_body == NULL ? 0u : (DWORD)-1L,
        json_body == NULL ? WINHTTP_NO_REQUEST_DATA : (LPVOID)json_body,
        json_body == NULL ? 0u : (DWORD)strlen(json_body),
        json_body == NULL ? 0u : (DWORD)strlen(json_body),
        0
    ) || !WinHttpReceiveResponse(request, NULL)) {
        goto cleanup;
    }
    if (!WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &windows_status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    )) {
        goto cleanup;
    }
    while (WinHttpQueryDataAvailable(request, &available) && available > 0u) {
        char *chunk = (char *)malloc((size_t)available);
        DWORD read_count = 0;
        int appended;
        if (chunk == NULL
            || !WinHttpReadData(request, chunk, available, &read_count)) {
            free(chunk);
            goto cleanup;
        }
        appended = append_http_bytes(
            &body, &length, &capacity, chunk, (size_t)read_count
        );
        free(chunk);
        if (!appended) {
            goto cleanup;
        }
    }
    if (body == NULL) {
        body = duplicate_text("");
        if (body == NULL) {
            goto cleanup;
        }
    }
    *response_body = body;
    body = NULL;
    *status_code = (long)windows_status;
    success = 1;

cleanup:
    free(body);
    if (request != NULL) WinHttpCloseHandle(request);
    if (connection != NULL) WinHttpCloseHandle(connection);
    if (session != NULL) WinHttpCloseHandle(session);
    free(wide_url);
    free(wide_method);
    return success;
#else
    return capture_curl(method, url, json_body, response_body, status_code);
#endif
}

size_t xls_platform_device_components(
    char components[][256],
    size_t capacity
)
{
    size_t count = 0u;
#if defined(_WIN32)
    static const wchar_t *const commands[] = {
        L"powershell.exe -NoProfile -NonInteractive -Command "
            L"\"(Get-CimInstance Win32_BaseBoard).SerialNumber\"",
        L"powershell.exe -NoProfile -NonInteractive -Command "
            L"\"(Get-CimInstance Win32_Processor).ProcessorId\"",
        L"powershell.exe -NoProfile -NonInteractive -Command "
            L"\"(Get-CimInstance Win32_DiskDrive | Select-Object -First 1).SerialNumber\""
    };
    size_t index;
    for (index = 0u; index < sizeof(commands) / sizeof(commands[0])
        && count < capacity; ++index) {
        char *value = read_windows_command(commands[index]);
        if (value != NULL && value[0] != '\0') {
            (void)snprintf(components[count++], 256u, "%s", value);
        }
        free(value);
    }
    if (count < capacity) {
        HKEY key;
        wchar_t guid[256];
        DWORD bytes = (DWORD)sizeof(guid);
        DWORD type = 0;
        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Cryptography",
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &key
        ) == ERROR_SUCCESS) {
            if (RegQueryValueExW(
                key,
                L"MachineGuid",
                NULL,
                &type,
                (LPBYTE)guid,
                &bytes
            ) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                char *utf8;
                guid[(sizeof(guid) / sizeof(guid[0])) - 1u] = L'\0';
                utf8 = utf8_from_wide_service(guid);
                if (utf8 != NULL && utf8[0] != '\0') {
                    (void)snprintf(components[count++], 256u, "%s", utf8);
                }
                free(utf8);
            }
            RegCloseKey(key);
        }
    }
#elif defined(__APPLE__)
    char *uuid = read_command_output_service(
        "ioreg -rd1 -c IOPlatformExpertDevice 2>/dev/null | "
        "awk -F'\\\"' '/IOPlatformUUID/ { print $(NF-1); exit }'"
    );
    if (uuid != NULL && uuid[0] != '\0' && capacity > 0u) {
        (void)snprintf(components[count++], 256u, "%s", uuid);
    }
    free(uuid);
#else
    FILE *file = fopen("/etc/machine-id", "rb");
    if (file != NULL && capacity > 0u) {
        if (fgets(components[count], 256, file) != NULL) {
            size_t length = strlen(components[count]);
            while (length > 0u
                && (components[count][length - 1u] == '\n'
                    || components[count][length - 1u] == '\r')) {
                components[count][--length] = '\0';
            }
            if (length > 0u) {
                ++count;
            }
        }
        fclose(file);
    }
#endif
    return count;
}

int xls_platform_host_name(char *buffer, size_t capacity)
{
    if (capacity == 0u) {
        return 0;
    }
#if defined(_WIN32)
    wchar_t wide[256];
    DWORD size = (DWORD)(sizeof(wide) / sizeof(wide[0]));
    char *utf8;
    if (!GetComputerNameW(wide, &size)) {
        buffer[0] = '\0';
        return 0;
    }
    wide[(sizeof(wide) / sizeof(wide[0])) - 1u] = L'\0';
    utf8 = utf8_from_wide_service(wide);
    if (utf8 == NULL) {
        buffer[0] = '\0';
        return 0;
    }
    (void)snprintf(buffer, capacity, "%s", utf8);
    free(utf8);
    return 1;
#else
    if (gethostname(buffer, capacity) != 0) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[capacity - 1u] = '\0';
    return 1;
#endif
}

int xls_platform_read_text_file(const char *path, char **contents)
{
    FILE *file;
    long size;
    char *buffer;
    size_t read_count;
    *contents = NULL;
#if defined(_WIN32)
    wchar_t *wide = wide_from_utf8(path);
    if (wide == NULL) {
        return 0;
    }
    file = _wfopen(wide, L"rb");
    free(wide);
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0
        || (size = ftell(file)) < 0
        || size > (long)XLSONE_HTTP_RESPONSE_LIMIT
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    buffer = (char *)malloc((size_t)size + 1u);
    if (buffer == NULL) {
        fclose(file);
        return 0;
    }
    read_count = fread(buffer, 1u, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        free(buffer);
        return 0;
    }
    buffer[read_count] = '\0';
    *contents = buffer;
    return 1;
}

int xls_platform_write_text_file(const char *path, const char *contents)
{
    FILE *file;
    const size_t length = contents == NULL ? 0u : strlen(contents);
    int result;
#if defined(_WIN32)
    wchar_t *wide = wide_from_utf8(path);
    if (wide == NULL) {
        return 0;
    }
    file = _wfopen(wide, L"wb");
    free(wide);
#else
    file = fopen(path, "wb");
#endif
    if (file == NULL) {
        return 0;
    }
    result = length == 0u || fwrite(contents, 1u, length, file) == length;
    return fclose(file) == 0 && result;
}

int xls_platform_read_license(char **contents, int64_t *last_seen_utc)
{
    char *file_contents;
    char *line_end;
    char path[2048];
    *contents = NULL;
    *last_seen_utc = 0;
#if defined(_WIN32)
    {
        wchar_t wide_path[2048];
        char *utf8_path;
        if (!windows_license_path(
            wide_path, sizeof(wide_path) / sizeof(wide_path[0])
        )) {
            return 0;
        }
        utf8_path = utf8_from_wide_service(wide_path);
        if (utf8_path == NULL) {
            return 0;
        }
        (void)snprintf(path, sizeof(path), "%s", utf8_path);
        free(utf8_path);
    }
#else
    if (!posix_license_path(path, sizeof(path))) {
        return 0;
    }
#endif
    if (!xls_platform_read_text_file(path, &file_contents)) {
        return 0;
    }
    line_end = strchr(file_contents, '\n');
    if (line_end == NULL) {
        free(file_contents);
        return 0;
    }
    *line_end = '\0';
    *last_seen_utc = (int64_t)strtoll(file_contents, NULL, 10);
    *contents = duplicate_text(line_end + 1);
    free(file_contents);
    return *contents != NULL;
}

int xls_platform_write_license(const char *contents, int64_t last_seen_utc)
{
    FILE *file;
    int result;
#if defined(_WIN32)
    wchar_t path[2048];
    wchar_t temporary[2080];
    if (!windows_license_path(path, sizeof(path) / sizeof(path[0]))
        || _snwprintf_s(
            temporary,
            sizeof(temporary) / sizeof(temporary[0]),
            _TRUNCATE,
            L"%ls.tmp",
            path
        ) < 0) {
        return 0;
    }
    file = _wfopen(temporary, L"wb");
#else
    char path[2048];
    char temporary[2080];
    if (!posix_license_path(path, sizeof(path))
        || snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0) {
        return 0;
    }
    file = fopen(temporary, "wb");
#endif
    if (file == NULL) {
        return 0;
    }
    result = fprintf(
        file,
        "%lld\n%s",
        (long long)last_seen_utc,
        contents == NULL ? "" : contents
    ) >= 0;
    result = fclose(file) == 0 && result;
#if defined(_WIN32)
    if (result) {
        result = MoveFileExW(
            temporary,
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) != 0;
    }
    if (!result) {
        (void)DeleteFileW(temporary);
    }
#else
    if (result) {
        result = rename(temporary, path) == 0;
    }
    if (!result) {
        (void)unlink(temporary);
    }
#endif
    return result;
}

static int rules_path(char *path, size_t capacity)
{
#if defined(_WIN32)
    wchar_t wide_path[2048];
    wchar_t *filename;
    char *utf8_path;
    if (!windows_license_path(
        wide_path, sizeof(wide_path) / sizeof(wide_path[0])
    )) {
        return 0;
    }
    filename = wcsrchr(wide_path, L'\\');
    if (filename == NULL
        || wcscpy_s(
            filename + 1,
            (sizeof(wide_path) / sizeof(wide_path[0]))
                - (size_t)(filename + 1 - wide_path),
            L"rules.json"
        ) != 0) {
        return 0;
    }
    utf8_path = utf8_from_wide_service(wide_path);
    if (utf8_path == NULL) {
        return 0;
    }
    (void)snprintf(path, capacity, "%s", utf8_path);
    free(utf8_path);
    return path[0] != '\0';
#else
    char *filename;
    if (!posix_license_path(path, capacity)) {
        return 0;
    }
    filename = strrchr(path, '/');
    if (filename == NULL
        || snprintf(
            filename + 1,
            capacity - (size_t)(filename + 1 - path),
            "rules.json"
        ) < 0) {
        return 0;
    }
    return 1;
#endif
}

int xls_platform_read_rules(char **contents)
{
    char path[2048];
    *contents = NULL;
    return rules_path(path, sizeof(path))
        && xls_platform_read_text_file(path, contents);
}

int xls_platform_write_rules(const char *contents)
{
    char path[2048];
    return rules_path(path, sizeof(path))
        && xls_platform_write_text_file(path, contents);
}
