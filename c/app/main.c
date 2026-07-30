#include "xlsone/xlsone.h"
#include "drop_paths.h"
#include "i18n.h"
#include "license_manager.h"
#include "platform_dialog.h"
#include "platform_drop.h"
#include "sheet_tabs.h"
#include "source_list.h"
#include "update_checker.h"

#include <SDL.h>
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_sdl_renderer.h"

#if !defined(XLSONE_VERSION)
#define XLSONE_VERSION "development"
#endif

typedef enum app_menu {
    APP_MENU_NONE,
    APP_MENU_FILE,
    APP_MENU_EDIT,
    APP_MENU_RULES,
    APP_MENU_LICENSE,
    APP_MENU_LANGUAGE,
    APP_MENU_HELP
} app_menu;

typedef enum app_dialog {
    APP_DIALOG_NONE,
    APP_DIALOG_LICENSE,
    APP_DIALOG_RULES,
    APP_DIALOG_NOTICE,
    APP_DIALOG_UPDATE
} app_dialog;

typedef enum app_drop_feedback {
    APP_DROP_IDLE,
    APP_DROP_HOVER,
    APP_DROP_IMPORTING,
    APP_DROP_SUCCESS,
    APP_DROP_REJECTED
} app_drop_feedback;

typedef enum app_update_status {
    APP_UPDATE_AVAILABLE,
    APP_UPDATE_CURRENT,
    APP_UPDATE_FAILED
} app_update_status;

typedef struct app_update_result {
    app_update_status status;
    xls_update_info info;
} app_update_result;

typedef struct app_update_thread_context {
    Uint32 event_type;
    app_update_result *result;
} app_update_thread_context;

typedef struct app_state {
    xls_workbook *workbooks;
    size_t workbook_count;
    xls_validation_report validation;
    xls_merged_sheet *merged_sheets;
    size_t merged_sheet_count;
    size_t selected_sheet;
    size_t first_visible_sheet;
    size_t selected_row;
    size_t selected_column;
    int has_selection;
    size_t first_visible_row;
    size_t first_visible_column;
    int sources_expanded;
    double source_scroll_offset;
    double source_scroll_maximum;
    double source_scroll_viewport_height;
    int source_scroll_selection_valid;
    int source_scroll_dragging;
    double source_scroll_drag_anchor;
    size_t source_scroll_sheet;
    size_t source_scroll_row;
    size_t source_scroll_column;
    Uint64 interaction_feedback_until;
    unsigned int source_reveal_attempt_count;
    unsigned int source_reveal_failure_count;
    app_drop_feedback drop_feedback;
    Uint64 drop_feedback_until;
    char **pending_drop_paths;
    size_t pending_drop_count;
    size_t pending_drop_capacity;
    int pending_drop_complete;
    size_t last_drop_count;
    int running;
    app_menu active_menu;
    app_dialog dialog;
    int license_page;
    char license_key[32];
    char dialog_title[128];
    char dialog_message[1024];
    int dialog_error;
    char update_version[64];
    char update_changelog[1024];
    char update_download_url[1024];
    SDL_Thread *update_thread;
    Uint32 update_event_type;
    int update_check_silent;
    int automatic_update_pending;
    Uint64 automatic_update_at;
    int language_index;
    int language_layout_dirty;
    int language_minimum_width;
    SDL_Window *window;
    int has_last_override;
    size_t last_override_sheet;
    size_t last_override_row;
    size_t last_override_column;
    xls_cell_kind last_override_kind;
    xls_license_manager license;
    char status[512];
} app_state;

typedef struct ui_fonts {
    const struct nk_user_font *body;
    const struct nk_user_font *title;
    const struct nk_user_font *numeric;
    const struct nk_user_font *source_value;
} ui_fonts;

#define UI_MENU_HEIGHT 26.0f
#define UI_TOOLBAR_TOP UI_MENU_HEIGHT
#define UI_TOOLBAR_BOTTOM (UI_MENU_HEIGHT + 40.0f)
#define UI_SHEET_BOTTOM (UI_TOOLBAR_BOTTOM + 78.0f)
#define UI_SOURCE_ROW_HEIGHT 56.0

static int ui_input_blocked = 0;

static float app_body_font_size(void)
{
#if defined(__linux__)
    return 18.0f;
#elif defined(_WIN32)
    return 14.0f;
#else
    return 12.0f;
#endif
}

static float app_title_font_size(void)
{
#if defined(__linux__)
    return 27.0f;
#elif defined(_WIN32)
    return 22.0f;
#else
    return 20.0f;
#endif
}

static float app_numeric_font_size(void)
{
#if defined(__linux__)
    return 26.0f;
#elif defined(_WIN32)
    return 22.0f;
#else
    return 20.0f;
#endif
}

static float app_source_font_size(void)
{
#if defined(__linux__)
    return 19.0f;
#elif defined(_WIN32)
    return 15.0f;
#else
    return 14.0f;
#endif
}

static const char *app_tr(const char *source)
{
    return xls_i18n_translate(source);
}

static void app_set_status(app_state *app, const char *message)
{
    (void)snprintf(
        app->status,
        sizeof(app->status),
        "%s",
        app_tr(message == NULL ? "" : message)
    );
}

static void app_set_interaction_feedback(
    app_state *app,
    const char *message
)
{
    app_set_status(app, message);
    app->interaction_feedback_until = SDL_GetTicks64() + 1600u;
}

static const char *app_license_watermark(
    const xls_license_manager *manager
)
{
    if (xls_license_is_full(manager)) {
        return "";
    }
    return app_tr(
        manager->state == XLS_LICENSE_EXPIRED
            ? "授权已过期 — xlsOne"
            : "未激活试用版 — xlsOne"
    );
}

static const char *app_license_state_text(
    const xls_license_manager *manager,
    char *buffer,
    size_t capacity
)
{
    const int remaining = xls_license_remaining_days(manager);
    const int grace = xls_license_grace_days(manager);
    if (manager->state == XLS_LICENSE_ACTIVATED) {
        if (grace > 0) {
            (void)snprintf(
                buffer,
                capacity,
                app_tr("宽限期 · 剩余 %d 天"),
                grace
            );
        } else if (remaining > 0
            && manager->info.plan == XLS_LICENSE_PLAN_PERSONAL_YEARLY) {
            (void)snprintf(
                buffer,
                capacity,
                app_tr("已激活 · 剩余 %d 天"),
                remaining
            );
        } else {
            (void)snprintf(buffer, capacity, "%s", app_tr("已激活"));
        }
    } else if (manager->state == XLS_LICENSE_TRIAL) {
        if (remaining > 0) {
            (void)snprintf(
                buffer,
                capacity,
                app_tr("试用期 · 剩余 %d 天"),
                remaining
            );
        } else if (grace > 0) {
            (void)snprintf(
                buffer,
                capacity,
                app_tr("试用宽限 · 剩余 %d 天"),
                grace
            );
        } else {
            (void)snprintf(buffer, capacity, "%s", app_tr("试用期"));
        }
    } else if (manager->state == XLS_LICENSE_EXPIRED) {
        (void)snprintf(buffer, capacity, "%s", app_tr("已过期"));
    } else {
        (void)snprintf(
            buffer,
            capacity,
            "%s",
            app_tr("未授权 · 功能受限")
        );
    }
    return buffer;
}

static int app_language_preference_path(
    char *path,
    size_t capacity
)
{
    char *directory = SDL_GetPrefPath("Z-Pulse", "xlsOne");
    int result;
    if (directory == NULL) {
        return 0;
    }
    result = snprintf(
        path, capacity, "%slanguage.preference", directory
    );
    SDL_free(directory);
    return result > 0 && (size_t)result < capacity;
}

static xls_ui_language app_effective_language(
    xls_ui_language preference
)
{
    SDL_Locale *locales;
    char locale[96] = "";
    xls_ui_language effective;
    if (preference != XLS_UI_LANGUAGE_SYSTEM) {
        return preference;
    }
    locales = SDL_GetPreferredLocales();
    if (locales != NULL && locales[0].language != NULL) {
        if (locales[0].country != NULL && locales[0].country[0] != '\0') {
            (void)snprintf(
                locale,
                sizeof(locale),
                "%s-%s",
                locales[0].language,
                locales[0].country
            );
        } else {
            (void)snprintf(
                locale, sizeof(locale), "%s", locales[0].language
            );
        }
    }
    effective = xls_i18n_resolve_language(preference, locale);
    SDL_free(locales);
    return effective;
}

static void app_load_language(app_state *app)
{
    char path[2048];
    const char *override = getenv("XLSONE_UI_LANGUAGE");
    xls_ui_language preference = XLS_UI_LANGUAGE_SYSTEM;
    xls_ui_language parsed;
    if (app_language_preference_path(path, sizeof(path))) {
        (void)xls_i18n_read_preference(path, &preference);
    }
    if (override != NULL
        && xls_i18n_parse_language(override, &parsed)) {
        preference = parsed;
    }
    app->language_index = (int)preference;
    xls_i18n_set_language(app_effective_language(preference));
}

static int app_store_language(xls_ui_language preference)
{
    char path[2048];
    return app_language_preference_path(path, sizeof(path))
        && xls_i18n_write_preference(path, preference);
}

static void app_free_results(app_state *app)
{
    size_t index;
    for (index = 0; index < app->merged_sheet_count; ++index) {
        xls_merged_sheet_free(&app->merged_sheets[index]);
    }
    free(app->merged_sheets);
    app->merged_sheets = NULL;
    app->merged_sheet_count = 0;
    xls_validation_report_free(&app->validation);
    app->selected_sheet = 0;
    app->first_visible_sheet = 0;
    app->has_selection = 0;
    app->first_visible_row = 0;
    app->first_visible_column = 0;
    app->source_scroll_offset = 0.0;
    app->source_scroll_maximum = 0.0;
    app->source_scroll_viewport_height = 0.0;
    app->source_scroll_selection_valid = 0;
    app->source_scroll_dragging = 0;
    app->has_last_override = 0;
}

static void app_rule_signature(
    const app_state *app,
    char signature[17]
)
{
    unsigned long long hash = 1469598103934665603ULL;
    size_t sheet_index;
    for (sheet_index = 0u;
         sheet_index < app->merged_sheet_count;
         ++sheet_index) {
        const xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        const unsigned char *cursor =
            (const unsigned char *)(sheet->sheet_name == NULL
                ? ""
                : sheet->sheet_name);
        while (*cursor != '\0') {
            hash ^= (unsigned long long)*cursor++;
            hash *= 1099511628211ULL;
        }
        hash ^= (unsigned long long)sheet->row_count;
        hash *= 1099511628211ULL;
        hash ^= (unsigned long long)sheet->column_count;
        hash *= 1099511628211ULL;
    }
    (void)snprintf(signature, 17u, "%016llx", hash);
}

static xls_cell_kind app_kind_from_rule(const char *text)
{
    if (text != NULL && strcmp(text, "sum") == 0) {
        return XLS_CELL_SUM;
    }
    if (text != NULL && strcmp(text, "mixed") == 0) {
        return XLS_CELL_MIXED;
    }
    if (text != NULL && strcmp(text, "single") == 0) {
        return XLS_CELL_SINGLE;
    }
    return XLS_CELL_LABEL;
}

static cJSON *app_find_rule_schema(
    const cJSON *root,
    const char *signature
)
{
    cJSON *schemas = cJSON_IsObject(root)
        ? cJSON_GetObjectItemCaseSensitive(root, "schemas")
        : NULL;
    cJSON *schema;
    if (!cJSON_IsArray(schemas)) {
        return NULL;
    }
    cJSON_ArrayForEach(schema, schemas) {
        cJSON *stored = cJSON_IsObject(schema)
            ? cJSON_GetObjectItemCaseSensitive(schema, "signature")
            : NULL;
        if (stored != NULL
            && cJSON_IsString(stored)
            && stored->valuestring != NULL
            && strcmp(stored->valuestring, signature) == 0) {
            return schema;
        }
    }
    return NULL;
}

static size_t app_apply_saved_rules(app_state *app)
{
    char *json = NULL;
    char signature[17];
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *schema;
    cJSON *overrides;
    cJSON *item;
    size_t applied = 0u;
    if (app->merged_sheet_count == 0u
        || !xls_platform_read_rules(&json)) {
        return 0u;
    }
    root = cJSON_ParseWithLengthOpts(
        json, strlen(json) + 1u, &parse_end, 1
    );
    free(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return 0u;
    }
    app_rule_signature(app, signature);
    schema = app_find_rule_schema(root, signature);
    overrides = schema == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(schema, "overrides");
    if (!cJSON_IsArray(overrides)) {
        cJSON_Delete(root);
        return 0u;
    }
    cJSON_ArrayForEach(item, overrides) {
        cJSON *sheet_name = cJSON_IsObject(item)
            ? cJSON_GetObjectItemCaseSensitive(item, "sheetName")
            : NULL;
        cJSON *row_item = cJSON_IsObject(item)
            ? cJSON_GetObjectItemCaseSensitive(item, "row")
            : NULL;
        cJSON *column_item = cJSON_IsObject(item)
            ? cJSON_GetObjectItemCaseSensitive(item, "column")
            : NULL;
        cJSON *type_item = cJSON_IsObject(item)
            ? cJSON_GetObjectItemCaseSensitive(item, "type")
            : NULL;
        size_t sheet_index;
        if (!cJSON_IsString(sheet_name)
            || !cJSON_IsNumber(row_item)
            || !cJSON_IsNumber(column_item)
            || !cJSON_IsString(type_item)
            || row_item->valuedouble < 0.0
            || column_item->valuedouble < 0.0) {
            continue;
        }
        for (sheet_index = 0u;
             sheet_index < app->merged_sheet_count;
             ++sheet_index) {
            xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
            size_t row;
            size_t column;
            xls_merged_cell *cell;
            xls_error error;
            if (sheet->sheet_name == NULL
                || strcmp(sheet->sheet_name, sheet_name->valuestring) != 0
                || row_item->valuedouble >= (double)sheet->row_count
                || column_item->valuedouble >= (double)sheet->column_count) {
                continue;
            }
            row = (size_t)row_item->valuedouble;
            column = (size_t)column_item->valuedouble;
            if ((double)row != row_item->valuedouble
                || (double)column != column_item->valuedouble) {
                continue;
            }
            cell = xls_merged_sheet_cell_mutable(sheet, row, column);
            if (cell != NULL
                && xls_merged_cell_set_kind(
                    cell,
                    app_kind_from_rule(type_item->valuestring),
                    &error
                )) {
                ++applied;
            }
            break;
        }
    }
    cJSON_Delete(root);
    return applied;
}

static void app_clear(app_state *app)
{
    size_t index;
    app_free_results(app);
    for (index = 0; index < app->workbook_count; ++index) {
        xls_workbook_free(&app->workbooks[index]);
    }
    free(app->workbooks);
    app->workbooks = NULL;
    app->workbook_count = 0;
    app_set_status(app, "已清空。可拖入或打开多个 Excel 工作簿。");
}

static int app_recompute(app_state *app)
{
    xls_error error;
    size_t index;
    size_t applied_rules;
    app_free_results(app);
    if (app->workbook_count == 0) {
        return 1;
    }
    if (!xls_validate_workbooks(
        app->workbooks,
        app->workbook_count,
        &app->validation,
        &error
    )) {
        app_set_status(app, error.message);
        return 0;
    }
    if (app->validation.readiness != XLS_MERGE_READY) {
        app_set_status(
            app,
            app->validation.issue_count > 0
                ? app->validation.issues[0].message
                : "没有结构一致、可合并的工作表。"
        );
        return 1;
    }
    app->merged_sheet_count = app->validation.common_sheet_count;
    app->merged_sheets = (xls_merged_sheet *)calloc(
        app->merged_sheet_count, sizeof(*app->merged_sheets)
    );
    if (app->merged_sheets == NULL) {
        app->merged_sheet_count = 0;
        app_set_status(app, "内存不足，无法生成汇总结果。");
        return 0;
    }
    for (index = 0; index < app->merged_sheet_count; ++index) {
        if (!xls_merge_sheet(
            app->workbooks,
            app->workbook_count,
            app->validation.common_sheet_names[index],
            &app->merged_sheets[index],
            &error
        )) {
            app_set_status(app, error.message);
            return 0;
        }
    }
    applied_rules = app_apply_saved_rules(app);
    if (applied_rules > 0u) {
        (void)snprintf(
            app->status,
            sizeof(app->status),
            app_tr("已导入 %zu 个工作簿，识别 %zu 个可汇总工作表，并应用 %zu 条修正规则。"),
            app->workbook_count,
            app->merged_sheet_count,
            applied_rules
        );
    } else {
        (void)snprintf(
            app->status,
            sizeof(app->status),
            app_tr("已导入 %zu 个工作簿，识别 %zu 个可汇总工作表。"),
            app->workbook_count,
            app->merged_sheet_count
        );
    }
    return 1;
}

static int app_has_path(const app_state *app, const char *path)
{
    size_t index;
    for (index = 0; index < app->workbook_count; ++index) {
        if (app->workbooks[index].filepath != NULL
            && strcmp(app->workbooks[index].filepath, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int app_add_path(app_state *app, const char *path)
{
    xls_workbook parsed;
    xls_workbook *replacement;
    xls_error error;
    char *absolute_path = NULL;
    const char *import_path = path;
    if (xls_platform_absolute_path(path, &absolute_path)) {
        import_path = absolute_path;
    }
    if (app_has_path(app, import_path)) {
        free(absolute_path);
        return 1;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (!xls_parse_file(import_path, &parsed, &error)) {
        fprintf(
            stderr,
            "could not open workbook \"%s\": %s\n",
            import_path,
            error.message
        );
        app_set_status(app, error.message);
        free(absolute_path);
        return 0;
    }
    free(absolute_path);
    replacement = (xls_workbook *)realloc(
        app->workbooks,
        (app->workbook_count + 1) * sizeof(*app->workbooks)
    );
    if (replacement == NULL) {
        xls_workbook_free(&parsed);
        app_set_status(app, "内存不足，无法添加工作簿。");
        return 0;
    }
    app->workbooks = replacement;
    app->workbooks[app->workbook_count++] = parsed;
    return 1;
}

typedef struct app_drop_import {
    app_state *app;
    int changed;
    int failed;
    int license_blocked;
} app_drop_import;

static void app_show_license(app_state *app);

static int app_import_dropped_path(
    const char *path,
    void *user_data
)
{
    app_drop_import *import = (app_drop_import *)user_data;
    const size_t before = import->app->workbook_count;
    if (before >= (size_t)xls_license_max_import_files(
        &import->app->license
    )) {
        import->license_blocked = 1;
        return 0;
    }
    if (!app_add_path(import->app, path)) {
        import->failed = 1;
    } else if (import->app->workbook_count > before) {
        import->changed = 1;
    }
    return 1;
}

static void app_finish_drop(app_drop_import *import)
{
    if (import->changed) {
        (void)app_recompute(import->app);
    }
    if (import->license_blocked) {
        app_set_status(
            import->app,
            "未授权时最多处理 3 个文件；请激活或开始免费试用。"
        );
        app_show_license(import->app);
    }
}

static void app_clear_pending_drop(app_state *app)
{
    size_t index;
    for (index = 0u; index < app->pending_drop_count; ++index) {
        free(app->pending_drop_paths[index]);
    }
    free(app->pending_drop_paths);
    app->pending_drop_paths = NULL;
    app->pending_drop_count = 0u;
    app->pending_drop_capacity = 0u;
    app->pending_drop_complete = 0;
}

static int app_queue_dropped_path(
    const char *path,
    void *user_data
)
{
    app_state *app = (app_state *)user_data;
    char **replacement;
    char *copy;
    size_t length;
    size_t capacity;
    if (path == NULL || !xls_drop_path_is_workbook(path)) {
        return 0;
    }
    if (app->pending_drop_count == app->pending_drop_capacity) {
        capacity = app->pending_drop_capacity == 0u
            ? 4u
            : app->pending_drop_capacity * 2u;
        replacement = (char **)realloc(
            app->pending_drop_paths,
            capacity * sizeof(*replacement)
        );
        if (replacement == NULL) {
            app_set_status(app, "内存不足，无法添加工作簿。");
            return 0;
        }
        app->pending_drop_paths = replacement;
        app->pending_drop_capacity = capacity;
    }
    length = strlen(path);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) {
        app_set_status(app, "内存不足，无法添加工作簿。");
        return 0;
    }
    memcpy(copy, path, length + 1u);
    app->pending_drop_paths[app->pending_drop_count++] = copy;
    app->drop_feedback = APP_DROP_IMPORTING;
    app->drop_feedback_until = 0u;
    return 1;
}

static void app_process_pending_drop(app_state *app)
{
    app_drop_import import;
    size_t index;
    const size_t queued = app->pending_drop_count;
    if (!app->pending_drop_complete || queued == 0u) {
        return;
    }
    memset(&import, 0, sizeof(import));
    import.app = app;
    for (index = 0u; index < queued; ++index) {
        (void)app_import_dropped_path(
            app->pending_drop_paths[index], &import
        );
    }
    app_clear_pending_drop(app);
    app_finish_drop(&import);
    app->last_drop_count = queued;
    app->drop_feedback = import.license_blocked || import.failed
        ? APP_DROP_REJECTED
        : APP_DROP_SUCCESS;
    app->drop_feedback_until = SDL_GetTicks64() + 1400u;
}

static void app_show_license(app_state *app)
{
    app->active_menu = APP_MENU_NONE;
    app->dialog = APP_DIALOG_LICENSE;
    app->license_page = 0;
    app->dialog_message[0] = '\0';
    app->dialog_error = 0;
    SDL_StartTextInput();
}

static void app_close_dialog(app_state *app)
{
    app->dialog = APP_DIALOG_NONE;
    app->dialog_message[0] = '\0';
    SDL_StopTextInput();
}

static void app_open_files(app_state *app, int append)
{
    char **paths = NULL;
    size_t path_count = 0;
    size_t index;
    int changed = 0;
    if (!xls_platform_open_files(&paths, &path_count)) {
        return;
    }
    if (path_count + (append ? app->workbook_count : 0u)
        > (size_t)xls_license_max_import_files(&app->license)) {
        xls_platform_free_paths(paths, path_count);
        app_set_status(
            app,
            "未授权时每次最多处理 3 个文件；请激活或开始免费试用。"
        );
        app_show_license(app);
        return;
    }
    if (!append) {
        app_clear(app);
    }
    for (index = 0; index < path_count; ++index) {
        if (app_add_path(app, paths[index])) {
            changed = 1;
        }
    }
    xls_platform_free_paths(paths, path_count);
    if (changed) {
        (void)app_recompute(app);
    }
}

static int path_has_suffix(const char *path, const char *suffix)
{
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    return path_length >= suffix_length
        && strcmp(path + path_length - suffix_length, suffix) == 0;
}

static void app_export(app_state *app)
{
    char *path = NULL;
    xls_error error;
    int ok;
    if (app->merged_sheet_count == 0
        || !app->validation.has_template_workbook) {
        app_set_status(app, "当前没有可导出的汇总结果。");
        return;
    }
    if (!xls_platform_save_file(&path)) {
        return;
    }
    if (path_has_suffix(path, ".csv")) {
        ok = xls_export_csv(
            &app->merged_sheets[app->selected_sheet],
            path,
            app_license_watermark(&app->license),
            &error
        );
    } else {
        ok = xls_export_xlsx(
            app->workbooks[app->validation.template_workbook_index].filepath,
            app->merged_sheets,
            app->merged_sheet_count,
            path,
            app_license_watermark(&app->license),
            &error
        );
    }
    if (ok) {
        (void)snprintf(
            app->status,
            sizeof(app->status),
            app_tr("导出完成：%s"),
            path
        );
    } else {
        app_set_status(app, error.message);
    }
    free(path);
}

static void app_set_selected_kind(app_state *app, xls_cell_kind kind)
{
    xls_merged_cell *cell;
    xls_error error;
    if (!app->has_selection || app->selected_sheet >= app->merged_sheet_count) {
        return;
    }
    cell = xls_merged_sheet_cell_mutable(
        &app->merged_sheets[app->selected_sheet],
        app->selected_row,
        app->selected_column
    );
    if (cell == NULL) {
        app_set_status(app, "没有选中的汇总单元格。");
        return;
    }
    app->last_override_sheet = app->selected_sheet;
    app->last_override_row = app->selected_row;
    app->last_override_column = app->selected_column;
    app->last_override_kind = cell->kind;
    app->has_last_override = 1;
    if (!xls_merged_cell_set_kind(cell, kind, &error)) {
        app->has_last_override = 0;
        app_set_status(
            app,
            error.message
        );
        return;
    }
    app_set_status(app, "已修正当前单元格类型；导出时会使用修正后的值。");
}

static void app_restore_selected_kind(app_state *app)
{
    xls_merged_cell *cell;
    xls_error error;
    if (!app->has_selection || app->selected_sheet >= app->merged_sheet_count) {
        return;
    }
    cell = xls_merged_sheet_cell_mutable(
        &app->merged_sheets[app->selected_sheet],
        app->selected_row,
        app->selected_column
    );
    if (cell == NULL) {
        app_set_status(app, "没有选中的汇总单元格。");
        return;
    }
    app->last_override_sheet = app->selected_sheet;
    app->last_override_row = app->selected_row;
    app->last_override_column = app->selected_column;
    app->last_override_kind = cell->kind;
    app->has_last_override = 1;
    if (!xls_merged_cell_restore_automatic(cell, &error)) {
        app->has_last_override = 0;
        app_set_status(
            app,
            error.message
        );
        return;
    }
    app_set_status(app, "已恢复当前单元格的自动识别结果。");
}

static void app_clear_overrides(app_state *app)
{
    size_t sheet_index;
    size_t cell_index;
    xls_error error;
    for (sheet_index = 0; sheet_index < app->merged_sheet_count; ++sheet_index) {
        xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        for (cell_index = 0;
             cell_index < sheet->row_count * sheet->column_count;
             ++cell_index) {
            if (sheet->cells[cell_index].is_overridden != 0u
                && !xls_merged_cell_restore_automatic(
                    &sheet->cells[cell_index],
                    &error
                )) {
                app_set_status(app, error.message);
                return;
            }
        }
    }
    app->has_last_override = 0;
    app_set_status(app, "已清除全部手动修正。");
}

static void app_undo_last_override(app_state *app)
{
    xls_merged_cell *cell;
    xls_error error;
    if (!app->has_last_override
        || app->last_override_sheet >= app->merged_sheet_count) {
        app_set_status(app, "当前没有可撤销的修正。");
        return;
    }
    cell = xls_merged_sheet_cell_mutable(
        &app->merged_sheets[app->last_override_sheet],
        app->last_override_row,
        app->last_override_column
    );
    if (cell == NULL
        || !xls_merged_cell_set_kind(
            cell, app->last_override_kind, &error
        )) {
        app_set_status(
            app,
            cell == NULL ? "无法找到上次修正的单元格。" : error.message
        );
        return;
    }
    app->selected_sheet = app->last_override_sheet;
    app->first_visible_sheet = app->selected_sheet;
    app->selected_row = app->last_override_row;
    app->selected_column = app->last_override_column;
    app->has_selection = 1;
    app->has_last_override = 0;
    app_set_status(app, "已撤销上一次修正。");
}

static size_t app_override_count(const app_state *app)
{
    size_t sheet_index;
    size_t cell_index;
    size_t count = 0u;
    for (sheet_index = 0u;
         sheet_index < app->merged_sheet_count;
         ++sheet_index) {
        const xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        for (cell_index = 0u;
             cell_index < sheet->row_count * sheet->column_count;
             ++cell_index) {
            if (sheet->cells[cell_index].is_overridden != 0u) {
                ++count;
            }
        }
    }
    return count;
}

static void app_show_notice(
    app_state *app,
    const char *title,
    const char *message,
    int error
)
{
    app->active_menu = APP_MENU_NONE;
    app->dialog = APP_DIALOG_NOTICE;
    app->dialog_error = error;
    (void)snprintf(
        app->dialog_title,
        sizeof(app->dialog_title),
        "%s",
        app_tr(title == NULL ? "" : title)
    );
    (void)snprintf(
        app->dialog_message,
        sizeof(app->dialog_message),
        "%s",
        app_tr(message == NULL ? "" : message)
    );
}

static void app_show_rules(app_state *app)
{
    const size_t count = app_override_count(app);
    app->active_menu = APP_MENU_NONE;
    app->dialog = APP_DIALOG_RULES;
    app->dialog_error = 0;
    (void)snprintf(
        app->dialog_message,
        sizeof(app->dialog_message),
        count == 0u
            ? app_tr("当前工作区尚未产生手动修正。")
            : app_tr("当前工作区共有 %zu 条修正规则；保存后会自动用于同结构工作簿。"),
        count
    );
}

static void app_save_rules(app_state *app)
{
    char *existing_json = NULL;
    const char *parse_end = NULL;
    cJSON *root = NULL;
    cJSON *schemas;
    cJSON *schema;
    cJSON *overrides;
    char *json;
    char signature[17];
    size_t sheet_index;
    if (app_override_count(app) == 0u) {
        app_show_notice(
            app,
            "保存当前修正规则",
            "当前没有可保存的手动修正。",
            0
        );
        return;
    }
    if (xls_platform_read_rules(&existing_json)) {
        root = cJSON_ParseWithLengthOpts(
            existing_json,
            strlen(existing_json) + 1u,
            &parse_end,
            1
        );
        free(existing_json);
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
        if (root != NULL) {
            cJSON_AddNumberToObject(root, "version", 1);
        }
    }
    schemas = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "schemas");
    if (!cJSON_IsArray(schemas) && root != NULL) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "schemas");
        schemas = cJSON_AddArrayToObject(root, "schemas");
    }
    app_rule_signature(app, signature);
    schema = app_find_rule_schema(root, signature);
    if (schema == NULL && schemas != NULL) {
        schema = cJSON_CreateObject();
        if (schema != NULL) {
            cJSON_AddStringToObject(schema, "signature", signature);
            cJSON_AddStringToObject(
                schema,
                "name",
                app->merged_sheet_count > 0u
                    && app->merged_sheets[0].sheet_name != NULL
                    ? app->merged_sheets[0].sheet_name
                    : "当前工作区"
            );
            cJSON_AddItemToArray(schemas, schema);
        }
    }
    if (root == NULL || schemas == NULL || schema == NULL) {
        cJSON_Delete(root);
        app_show_notice(app, "保存失败", "内存不足，无法生成修正规则。", 1);
        return;
    }
    cJSON_DeleteItemFromObjectCaseSensitive(schema, "updated_at");
    cJSON_AddNumberToObject(schema, "updated_at", (double)time(NULL));
    cJSON_DeleteItemFromObjectCaseSensitive(schema, "overrides");
    overrides = cJSON_AddArrayToObject(schema, "overrides");
    if (overrides == NULL) {
        cJSON_Delete(root);
        app_show_notice(app, "保存失败", "内存不足，无法生成修正规则。", 1);
        return;
    }
    for (sheet_index = 0u;
         sheet_index < app->merged_sheet_count;
         ++sheet_index) {
        const xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        size_t row;
        for (row = 0u; row < sheet->row_count; ++row) {
            size_t column;
            for (column = 0u; column < sheet->column_count; ++column) {
                const xls_merged_cell *cell = xls_merged_sheet_cell(
                    sheet, row, column
                );
                cJSON *item;
                if (cell == NULL || cell->is_overridden == 0u) {
                    continue;
                }
                item = cJSON_CreateObject();
                if (item == NULL) {
                    continue;
                }
                cJSON_AddStringToObject(
                    item,
                    "sheetName",
                    sheet->sheet_name == NULL ? "" : sheet->sheet_name
                );
                cJSON_AddNumberToObject(item, "row", (double)row);
                cJSON_AddNumberToObject(item, "column", (double)column);
                cJSON_AddStringToObject(
                    item, "type", xls_cell_kind_name(cell->kind)
                );
                cJSON_AddItemToArray(overrides, item);
            }
        }
    }
    json = cJSON_Print(root);
    cJSON_Delete(root);
    if (json == NULL
        || !xls_platform_write_rules(json)) {
        cJSON_free(json);
        app_show_notice(app, "保存失败", "无法保存当前修正规则。", 1);
        return;
    }
    cJSON_free(json);
    app_set_status(
        app,
        "当前修正规则已保存；下次导入同结构工作簿时会自动应用。"
    );
}

static int app_update_worker(void *opaque)
{
    app_update_thread_context *thread_context =
        (app_update_thread_context *)opaque;
    app_update_result *result = thread_context->result;
    char *response = NULL;
    long http_status = 0;
    SDL_Event event;
    const Uint32 event_type = thread_context->event_type;
    free(thread_context);
    result->status = APP_UPDATE_FAILED;
    if (xls_platform_http_request(
        "GET",
        "https://z-pulse.cn/api/version",
        NULL,
        &response,
        &http_status
    )
        && http_status == 200
        && xls_update_parse_response(
            response,
            xls_update_platform_key(),
            &result->info
        )) {
        result->status = xls_update_compare_versions(
            result->info.latest_version, XLSONE_VERSION
        ) > 0
            ? APP_UPDATE_AVAILABLE
            : APP_UPDATE_CURRENT;
    }
    free(response);
    memset(&event, 0, sizeof(event));
    event.type = event_type;
    event.user.data1 = result;
    if (SDL_PushEvent(&event) <= 0) {
        free(result);
        return 1;
    }
    return 0;
}

static void app_show_update(app_state *app, const xls_update_info *info)
{
    app->active_menu = APP_MENU_NONE;
    app->dialog = APP_DIALOG_UPDATE;
    app->dialog_error = 0;
    (void)snprintf(
        app->update_version,
        sizeof(app->update_version),
        "%s",
        info->latest_version
    );
    (void)snprintf(
        app->update_changelog,
        sizeof(app->update_changelog),
        "%s",
        info->changelog
    );
    (void)snprintf(
        app->update_download_url,
        sizeof(app->update_download_url),
        "%s",
        info->download_url
    );
}

static void app_start_update_check(app_state *app, int silent)
{
    app_update_thread_context *thread_context;
    app_update_result *result;
    if (app->update_thread != NULL) {
        if (!silent) {
            app->update_check_silent = 0;
            app_show_notice(
                app,
                "检查更新",
                "正在检查更新...",
                0
            );
        }
        return;
    }
    if (app->update_event_type == (Uint32)-1) {
        if (!silent) {
            app_show_notice(
                app,
                "检查更新失败",
                "无法连接更新服务器，请稍后重试。",
                1
            );
        }
        return;
    }
    thread_context = (app_update_thread_context *)malloc(
        sizeof(*thread_context)
    );
    result = (app_update_result *)calloc(1u, sizeof(*result));
    if (thread_context == NULL || result == NULL) {
        free(thread_context);
        free(result);
        if (!silent) {
            app_show_notice(
                app,
                "检查更新失败",
                "无法连接更新服务器，请稍后重试。",
                1
            );
        }
        return;
    }
    thread_context->event_type = app->update_event_type;
    thread_context->result = result;
    app->update_check_silent = silent;
    if (!silent) {
        app_show_notice(
            app,
            "检查更新",
            "正在检查更新...",
            0
        );
    }
    app->update_thread = SDL_CreateThread(
        app_update_worker,
        "xlsOne update check",
        thread_context
    );
    if (app->update_thread == NULL) {
        free(result);
        free(thread_context);
        if (!silent) {
            app_show_notice(
                app,
                "检查更新失败",
                "无法连接更新服务器，请稍后重试。",
                1
            );
        }
    }
}

static void app_handle_update_result(
    app_state *app,
    app_update_result *result
)
{
    char message[512];
    const int silent = app->update_check_silent;
    if (app->update_thread != NULL) {
        SDL_WaitThread(app->update_thread, NULL);
        app->update_thread = NULL;
    }
    if (result == NULL) {
        return;
    }
    if (result->status == APP_UPDATE_AVAILABLE) {
        app_show_update(app, &result->info);
    } else if (!silent && result->status == APP_UPDATE_CURRENT) {
        (void)snprintf(
            message,
            sizeof(message),
            app_tr("当前版本 %s 已是最新版本。"),
            XLSONE_VERSION
        );
        app_show_notice(app, "检查更新", message, 0);
    } else if (!silent) {
        app_show_notice(
            app,
            "检查更新失败",
            "无法连接更新服务器，请稍后重试。",
            1
        );
    }
    free(result);
}

#define UI_BG0 nk_rgb(246, 247, 250)
#define UI_CHROME nk_rgb(235, 235, 235)
#define UI_BG1 nk_rgb(255, 255, 255)
#define UI_BG2 nk_rgb(248, 249, 251)
#define UI_BORDER nk_rgb(220, 224, 232)
#define UI_BORDER_SOFT nk_rgb(232, 235, 240)
#define UI_TEXT nk_rgb(31, 35, 40)
#define UI_MUTED nk_rgb(100, 109, 122)
#define UI_DISABLED nk_rgb(160, 168, 180)
#define UI_ACCENT nk_rgb(42, 117, 255)
#define UI_ACCENT_HOVER nk_rgb(35, 104, 232)
#define UI_ACCENT_SOFT nk_rgb(232, 240, 255)
#define UI_SUM_BG nk_rgb(239, 246, 255)
#define UI_SUM_FG nk_rgb(29, 95, 191)
#define UI_SUM_BORDER nk_rgb(189, 215, 255)
#define UI_LABEL_BG nk_rgb(236, 253, 245)
#define UI_LABEL_FG nk_rgb(22, 116, 79)
#define UI_LABEL_BORDER nk_rgb(189, 235, 212)
#define UI_WARNING nk_rgb(255, 149, 0)

typedef enum ui_icon {
    UI_ICON_NONE,
    UI_ICON_PLUS,
    UI_ICON_REFRESH,
    UI_ICON_CLOSE,
    UI_ICON_EXPORT,
    UI_ICON_FOLDER
} ui_icon;

typedef enum app_action {
    APP_ACTION_NONE,
    APP_ACTION_IMPORT,
    APP_ACTION_APPEND,
    APP_ACTION_RELOAD,
    APP_ACTION_CLEAR,
    APP_ACTION_EXPORT,
    APP_ACTION_QUIT,
    APP_ACTION_UNDO,
    APP_ACTION_CLEAR_OVERRIDES,
    APP_ACTION_VIEW_RULES,
    APP_ACTION_SAVE_RULES,
    APP_ACTION_LICENSE,
    APP_ACTION_LANGUAGE_SYSTEM,
    APP_ACTION_LANGUAGE_ENGLISH,
    APP_ACTION_LANGUAGE_ZH_HANS,
    APP_ACTION_LANGUAGE_ZH_HANT,
    APP_ACTION_LANGUAGE_JAPANESE,
    APP_ACTION_CHECK_UPDATE,
    APP_ACTION_HELP,
    APP_ACTION_ABOUT
} app_action;

typedef struct app_menu_item {
    const char *label;
    const char *shortcut;
    app_action action;
    int separator_before;
} app_menu_item;

static void apply_theme(struct nk_context *context)
{
    struct nk_color table[NK_COLOR_COUNT];
    size_t index;
    for (index = 0; index < NK_COLOR_COUNT; ++index) {
        table[index] = UI_BG0;
    }
    table[NK_COLOR_TEXT] = UI_TEXT;
    table[NK_COLOR_WINDOW] = UI_BG0;
    table[NK_COLOR_HEADER] = UI_BG0;
    table[NK_COLOR_BORDER] = UI_BORDER;
    table[NK_COLOR_BUTTON] = UI_BG1;
    table[NK_COLOR_BUTTON_HOVER] = UI_BG2;
    table[NK_COLOR_BUTTON_ACTIVE] = UI_ACCENT_SOFT;
    table[NK_COLOR_SCROLLBAR] = UI_BG2;
    table[NK_COLOR_SCROLLBAR_CURSOR] = UI_BORDER;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = UI_DISABLED;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = UI_MUTED;
    nk_style_from_table(context, table);
    context->style.window.padding = nk_vec2(0.0f, 0.0f);
    context->style.window.spacing = nk_vec2(0.0f, 0.0f);
    context->style.window.border = 0.0f;
    context->style.window.rounding = 0.0f;
}

static float ui_text_width(const struct nk_user_font *font, const char *text)
{
    size_t length;
    if (font == NULL || text == NULL || text[0] == '\0') {
        return 0.0f;
    }
    length = strlen(text);
    if (length > 2147483647u) {
        length = 2147483647u;
    }
    return font->width(font->userdata, font->height, text, (int)length);
}

static float ui_max_float(float left, float right)
{
    return left > right ? left : right;
}

static float ui_required_button_width(
    const struct nk_user_font *font,
    const char *text,
    float minimum,
    float horizontal_padding,
    int has_icon
)
{
    float width = ui_text_width(font, text)
        + horizontal_padding
        + (has_icon ? 18.0f : 0.0f);
    if (width < minimum) {
        width = minimum;
    }
    return width;
}

static float ui_multiline_max_width(
    const struct nk_user_font *font,
    const char *text
)
{
    const char *cursor = text == NULL ? "" : text;
    float maximum = 0.0f;
    while (*cursor != '\0') {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline == NULL
            ? strlen(cursor)
            : (size_t)(newline - cursor);
        char line[1024];
        if (length >= sizeof(line)) {
            length = sizeof(line) - 1u;
        }
        memcpy(line, cursor, length);
        line[length] = '\0';
        maximum = ui_max_float(
            maximum, ui_text_width(font, line)
        );
        cursor = newline == NULL
            ? cursor + strlen(cursor)
            : newline + 1;
    }
    return maximum;
}

static int ui_text_has_non_ascii(const char *text)
{
    size_t index;
    if (text == NULL) {
        return 0;
    }
    for (index = 0u; text[index] != '\0'; ++index) {
        if ((unsigned char)text[index] >= 0x80u) {
            return 1;
        }
    }
    return 0;
}

static void ui_draw_text(
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    struct nk_color color,
    nk_flags alignment
)
{
    float width;
    float x;
    float y;
    int length;
    if (font == NULL || text == NULL || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    length = (int)strlen(text);
    width = ui_text_width(font, text);
    x = rect.x;
    if ((alignment & NK_TEXT_ALIGN_CENTERED) != 0u) {
        x += (rect.w - width) * 0.5f;
    } else if ((alignment & NK_TEXT_ALIGN_RIGHT) != 0u) {
        x += rect.w - width;
    }
    y = rect.y + (rect.h - font->height) * 0.5f;
    nk_draw_text(
        canvas,
        nk_rect(x, y, width + 2.0f, font->height),
        text,
        length,
        font,
        nk_rgba(0, 0, 0, 0),
        color
    );
}

static void ui_draw_text_elided(
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    struct nk_color color,
    nk_flags alignment
)
{
    char buffer[512];
    size_t length;
    if (text == NULL) {
        text = "";
    }
    if (ui_text_width(font, text) <= rect.w) {
        ui_draw_text(canvas, rect, text, font, color, alignment);
        return;
    }
    (void)snprintf(buffer, sizeof(buffer), "%s", text);
    length = strlen(buffer);
    while (length > 0
        && (((unsigned char)buffer[length] & 0xc0u) == 0x80u)) {
        buffer[length--] = '\0';
    }
    while (length > 0) {
        while (length > 0
            && (((unsigned char)buffer[length - 1] & 0xc0u) == 0x80u)) {
            --length;
        }
        if (length > 0) {
            --length;
        }
        buffer[length] = '\0';
        if (length + 3 < sizeof(buffer)) {
            (void)strcat(buffer, "...");
        }
        if (ui_text_width(font, buffer) <= rect.w) {
            break;
        }
        buffer[length] = '\0';
    }
    ui_draw_text(canvas, rect, buffer, font, color, alignment);
}

static void ui_draw_multiline(
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    struct nk_color color,
    float line_height
)
{
    const char *cursor = text == NULL ? "" : text;
    float y = rect.y;
    while (*cursor != '\0' && y + line_height <= rect.y + rect.h) {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline == NULL
            ? strlen(cursor)
            : (size_t)(newline - cursor);
        char line[512];
        if (length >= sizeof(line)) {
            length = sizeof(line) - 1u;
        }
        memcpy(line, cursor, length);
        line[length] = '\0';
        ui_draw_text_elided(
            canvas,
            nk_rect(rect.x, y, rect.w, line_height),
            line,
            font,
            color,
            NK_TEXT_LEFT
        );
        y += line_height;
        cursor = newline == NULL ? cursor + strlen(cursor) : newline + 1;
    }
}

static size_t ui_utf8_character_size(unsigned char first)
{
    if ((first & 0x80u) == 0u) {
        return 1u;
    }
    if ((first & 0xe0u) == 0xc0u) {
        return 2u;
    }
    if ((first & 0xf0u) == 0xe0u) {
        return 3u;
    }
    if ((first & 0xf8u) == 0xf0u) {
        return 4u;
    }
    return 1u;
}

static void ui_draw_wrapped_text(
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    struct nk_color color,
    float line_height
)
{
    const size_t text_length = text == NULL ? 0u : strlen(text);
    size_t offset = 0u;
    float y = rect.y;
    while (offset < text_length
        && y + line_height <= rect.y + rect.h) {
        size_t line_end = offset;
        size_t last_space = (size_t)-1;
        char line[512];
        while (line_end < text_length
            && text[line_end] != '\n') {
            size_t character_size = ui_utf8_character_size(
                (unsigned char)text[line_end]
            );
            size_t candidate_end;
            size_t candidate_length;
            if (character_size > text_length - line_end) {
                character_size = 1u;
            }
            candidate_end = line_end + character_size;
            candidate_length = candidate_end - offset;
            if (candidate_length >= sizeof(line)) {
                break;
            }
            memcpy(line, text + offset, candidate_length);
            line[candidate_length] = '\0';
            if (ui_text_width(font, line) > rect.w
                && line_end > offset) {
                break;
            }
            line_end = candidate_end;
            if (character_size == 1u
                && (text[line_end - 1u] == ' '
                    || text[line_end - 1u] == '\t')) {
                last_space = line_end;
            }
        }
        if (line_end < text_length
            && text[line_end] != '\n'
            && last_space != (size_t)-1
            && last_space > offset) {
            line_end = last_space;
        }
        if (line_end == offset
            && text[line_end] != '\n') {
            line_end += ui_utf8_character_size(
                (unsigned char)text[line_end]
            );
            if (line_end > text_length) {
                line_end = text_length;
            }
        }
        {
            size_t line_length = line_end - offset;
            while (line_length > 0u
                && (text[offset + line_length - 1u] == ' '
                    || text[offset + line_length - 1u] == '\t')) {
                --line_length;
            }
            if (line_length >= sizeof(line)) {
                line_length = sizeof(line) - 1u;
            }
            memcpy(line, text + offset, line_length);
            line[line_length] = '\0';
            ui_draw_text(
                canvas,
                nk_rect(rect.x, y, rect.w, line_height),
                line,
                font,
                color,
                NK_TEXT_LEFT
            );
        }
        y += line_height;
        offset = line_end;
        if (offset < text_length && text[offset] == '\n') {
            ++offset;
        }
        while (offset < text_length
            && (text[offset] == ' ' || text[offset] == '\t')) {
            ++offset;
        }
    }
}

static int ui_hovered(const struct nk_context *context, struct nk_rect rect)
{
    if (ui_input_blocked) {
        return 0;
    }
    return nk_input_is_mouse_hovering_rect(&context->input, rect) != 0;
}

static int ui_clicked(const struct nk_context *context, struct nk_rect rect)
{
    if (ui_input_blocked) {
        return 0;
    }
    return nk_input_is_mouse_click_in_rect(
        &context->input,
        NK_BUTTON_LEFT,
        rect
    ) != 0;
}

static void ui_draw_icon(
    struct nk_command_buffer *canvas,
    ui_icon icon,
    float x,
    float y,
    struct nk_color color
)
{
    const float center_x = x + 7.0f;
    const float center_y = y + 7.0f;
    if (icon == UI_ICON_PLUS) {
        nk_stroke_line(canvas, center_x, y + 2.0f, center_x, y + 12.0f, 1.5f, color);
        nk_stroke_line(canvas, x + 2.0f, center_y, x + 12.0f, center_y, 1.5f, color);
    } else if (icon == UI_ICON_CLOSE) {
        nk_stroke_line(canvas, x + 3.0f, y + 3.0f, x + 11.0f, y + 11.0f, 1.5f, color);
        nk_stroke_line(canvas, x + 11.0f, y + 3.0f, x + 3.0f, y + 11.0f, 1.5f, color);
    } else if (icon == UI_ICON_REFRESH) {
        nk_stroke_arc(canvas, center_x, center_y, 5.0f, 0.55f, 5.6f, 1.5f, color);
        nk_fill_triangle(
            canvas,
            x + 10.0f,
            y + 1.0f,
            x + 13.0f,
            y + 5.0f,
            x + 8.0f,
            y + 5.0f,
            color
        );
    } else if (icon == UI_ICON_EXPORT) {
        nk_stroke_line(canvas, center_x, y + 2.0f, center_x, y + 9.0f, 1.5f, color);
        nk_stroke_line(canvas, center_x, y + 2.0f, x + 4.0f, y + 5.0f, 1.5f, color);
        nk_stroke_line(canvas, center_x, y + 2.0f, x + 10.0f, y + 5.0f, 1.5f, color);
        nk_stroke_line(canvas, x + 3.0f, y + 8.0f, x + 3.0f, y + 12.0f, 1.5f, color);
        nk_stroke_line(canvas, x + 3.0f, y + 12.0f, x + 11.0f, y + 12.0f, 1.5f, color);
        nk_stroke_line(canvas, x + 11.0f, y + 12.0f, x + 11.0f, y + 8.0f, 1.5f, color);
    } else if (icon == UI_ICON_FOLDER) {
        float points[] = {
            x + 1.0f, y + 5.0f,
            x + 5.0f, y + 5.0f,
            x + 7.0f, y + 7.0f,
            x + 13.0f, y + 7.0f,
            x + 13.0f, y + 13.0f,
            x + 1.0f, y + 13.0f
        };
        nk_stroke_polygon(canvas, points, 6, 1.3f, color);
        nk_stroke_line(canvas, x + 1.0f, y + 5.0f, x + 1.0f, y + 13.0f, 1.3f, color);
    }
}

static int ui_draw_button(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    ui_icon icon,
    int primary
)
{
    const int hovered = ui_hovered(context, rect);
    const struct nk_color background = primary
        ? (hovered ? UI_ACCENT_HOVER : UI_ACCENT)
        : (hovered ? UI_BG2 : UI_BG1);
    const struct nk_color foreground = primary ? nk_rgb(255, 255, 255) : UI_MUTED;
    const float text_width = ui_text_width(font, text);
    const float icon_width = icon == UI_ICON_NONE ? 0.0f : 18.0f;
    const float start_x = rect.x + (rect.w - text_width - icon_width) * 0.5f;
    nk_fill_rect(canvas, rect, primary ? 8.0f : 9.0f, background);
    nk_stroke_rect(
        canvas,
        nk_rect(rect.x + 0.5f, rect.y + 0.5f, rect.w - 1.0f, rect.h - 1.0f),
        primary ? 8.0f : 9.0f,
        1.0f,
        primary ? nk_rgb(81, 145, 255) : UI_BORDER
    );
    if (icon != UI_ICON_NONE) {
        ui_draw_icon(
            canvas,
            icon,
            start_x,
            rect.y + (rect.h - 14.0f) * 0.5f,
            foreground
        );
    }
    ui_draw_text(
        canvas,
        nk_rect(start_x + icon_width, rect.y, text_width + 2.0f, rect.h),
        text,
        font,
        foreground,
        NK_TEXT_LEFT
    );
    return ui_clicked(context, rect);
}

static int ui_draw_utility_button(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    struct nk_rect rect,
    const char *text,
    const struct nk_user_font *font,
    ui_icon icon
)
{
    const float text_width = ui_text_width(font, text);
    const float start_x = rect.x + (rect.w - text_width - 18.0f) * 0.5f;
    if (ui_hovered(context, rect)) {
        nk_fill_rect(canvas, rect, 7.0f, nk_rgb(244, 245, 247));
    }
    ui_draw_icon(
        canvas,
        icon,
        start_x,
        rect.y + (rect.h - 14.0f) * 0.5f,
        UI_MUTED
    );
    ui_draw_text(
        canvas,
        nk_rect(start_x + 18.0f, rect.y, text_width + 1.0f, rect.h),
        text,
        font,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    return ui_clicked(context, rect);
}

static int app_action_enabled(const app_state *app, app_action action)
{
    switch (action) {
    case APP_ACTION_APPEND:
    case APP_ACTION_RELOAD:
    case APP_ACTION_CLEAR:
        return app->workbook_count > 0u;
    case APP_ACTION_EXPORT:
        return app->merged_sheet_count > 0u
            && app->validation.has_template_workbook != 0u;
    case APP_ACTION_UNDO:
        return app->has_last_override;
    case APP_ACTION_CLEAR_OVERRIDES:
    case APP_ACTION_SAVE_RULES:
        return app_override_count(app) > 0u;
    default:
        return 1;
    }
}

static void app_change_language(
    app_state *app,
    xls_ui_language preference
)
{
    const int stored = app_store_language(preference);
    app->language_index = (int)preference;
    xls_i18n_set_language(app_effective_language(preference));
    app->language_layout_dirty = 1;
    if (app->window != NULL) {
        SDL_SetWindowTitle(app->window, app_tr("表表归一"));
    }
    app_set_status(
        app,
        stored
            ? "界面语言已切换，并会在下次启动时继续使用。"
            : "界面语言已切换，但无法保存语言设置。"
    );
}

static void app_execute_action(app_state *app, app_action action)
{
    app->active_menu = APP_MENU_NONE;
    if (!app_action_enabled(app, action)) {
        return;
    }
    switch (action) {
    case APP_ACTION_IMPORT:
        app_open_files(app, 0);
        break;
    case APP_ACTION_APPEND:
        app_open_files(app, 1);
        break;
    case APP_ACTION_RELOAD:
        (void)app_recompute(app);
        break;
    case APP_ACTION_CLEAR:
        app_clear(app);
        break;
    case APP_ACTION_EXPORT:
        app_export(app);
        break;
    case APP_ACTION_QUIT:
        app->running = 0;
        break;
    case APP_ACTION_UNDO:
        app_undo_last_override(app);
        break;
    case APP_ACTION_CLEAR_OVERRIDES:
        app_clear_overrides(app);
        break;
    case APP_ACTION_VIEW_RULES:
        app_show_rules(app);
        break;
    case APP_ACTION_SAVE_RULES:
        app_save_rules(app);
        break;
    case APP_ACTION_LICENSE:
        app_show_license(app);
        break;
    case APP_ACTION_LANGUAGE_SYSTEM:
        app_change_language(app, XLS_UI_LANGUAGE_SYSTEM);
        break;
    case APP_ACTION_LANGUAGE_ENGLISH:
        app_change_language(app, XLS_UI_LANGUAGE_ENGLISH);
        break;
    case APP_ACTION_LANGUAGE_ZH_HANS:
        app_change_language(app, XLS_UI_LANGUAGE_ZH_HANS);
        break;
    case APP_ACTION_LANGUAGE_ZH_HANT:
        app_change_language(app, XLS_UI_LANGUAGE_ZH_HANT);
        break;
    case APP_ACTION_LANGUAGE_JAPANESE:
        app_change_language(app, XLS_UI_LANGUAGE_JAPANESE);
        break;
    case APP_ACTION_CHECK_UPDATE:
        app_start_update_check(app, 0);
        break;
    case APP_ACTION_HELP:
        if (!xls_platform_open_url("https://z-pulse.cn/xlsone/")) {
            app_set_status(app, "无法打开快速参考指南。");
        }
        break;
    case APP_ACTION_ABOUT:
        {
            char about[512];
            (void)snprintf(
                about,
                sizeof(about),
                app_tr(
                    "表表归一 %s\n"
                    "多张同格式 Excel 报表一键汇总\n\n"
                    "版权所有 © Z-Pulse"
                ),
                XLSONE_VERSION
            );
            app_show_notice(
                app,
                "关于 表表归一",
                about,
                0
            );
        }
        break;
    case APP_ACTION_NONE:
    default:
        break;
    }
}

static const char *app_menu_label(app_menu menu);

static float app_menu_width(
    app_menu menu,
    const struct nk_user_font *font
)
{
    float width = ui_text_width(font, app_menu_label(menu)) + 20.0f;
    if (width < 42.0f) {
        width = 42.0f;
    }
    return width;
}

static float app_menu_x(
    app_menu target,
    const struct nk_user_font *font
)
{
    app_menu menu;
    float x = 8.0f;
    for (menu = APP_MENU_FILE; menu < target;
         menu = (app_menu)((int)menu + 1)) {
        x += app_menu_width(menu, font);
    }
    return x;
}

static const char *app_menu_label(app_menu menu)
{
    switch (menu) {
    case APP_MENU_FILE: return app_tr("文件");
    case APP_MENU_EDIT: return app_tr("编辑");
    case APP_MENU_RULES: return app_tr("修正规则");
    case APP_MENU_LICENSE: return app_tr("许可");
    case APP_MENU_LANGUAGE: return app_tr("语言");
    case APP_MENU_HELP: return app_tr("帮助");
    case APP_MENU_NONE:
    default:
        return "";
    }
}

static float app_license_brand_width(const ui_fonts *fonts)
{
    static const char *const pills[] = {"快速", "安全", "原生"};
    float pill_row_width = 0.0f;
    float width = ui_text_width(
        fonts->body,
        app_tr("多张同格式 Excel 报表一键汇总")
    ) + 48.0f;
    size_t index;
    for (index = 0u; index < 3u; ++index) {
        pill_row_width += ui_required_button_width(
            fonts->body,
            app_tr(pills[index]),
            56.0f,
            20.0f,
            0
        );
        if (index > 0u) {
            pill_row_width += 12.0f;
        }
    }
    width = ui_max_float(width, pill_row_width + 64.0f);
    return ui_max_float(width, 280.0f);
}

static float app_license_panel_width(const ui_fonts *fonts)
{
    static const char *const steps[] = {
        "1   打开离线激活页面，粘贴设备码并提交",
        "2   下载生成的授权文件（.license）",
        "3   回到这里导入授权文件完成激活"
    };
    static const char *const subtitles[] = {
        "您的软件已激活，许可证剩余 %d 天。",
        "您的软件已激活，可正常使用全部功能。",
        "试用期剩余 %d 天，期间所有功能开放。",
        "许可证或试用期已过期，请输入激活码继续使用完整功能。",
        "输入激活码，或先开始 14 天免费试用。"
    };
    float width = 440.0f;
    float button_row;
    size_t index;
    for (index = 0u; index < 3u; ++index) {
        width = ui_max_float(
            width,
            ui_text_width(fonts->body, app_tr(steps[index]))
                + 90.0f
        );
    }
    for (index = 0u; index < 5u; ++index) {
        width = ui_max_float(
            width,
            ui_text_width(
                fonts->body, app_tr(subtitles[index])
            ) + 60.0f
        );
    }
    button_row = ui_required_button_width(
        fonts->body,
        app_tr("打开离线激活页面"),
        168.0f,
        28.0f,
        0
    ) + ui_required_button_width(
        fonts->body,
        app_tr("导入授权文件..."),
        168.0f,
        28.0f,
        0
    ) + 105.0f;
    width = ui_max_float(width, button_row);
    width = ui_max_float(
        width,
        ui_required_button_width(
            fonts->body,
            app_tr("开始免费试用 14 天"),
            142.0f,
            12.0f,
            0
        ) + ui_required_button_width(
            fonts->body,
            app_tr("获取激活码"),
            108.0f,
            24.0f,
            0
        ) + 96.0f
    );
    return width;
}

static int app_language_minimum_width(
    const app_state *app,
    const ui_fonts *fonts
)
{
    app_menu menu;
    char license_text[96];
    const char *license_state = app_license_state_text(
        &app->license, license_text, sizeof(license_text)
    );
    float menu_width = 16.0f;
    float toolbar_left = 12.0f;
    float toolbar_right;
    float required = 980.0f;
    for (menu = APP_MENU_FILE; menu <= APP_MENU_HELP;
         menu = (app_menu)((int)menu + 1)) {
        menu_width += app_menu_width(menu, fonts->body);
    }
    toolbar_left += ui_required_button_width(
        fonts->body, app_tr("追加"), 51.0f, 14.0f, 1
    );
    toolbar_left += ui_required_button_width(
        fonts->body, app_tr("刷新"), 54.0f, 14.0f, 1
    );
    toolbar_left += ui_required_button_width(
        fonts->body, app_tr("清空"), 52.0f, 14.0f, 1
    );
    toolbar_right = ui_text_width(
        fonts->body, license_state
    ) + 22.0f;
    toolbar_right += ui_required_button_width(
        fonts->body,
        app_tr("导出 XLSX"),
        91.0f,
        18.0f,
        1
    ) + 42.0f;
    required = ui_max_float(required, menu_width);
    required = ui_max_float(
        required, toolbar_left + toolbar_right + 36.0f
    );
    required = ui_max_float(
        required,
        app_license_brand_width(fonts)
            + app_license_panel_width(fonts)
            + 60.0f
    );
    return (int)(required + 0.999f);
}

static void app_apply_language_layout(
    app_state *app,
    const ui_fonts *fonts
)
{
    int width;
    int height;
    const int minimum_width =
        app_language_minimum_width(app, fonts);
    if (app->window == NULL) {
        return;
    }
    SDL_SetWindowMinimumSize(
        app->window, minimum_width, 600
    );
    SDL_GetWindowSize(app->window, &width, &height);
    if (width < minimum_width || height < 600) {
        SDL_SetWindowSize(
            app->window,
            width < minimum_width ? minimum_width : width,
            height < 600 ? 600 : height
        );
    }
    app->language_minimum_width = minimum_width;
    app->language_layout_dirty = 0;
}

static const app_menu_item *app_menu_items(
    app_menu menu,
    size_t *count
)
{
    static const app_menu_item file_items[] = {
        {"导入文件...", "Ctrl+O", APP_ACTION_IMPORT, 0},
        {"追加文件...", "Ctrl+Shift+O", APP_ACTION_APPEND, 0},
        {"刷新", "Ctrl+R", APP_ACTION_RELOAD, 0},
        {"清空工作区", "Ctrl+N", APP_ACTION_CLEAR, 0},
        {"导出 XLSX...", "Ctrl+S", APP_ACTION_EXPORT, 1},
        {"退出", "Ctrl+Q", APP_ACTION_QUIT, 1}
    };
    static const app_menu_item edit_items[] = {
        {"撤销修正", "Ctrl+Z", APP_ACTION_UNDO, 0},
        {"清除所有修正", "", APP_ACTION_CLEAR_OVERRIDES, 0}
    };
    static const app_menu_item rule_items[] = {
        {"查看当前修正规则", "Ctrl+,", APP_ACTION_VIEW_RULES, 0},
        {"保存当前修正规则", "", APP_ACTION_SAVE_RULES, 0}
    };
    static const app_menu_item license_items[] = {
        {"激活/导入许可证...", "", APP_ACTION_LICENSE, 0}
    };
    static const app_menu_item language_items[] = {
        {"跟随系统", "", APP_ACTION_LANGUAGE_SYSTEM, 0},
        {"English", "", APP_ACTION_LANGUAGE_ENGLISH, 0},
        {"简体中文", "", APP_ACTION_LANGUAGE_ZH_HANS, 0},
        {"繁體中文", "", APP_ACTION_LANGUAGE_ZH_HANT, 0},
        {"日本語", "", APP_ACTION_LANGUAGE_JAPANESE, 0}
    };
    static const app_menu_item help_items[] = {
        {"检查更新", "", APP_ACTION_CHECK_UPDATE, 0},
        {"快速参考指南", "F1", APP_ACTION_HELP, 1},
        {"关于 表表归一", "", APP_ACTION_ABOUT, 1}
    };
    switch (menu) {
    case APP_MENU_FILE:
        *count = sizeof(file_items) / sizeof(file_items[0]);
        return file_items;
    case APP_MENU_EDIT:
        *count = sizeof(edit_items) / sizeof(edit_items[0]);
        return edit_items;
    case APP_MENU_RULES:
        *count = sizeof(rule_items) / sizeof(rule_items[0]);
        return rule_items;
    case APP_MENU_LICENSE:
        *count = sizeof(license_items) / sizeof(license_items[0]);
        return license_items;
    case APP_MENU_LANGUAGE:
        *count = sizeof(language_items) / sizeof(language_items[0]);
        return language_items;
    case APP_MENU_HELP:
        *count = sizeof(help_items) / sizeof(help_items[0]);
        return help_items;
    case APP_MENU_NONE:
    default:
        *count = 0u;
        return NULL;
    }
}

static int app_language_action_index(app_action action)
{
    switch (action) {
    case APP_ACTION_LANGUAGE_SYSTEM: return 0;
    case APP_ACTION_LANGUAGE_ENGLISH: return 1;
    case APP_ACTION_LANGUAGE_ZH_HANS: return 2;
    case APP_ACTION_LANGUAGE_ZH_HANT: return 3;
    case APP_ACTION_LANGUAGE_JAPANESE: return 4;
    default: return -1;
    }
}

static void render_menu_bar(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width
)
{
    app_menu menu;
    nk_fill_rect(
        canvas,
        nk_rect(0.0f, 0.0f, width, UI_MENU_HEIGHT),
        0.0f,
        UI_BG0
    );
    nk_stroke_line(
        canvas,
        0.0f,
        UI_MENU_HEIGHT - 0.5f,
        width,
        UI_MENU_HEIGHT - 0.5f,
        1.0f,
        UI_BORDER
    );
    for (menu = APP_MENU_FILE; menu <= APP_MENU_HELP;
         menu = (app_menu)((int)menu + 1)) {
        const struct nk_rect item = nk_rect(
            app_menu_x(menu, fonts->body),
            2.0f,
            app_menu_width(menu, fonts->body),
            UI_MENU_HEIGHT - 4.0f
        );
        if (app->active_menu == menu || ui_hovered(context, item)) {
            nk_fill_rect(canvas, item, 5.0f, UI_BORDER_SOFT);
        }
        ui_draw_text(
            canvas,
            item,
            app_menu_label(menu),
            fonts->body,
            UI_TEXT,
            NK_TEXT_CENTERED
        );
        if (app->dialog == APP_DIALOG_NONE && ui_clicked(context, item)) {
            app->active_menu = app->active_menu == menu
                ? APP_MENU_NONE
                : menu;
        }
    }
}

static void render_active_menu(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts
)
{
    const app_menu active = app->active_menu;
    const app_menu_item *items;
    size_t count;
    size_t index;
    float height = 10.0f;
    float popup_width = 244.0f;
    float y;
    struct nk_rect popup;
    int handled_click = 0;
    if (active == APP_MENU_NONE || app->dialog != APP_DIALOG_NONE) {
        return;
    }
    items = app_menu_items(active, &count);
    for (index = 0u; index < count; ++index) {
        const float required_width =
            ui_text_width(fonts->body, app_tr(items[index].label))
            + ui_text_width(fonts->body, items[index].shortcut)
            + 76.0f;
        height += 27.0f + (items[index].separator_before ? 8.0f : 0.0f);
        if (required_width > popup_width) {
            popup_width = required_width;
        }
    }
    popup = nk_rect(
        app_menu_x(active, fonts->body),
        UI_MENU_HEIGHT - 1.0f,
        popup_width,
        height
    );
    nk_fill_rect(
        canvas,
        nk_rect(popup.x + 2.0f, popup.y + 4.0f, popup.w, popup.h),
        7.0f,
        nk_rgba(0, 0, 0, 28)
    );
    nk_fill_rect(canvas, popup, 7.0f, UI_BG1);
    nk_stroke_rect(
        canvas,
        nk_rect(
            popup.x + 0.5f,
            popup.y + 0.5f,
            popup.w - 1.0f,
            popup.h - 1.0f
        ),
        7.0f,
        1.0f,
        UI_BORDER
    );
    y = popup.y + 5.0f;
    for (index = 0u; index < count; ++index) {
        const app_menu_item *item = &items[index];
        const int enabled = app_action_enabled(app, item->action);
        struct nk_rect row;
        int language_index;
        if (item->separator_before) {
            nk_stroke_line(
                canvas,
                popup.x + 9.0f,
                y + 3.5f,
                popup.x + popup.w - 9.0f,
                y + 3.5f,
                1.0f,
                UI_BORDER_SOFT
            );
            y += 8.0f;
        }
        row = nk_rect(popup.x + 5.0f, y, popup.w - 10.0f, 27.0f);
        if (enabled && ui_hovered(context, row)) {
            nk_fill_rect(canvas, row, 5.0f, UI_ACCENT_SOFT);
        }
        language_index = app_language_action_index(item->action);
        if (language_index >= 0 && app->language_index == language_index) {
            nk_fill_circle(
                canvas,
                nk_rect(row.x + 9.0f, row.y + 10.0f, 7.0f, 7.0f),
                UI_ACCENT
            );
        }
        ui_draw_text(
            canvas,
            nk_rect(row.x + 22.0f, row.y, row.w - 92.0f, row.h),
            app_tr(item->label),
            fonts->body,
            enabled ? UI_TEXT : UI_DISABLED,
            NK_TEXT_LEFT
        );
        if (item->shortcut[0] != '\0') {
            ui_draw_text(
                canvas,
                nk_rect(row.x + row.w - 92.0f, row.y, 82.0f, row.h),
                item->shortcut,
                fonts->body,
                enabled ? UI_MUTED : UI_DISABLED,
                NK_TEXT_RIGHT
            );
        }
        if (enabled && ui_clicked(context, row)) {
            handled_click = 1;
            app_execute_action(app, item->action);
        }
        y += 27.0f;
    }
    if (!handled_click
        && nk_input_is_mouse_pressed(
            &context->input, NK_BUTTON_LEFT
        ) != 0
        && !ui_hovered(context, popup)
        && context->input.mouse.pos.y >= UI_MENU_HEIGHT) {
        app->active_menu = APP_MENU_NONE;
    }
}

static void ui_column_letters(size_t column, char *buffer, size_t capacity)
{
    char reversed[16];
    size_t length = 0;
    do {
        reversed[length++] = (char)('A' + column % 26u);
        column /= 26u;
        if (column > 0) {
            --column;
        }
    } while (column > 0 && length < sizeof(reversed));
    if (length + 1 > capacity) {
        length = capacity - 1;
    }
    {
        size_t index;
        for (index = 0; index < length; ++index) {
            buffer[index] = reversed[length - index - 1];
        }
    }
    buffer[length] = '\0';
}

static const char *ui_kind_text(xls_cell_kind kind)
{
    switch (kind) {
    case XLS_CELL_SUM:
        return app_tr("求和");
    case XLS_CELL_MIXED:
        return app_tr("混合");
    case XLS_CELL_SINGLE:
        return app_tr("单值");
    case XLS_CELL_LABEL:
    default:
        return app_tr("标签");
    }
}

static int app_has_overrides(const app_state *app)
{
    size_t sheet_index;
    size_t cell_index;
    for (sheet_index = 0; sheet_index < app->merged_sheet_count; ++sheet_index) {
        const xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        for (cell_index = 0;
             cell_index < sheet->row_count * sheet->column_count;
             ++cell_index) {
            if (sheet->cells[cell_index].is_overridden != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

static void render_toolbar(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    int has_workspace
)
{
    const struct nk_rect toolbar = nk_rect(
        0.0f, UI_TOOLBAR_TOP, width, 40.0f
    );
    const char *export_text = app_tr("导出 XLSX");
    const float export_width = ui_required_button_width(
        fonts->body,
        export_text,
        91.0f,
        18.0f,
        1
    );
    nk_fill_rect(canvas, toolbar, 0.0f, UI_CHROME);
    nk_stroke_line(
        canvas,
        0.0f,
        UI_TOOLBAR_BOTTOM - 0.5f,
        width,
        UI_TOOLBAR_BOTTOM - 0.5f,
        1.0f,
        UI_BORDER
    );

    if (has_workspace) {
        const float append_width = ui_required_button_width(
            fonts->body, app_tr("追加"), 51.0f, 14.0f, 1
        );
        const float reload_width = ui_required_button_width(
            fonts->body, app_tr("刷新"), 54.0f, 14.0f, 1
        );
        const float clear_width = ui_required_button_width(
            fonts->body, app_tr("清空"), 52.0f, 14.0f, 1
        );
        const struct nk_rect group = nk_rect(
            12.0f,
            UI_TOOLBAR_TOP + 7.0f,
            append_width + reload_width + clear_width + 8.0f,
            32.0f
        );
        float button_x = group.x + 4.0f;
        nk_fill_rect(canvas, group, 11.0f, UI_BG1);
        nk_stroke_rect(
            canvas,
            nk_rect(group.x + 0.5f, group.y + 0.5f, group.w - 1.0f, group.h - 1.0f),
            11.0f,
            1.0f,
            UI_BORDER
        );
        if (ui_draw_utility_button(
            context, canvas,
            nk_rect(
                button_x,
                UI_TOOLBAR_TOP + 10.0f,
                append_width,
                26.0f
            ),
            app_tr("追加"), fonts->body, UI_ICON_PLUS
        )) {
            app_open_files(app, 1);
        }
        button_x += append_width;
        if (ui_draw_utility_button(
            context, canvas,
            nk_rect(
                button_x,
                UI_TOOLBAR_TOP + 10.0f,
                reload_width,
                26.0f
            ),
            app_tr("刷新"), fonts->body, UI_ICON_REFRESH
        )) {
            (void)app_recompute(app);
        }
        button_x += reload_width;
        if (ui_draw_utility_button(
            context, canvas,
            nk_rect(
                button_x,
                UI_TOOLBAR_TOP + 10.0f,
                clear_width,
                26.0f
            ),
            app_tr("清空"), fonts->body, UI_ICON_CLOSE
        )) {
            app_clear(app);
        }
    }

    {
        char license_text[96];
        const char *state_text = app_license_state_text(
            &app->license, license_text, sizeof(license_text)
        );
        float license_width = ui_text_width(fonts->body, state_text) + 22.0f;
        struct nk_color license_color = UI_WARNING;
        float license_x;
        struct nk_rect license;
        if (license_width < 64.0f) {
            license_width = 64.0f;
        }
        if (app->license.state == XLS_LICENSE_ACTIVATED) {
            license_color = UI_LABEL_FG;
        } else if (app->license.state == XLS_LICENSE_TRIAL) {
            license_color = UI_ACCENT;
        } else if (app->license.state == XLS_LICENSE_EXPIRED) {
            license_color = nk_rgb(204, 53, 53);
        }
        license_x = has_workspace
            ? width - 21.0f - export_width - license_width
            : width - 11.0f - license_width;
        license = nk_rect(
            license_x,
            UI_TOOLBAR_TOP + 11.0f,
            license_width,
            23.0f
        );
        nk_fill_rect(canvas, license, 11.0f, nk_rgba(255, 255, 255, 110));
        nk_stroke_rect(
            canvas,
            nk_rect(license.x + 0.5f, license.y + 0.5f, license.w - 1.0f, license.h - 1.0f),
            11.0f,
            1.0f,
            license_color
        );
        ui_draw_text(
            canvas,
            license,
            state_text,
            fonts->body,
            license_color,
            NK_TEXT_CENTERED
        );
        if (ui_clicked(context, license)) {
            app_show_license(app);
        }
    }

    if (has_workspace) {
        if (ui_draw_button(
            context,
            canvas,
            nk_rect(
                width - 11.0f - export_width,
                UI_TOOLBAR_TOP + 7.0f,
                export_width,
                32.0f
            ),
            export_text,
            fonts->body,
            UI_ICON_EXPORT,
            1
        )) {
            app_export(app);
        }
    }
}

typedef struct sheet_tab_width_context {
    const app_state *app;
    const ui_fonts *fonts;
} sheet_tab_width_context;

static float sheet_tab_width(size_t tab_index, void *user_data)
{
    const sheet_tab_width_context *tab_context =
        (const sheet_tab_width_context *)user_data;
    const char *name =
        tab_context->app->merged_sheets[tab_index].sheet_name;
    float tab_width = ui_text_width(
        tab_context->fonts->body, name
    ) + 16.0f;
    if (tab_width < 72.0f) {
        tab_width = 72.0f;
    }
    if (tab_width > 220.0f) {
        tab_width = 220.0f;
    }
    return tab_width;
}

static int draw_sheet_navigation_button(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    struct nk_rect button,
    int direction,
    int enabled
)
{
    const int hovered = enabled && ui_hovered(context, button);
    const struct nk_color foreground =
        enabled ? UI_MUTED : UI_DISABLED;
    const float center_x = button.x + button.w * 0.5f;
    const float center_y = button.y + button.h * 0.5f;
    nk_fill_rect(
        canvas,
        button,
        9.0f,
        hovered ? UI_BG2 : UI_BG1
    );
    nk_stroke_rect(
        canvas,
        nk_rect(
            button.x + 0.5f,
            button.y + 0.5f,
            button.w - 1.0f,
            button.h - 1.0f
        ),
        9.0f,
        1.0f,
        UI_BORDER
    );
    if (direction < 0) {
        nk_fill_triangle(
            canvas,
            center_x + 3.0f,
            center_y - 5.0f,
            center_x + 3.0f,
            center_y + 5.0f,
            center_x - 3.0f,
            center_y,
            foreground
        );
    } else {
        nk_fill_triangle(
            canvas,
            center_x - 3.0f,
            center_y - 5.0f,
            center_x - 3.0f,
            center_y + 5.0f,
            center_x + 3.0f,
            center_y,
            foreground
        );
    }
    return enabled && ui_clicked(context, button);
}

static void render_sheet_strip(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width
)
{
    const float navigation_width = 30.0f;
    const float navigation_gap = 8.0f;
    const float tab_gap = 8.0f;
    const struct nk_rect previous_button = nk_rect(
        9.0f,
        UI_TOOLBAR_BOTTOM + 30.0f,
        navigation_width,
        26.0f
    );
    const struct nk_rect next_button = nk_rect(
        width - 9.0f - navigation_width,
        UI_TOOLBAR_BOTTOM + 30.0f,
        navigation_width,
        26.0f
    );
    const struct nk_rect viewport = nk_rect(
        previous_button.x + previous_button.w + navigation_gap,
        UI_TOOLBAR_BOTTOM + 30.0f,
        next_button.x
            - navigation_gap
            - (previous_button.x + previous_button.w + navigation_gap),
        26.0f
    );
    sheet_tab_width_context tab_width_context;
    size_t page_end;
    size_t index;
    char page_text[64];
    int can_go_previous;
    int can_go_next;
    int page_changed = 0;
    float x;

    nk_fill_rect(
        canvas,
        nk_rect(0.0f, UI_TOOLBAR_BOTTOM, width, 78.0f),
        0.0f,
        UI_CHROME
    );
    nk_stroke_line(
        canvas,
        0.0f,
        UI_SHEET_BOTTOM - 0.5f,
        width,
        UI_SHEET_BOTTOM - 0.5f,
        1.0f,
        UI_BORDER
    );
    if (app->merged_sheet_count == 0u || viewport.w <= 0.0f) {
        return;
    }
    if (app->first_visible_sheet >= app->merged_sheet_count) {
        app->first_visible_sheet = app->merged_sheet_count - 1u;
    }
    tab_width_context.app = app;
    tab_width_context.fonts = fonts;
    page_end = xls_sheet_tab_page_end(
        app->first_visible_sheet,
        app->merged_sheet_count,
        viewport.w,
        tab_gap,
        sheet_tab_width,
        &tab_width_context
    );
    can_go_previous = app->first_visible_sheet > 0u;
    can_go_next = page_end < app->merged_sheet_count;
    if (draw_sheet_navigation_button(
        context,
        canvas,
        previous_button,
        -1,
        can_go_previous
    )) {
        app->first_visible_sheet = xls_sheet_tab_previous_page(
            app->first_visible_sheet,
            viewport.w,
            tab_gap,
            sheet_tab_width,
            &tab_width_context
        );
        page_changed = 1;
    }
    if (draw_sheet_navigation_button(
        context,
        canvas,
        next_button,
        1,
        can_go_next
    )) {
        app->first_visible_sheet = page_end;
        page_changed = 1;
    }
    if (page_changed) {
        page_end = xls_sheet_tab_page_end(
            app->first_visible_sheet,
            app->merged_sheet_count,
            viewport.w,
            tab_gap,
            sheet_tab_width,
            &tab_width_context
        );
    }
    (void)snprintf(
        page_text,
        sizeof(page_text),
        "%zu-%zu / %zu",
        app->first_visible_sheet + 1u,
        page_end,
        app->merged_sheet_count
    );
    ui_draw_text(
        canvas,
        nk_rect(9.0f, UI_TOOLBAR_BOTTOM + 3.0f, width - 18.0f, 22.0f),
        page_text,
        fonts->body,
        UI_MUTED,
        NK_TEXT_CENTERED
    );
    x = viewport.x;
    nk_push_scissor(canvas, viewport);
    for (index = app->first_visible_sheet; index < page_end; ++index) {
        const char *name = app->merged_sheets[index].sheet_name;
        const int selected = index == app->selected_sheet;
        float button_width = sheet_tab_width(
            index, &tab_width_context
        );
        struct nk_rect button;
        if (button_width > viewport.w) {
            button_width = viewport.w;
        }
        button = nk_rect(
            x, UI_TOOLBAR_BOTTOM + 30.0f, button_width, 26.0f
        );
        nk_fill_rect(
            canvas,
            nk_rect(button.x, button.y, button_width, button.h),
            13.0f,
            selected ? UI_ACCENT_SOFT : UI_BG2
        );
        nk_stroke_rect(
            canvas,
            nk_rect(
                button.x + 0.5f,
                button.y + 0.5f,
                button_width - 1.0f,
                button.h - 1.0f
            ),
            13.0f,
            1.0f,
            selected ? UI_SUM_BORDER : UI_BORDER
        );
        ui_draw_text_elided(
            canvas,
            nk_rect(button.x + 8.0f, button.y, button_width - 16.0f, button.h),
            name,
            fonts->body,
            selected ? UI_TEXT : UI_MUTED,
            NK_TEXT_CENTERED
        );
        if (ui_clicked(
            context,
            nk_rect(button.x, button.y, button_width, button.h)
        )) {
            app->selected_sheet = index;
            app->has_selection = 0;
            app->first_visible_row = 0;
            app->first_visible_column = 0;
        }
        x += button_width + 8.0f;
    }
    nk_push_scissor(
        canvas,
        nk_rect(0.0f, 0.0f, 32768.0f, 32768.0f)
    );
}

static void render_empty_artwork(
    struct nk_command_buffer *canvas,
    struct nk_rect area
)
{
    const struct nk_rect back_left = nk_rect(area.x + 4.0f, area.y + 14.0f, 88.0f, 69.0f);
    const struct nk_rect back_right = nk_rect(area.x + 51.0f, area.y + 18.0f, 95.0f, 74.0f);
    const struct nk_rect front = nk_rect(area.x + 21.0f, area.y + 5.0f, 106.0f, 80.0f);
    struct nk_rect sheets[] = {back_left, back_right, front};
    size_t sheet_index;
    for (sheet_index = 0; sheet_index < 3; ++sheet_index) {
        const struct nk_rect sheet = sheets[sheet_index];
        size_t row;
        size_t column;
        nk_fill_rect(canvas, sheet, 9.0f, UI_BG1);
        nk_stroke_rect(canvas, sheet, 9.0f, 1.0f, UI_BORDER);
        nk_fill_rect(
            canvas,
            nk_rect(sheet.x + 8.0f, sheet.y + 10.0f, sheet.w - 16.0f, 7.0f),
            3.0f,
            sheet_index == 2 ? nk_rgb(202, 220, 255) : UI_BORDER_SOFT
        );
        for (row = 0; row < 3; ++row) {
            for (column = 0; column < 3; ++column) {
                nk_fill_rect(
                    canvas,
                    nk_rect(
                        sheet.x + 8.0f + (float)column * 29.0f,
                        sheet.y + 27.0f + (float)row * 14.0f,
                        23.0f,
                        9.0f
                    ),
                    2.5f,
                    UI_BORDER_SOFT
                );
            }
        }
    }
    nk_fill_circle(
        canvas,
        nk_rect(area.x + 62.0f, area.y + 84.0f, 20.0f, 20.0f),
        UI_ACCENT
    );
    nk_stroke_line(canvas, area.x + 72.0f, area.y + 89.0f, area.x + 72.0f, area.y + 97.0f, 1.5f, nk_rgb(255, 255, 255));
    nk_stroke_line(canvas, area.x + 68.0f, area.y + 94.0f, area.x + 72.0f, area.y + 98.0f, 1.5f, nk_rgb(255, 255, 255));
    nk_stroke_line(canvas, area.x + 76.0f, area.y + 94.0f, area.x + 72.0f, area.y + 98.0f, 1.5f, nk_rgb(255, 255, 255));
}

static void render_empty_state(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height,
    float status_y
)
{
    const float body_top = UI_TOOLBAR_BOTTOM;
    const float body_height = status_y - body_top;
    const int highlighted =
        app->drop_feedback == APP_DROP_HOVER
        || app->drop_feedback == APP_DROP_IMPORTING;
    const char *drop_title = "拖入 Excel 文件";
    const char *drop_subtitle =
        "支持多个 .xlsx / .xls，自动识别表头与可汇总列";
    const char *shortcut_text =
        app_tr("也可以按 Ctrl+O 选择文件");
    float open_button_width;
    float desired_card_width;
    float card_width;
    float card_height = body_height - 64.0f;
    struct nk_rect card;
    struct nk_rect open_button;
    if (app->drop_feedback == APP_DROP_HOVER) {
        drop_title = "松手即可导入";
        drop_subtitle = "支持多个 .xlsx / .xls";
    } else if (app->drop_feedback == APP_DROP_IMPORTING) {
        drop_title = "已接收，正在导入...";
        drop_subtitle = "正在解析工作簿，请稍候";
    } else if (app->drop_feedback == APP_DROP_REJECTED) {
        drop_title = "文件格式不受支持";
        drop_subtitle = "请选择 .xlsx 或 .xls 工作簿";
    }
    open_button_width = ui_required_button_width(
        fonts->body,
        app_tr("选择文件"),
        106.0f,
        18.0f,
        1
    );
    desired_card_width = 416.0f;
    desired_card_width = ui_max_float(
        desired_card_width,
        ui_text_width(fonts->title, app_tr(drop_title))
            + 64.0f
    );
    desired_card_width = ui_max_float(
        desired_card_width,
        ui_text_width(fonts->body, app_tr(drop_subtitle))
            + 76.0f
    );
    desired_card_width = ui_max_float(
        desired_card_width,
        ui_text_width(fonts->body, shortcut_text)
            + 48.0f
    );
    desired_card_width = ui_max_float(
        desired_card_width, open_button_width + 64.0f
    );
    card_width = desired_card_width;
    if (card_width > width - 64.0f) {
        card_width = width - 64.0f;
    }
    if (card_width < 272.0f) {
        card_width = 272.0f;
    }
    if (card_height > 368.0f) {
        card_height = 368.0f;
    }
    if (card_height < 304.0f) {
        card_height = 304.0f;
    }
    card = nk_rect(
        (width - card_width) * 0.5f,
        body_top + (body_height - card_height) * 0.5f - 5.0f,
        card_width,
        card_height
    );
    nk_fill_rect(
        canvas,
        nk_rect(card.x, card.y + 8.0f, card.w, card.h),
        16.0f,
        nk_rgba(0, 0, 0, highlighted ? 34 : 18)
    );
    nk_fill_rect(
        canvas,
        card,
        16.0f,
        highlighted ? UI_ACCENT_SOFT : UI_BG1
    );
    nk_stroke_rect(
        canvas,
        nk_rect(card.x + 0.5f, card.y + 0.5f, card.w - 1.0f, card.h - 1.0f),
        16.0f,
        highlighted ? 2.5f : 1.0f,
        highlighted ? UI_ACCENT : UI_BORDER
    );

    {
        const struct nk_rect logo = nk_rect(card.x + card.w * 0.5f - 11.0f, card.y + 22.0f, 22.0f, 22.0f);
        nk_fill_rect(canvas, logo, 5.0f, UI_ACCENT);
        nk_fill_rect(canvas, nk_rect(logo.x + 5.0f, logo.y + 4.0f, 12.0f, 14.0f), 2.0f, nk_rgb(255, 255, 255));
        nk_fill_rect(canvas, nk_rect(logo.x + 7.0f, logo.y + 7.0f, 8.0f, 2.0f), 1.0f, nk_rgb(191, 215, 255));
        nk_fill_rect(canvas, nk_rect(logo.x + 7.0f, logo.y + 11.0f, 8.0f, 2.0f), 1.0f, nk_rgb(220, 233, 255));
    }
    ui_draw_text(
        canvas,
        nk_rect(card.x, card.y + 47.0f, card.w, 22.0f),
        app_tr("表表归一"),
        fonts->body,
        UI_TEXT,
        NK_TEXT_CENTERED
    );
    render_empty_artwork(
        canvas,
        nk_rect(card.x + card.w * 0.5f - 72.0f, card.y + 70.0f, 144.0f, 104.0f)
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 32.0f, card.y + 185.0f, card.w - 64.0f, 34.0f),
        app_tr(drop_title),
        fonts->title,
        UI_TEXT,
        NK_TEXT_CENTERED
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 38.0f, card.y + 217.0f, card.w - 76.0f, 28.0f),
        app_tr(drop_subtitle),
        fonts->body,
        UI_MUTED,
        NK_TEXT_CENTERED
    );
    open_button = nk_rect(
        card.x + (card.w - open_button_width) * 0.5f,
        card.y + card.h - 74.0f,
        open_button_width,
        32.0f
    );
    if (ui_draw_button(
        context,
        canvas,
        open_button,
        app_tr("选择文件"),
        fonts->body,
        UI_ICON_FOLDER,
        1
    )) {
        app_open_files(app, 0);
    }
    ui_draw_text(
        canvas,
        nk_rect(card.x, card.y + card.h - 31.0f, card.w, 16.0f),
        shortcut_text,
        fonts->body,
        UI_DISABLED,
        NK_TEXT_CENTERED
    );
    (void)height;
}

static void render_grid(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    struct nk_rect bounds
)
{
    const float row_header_width = 32.0f;
    const float column_width = 89.6f;
    const float header_height = 28.0f;
    const float row_height = 22.4f;
    xls_merged_sheet *sheet;
    size_t visible_row_count;
    size_t visible_column_count;
    size_t visible_row;
    size_t visible_column;
    if (app->selected_sheet >= app->merged_sheet_count) {
        return;
    }
    sheet = &app->merged_sheets[app->selected_sheet];
    visible_row_count = (size_t)((bounds.h - header_height) / row_height) + 1u;
    visible_column_count = (size_t)((bounds.w - row_header_width) / column_width) + 1u;
    if (app->first_visible_row >= sheet->row_count) {
        app->first_visible_row = sheet->row_count == 0 ? 0 : sheet->row_count - 1;
    }
    if (app->first_visible_column >= sheet->column_count) {
        app->first_visible_column = sheet->column_count == 0 ? 0 : sheet->column_count - 1;
    }

    nk_fill_rect(canvas, bounds, 0.0f, UI_BG1);
    nk_push_scissor(canvas, bounds);
    nk_fill_rect(
        canvas,
        nk_rect(bounds.x, bounds.y, bounds.w, header_height),
        0.0f,
        UI_BG2
    );
    nk_fill_rect(
        canvas,
        nk_rect(bounds.x, bounds.y, row_header_width, bounds.h),
        0.0f,
        UI_BG2
    );

    for (visible_column = 0;
         visible_column < visible_column_count;
         ++visible_column) {
        const size_t column = app->first_visible_column + visible_column;
        const float x = bounds.x + row_header_width
            + (float)visible_column * column_width;
        char letters[16];
        if (column >= sheet->column_count) {
            break;
        }
        ui_column_letters(column, letters, sizeof(letters));
        nk_stroke_rect(
            canvas,
            nk_rect(x, bounds.y, column_width, header_height),
            0.0f,
            1.0f,
            UI_BORDER_SOFT
        );
        ui_draw_text(
            canvas,
            nk_rect(x, bounds.y, column_width, header_height),
            letters,
            fonts->body,
            UI_MUTED,
            NK_TEXT_CENTERED
        );
    }

    for (visible_row = 0; visible_row < visible_row_count; ++visible_row) {
        const size_t row = app->first_visible_row + visible_row;
        const float y = bounds.y + header_height + (float)visible_row * row_height;
        char row_number[24];
        if (row >= sheet->row_count || y >= bounds.y + bounds.h) {
            break;
        }
        (void)snprintf(row_number, sizeof(row_number), "%zu", row + 1);
        nk_stroke_rect(
            canvas,
            nk_rect(bounds.x, y, row_header_width, row_height),
            0.0f,
            1.0f,
            UI_BORDER_SOFT
        );
        ui_draw_text(
            canvas,
            nk_rect(bounds.x, y, row_header_width - 5.0f, row_height),
            row_number,
            fonts->body,
            UI_MUTED,
            NK_TEXT_RIGHT
        );
        for (visible_column = 0;
             visible_column < visible_column_count;
             ++visible_column) {
            const size_t column = app->first_visible_column + visible_column;
            const xls_merged_cell *cell;
            const float x = bounds.x + row_header_width
                + (float)visible_column * column_width;
            struct nk_rect cell_rect;
            struct nk_color background;
            struct nk_color foreground;
            int selected;
            int same_axis;
            if (column >= sheet->column_count || x >= bounds.x + bounds.w) {
                break;
            }
            cell = xls_merged_sheet_cell(sheet, row, column);
            cell_rect = nk_rect(x, y, column_width, row_height);
            selected = app->has_selection
                && app->selected_row == row
                && app->selected_column == column;
            same_axis = app->has_selection
                && (app->selected_row == row || app->selected_column == column);
            background = cell != NULL && cell->kind == XLS_CELL_SUM
                ? UI_SUM_BG
                : UI_BG1;
            if (same_axis) {
                background = cell != NULL && cell->kind == XLS_CELL_SUM
                    ? nk_rgb(235, 244, 255)
                    : nk_rgb(248, 250, 255);
            }
            if (selected) {
                background = UI_ACCENT_SOFT;
            }
            foreground = cell != NULL && cell->kind == XLS_CELL_SUM
                ? UI_SUM_FG
                : UI_TEXT;
            nk_fill_rect(canvas, cell_rect, 0.0f, background);
            nk_stroke_rect(canvas, cell_rect, 0.0f, 1.0f, UI_BORDER_SOFT);
            if (cell != NULL && cell->display_value != NULL) {
                const int align_right = cell->kind == XLS_CELL_SUM;
                ui_draw_text_elided(
                    canvas,
                    nk_rect(
                        cell_rect.x + 7.0f,
                        cell_rect.y,
                        cell_rect.w - 14.0f,
                        cell_rect.h
                    ),
                    cell->display_value,
                    fonts->body,
                    foreground,
                    align_right ? NK_TEXT_RIGHT : NK_TEXT_LEFT
                );
            }
            if (cell != NULL && cell->is_overridden != 0u) {
                nk_fill_circle(
                    canvas,
                    nk_rect(cell_rect.x + cell_rect.w - 9.0f, cell_rect.y + 4.0f, 4.0f, 4.0f),
                    nk_rgb(38, 158, 143)
                );
            }
            if (selected) {
                nk_stroke_rect(
                    canvas,
                    nk_rect(cell_rect.x + 1.0f, cell_rect.y + 1.0f, cell_rect.w - 2.0f, cell_rect.h - 2.0f),
                    0.0f,
                    2.0f,
                    UI_ACCENT
                );
            }
            if (ui_clicked(context, cell_rect)) {
                app->selected_row = row;
                app->selected_column = column;
                app->has_selection = 1;
            }
        }
    }
    nk_push_scissor(canvas, nk_rect(0.0f, 0.0f, 32768.0f, 32768.0f));
}

static int ui_source_is_outlier(
    const xls_source_overview *overview,
    size_t source_index
)
{
    size_t index;
    for (index = 0; index < overview->outlier_count; ++index) {
        if (overview->outlier_indexes[index] == source_index) {
            return 1;
        }
    }
    return 0;
}

static void render_inspector(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    struct nk_rect bounds
)
{
    const float margin = 12.0f;
    const struct nk_rect placeholder = nk_rect(
        bounds.x + margin,
        bounds.y + 12.0f,
        bounds.w - margin * 2.0f,
        37.0f
    );
    nk_fill_rect(canvas, bounds, 0.0f, UI_CHROME);
    nk_stroke_line(canvas, bounds.x + 0.5f, bounds.y, bounds.x + 0.5f, bounds.y + bounds.h, 1.0f, UI_BORDER);

    if (!app->has_selection || app->selected_sheet >= app->merged_sheet_count) {
        app->source_scroll_selection_valid = 0;
        app->source_scroll_dragging = 0;
        app->source_scroll_offset = 0.0;
        app->source_scroll_maximum = 0.0;
        app->source_scroll_viewport_height = 0.0;
        nk_fill_rect(canvas, placeholder, 9.0f, UI_BG1);
        nk_stroke_rect(canvas, placeholder, 9.0f, 1.0f, UI_BORDER_SOFT);
        ui_draw_text(
            canvas,
            nk_rect(placeholder.x + 10.0f, placeholder.y, placeholder.w - 20.0f, placeholder.h),
            app_tr("选择一个单元格查看来源。"),
            fonts->body,
            UI_MUTED,
            NK_TEXT_LEFT
        );
        return;
    }

    {
        xls_merged_cell *cell = xls_merged_sheet_cell_mutable(
            &app->merged_sheets[app->selected_sheet],
            app->selected_row,
            app->selected_column
        );
        struct nk_rect detail;
        struct nk_rect source_card;
        struct nk_rect label_button;
        struct nk_rect sum_button;
        char reference[32];
        char letters[16];
        const char *kind_text;
        float badge_width;
        xls_source_overview overview;
        xls_error error;
        int overview_ok;
        size_t source_index;
        float source_card_height;
        if (cell == NULL) {
            return;
        }
        if (!app->source_scroll_selection_valid
            || app->source_scroll_sheet != app->selected_sheet
            || app->source_scroll_row != app->selected_row
            || app->source_scroll_column != app->selected_column) {
            app->source_scroll_offset = 0.0;
            app->source_scroll_maximum = 0.0;
            app->source_scroll_viewport_height = 0.0;
            app->source_scroll_dragging = 0;
            app->source_scroll_selection_valid = 1;
            app->source_scroll_sheet = app->selected_sheet;
            app->source_scroll_row = app->selected_row;
            app->source_scroll_column = app->selected_column;
        }
        ui_column_letters(app->selected_column, letters, sizeof(letters));
        (void)snprintf(reference, sizeof(reference), "%s%zu", letters, app->selected_row + 1);
        kind_text = ui_kind_text(cell->kind);
        badge_width = ui_text_width(fonts->body, kind_text) + 18.0f;
        detail = nk_rect(
            bounds.x + margin,
            bounds.y + 12.0f,
            bounds.w - margin * 2.0f,
            cell->is_overridden != 0u ? 157.0f : 137.0f
        );
        nk_fill_rect(canvas, detail, 10.0f, UI_BG1);
        nk_stroke_rect(canvas, detail, 10.0f, 1.0f, UI_BORDER_SOFT);
        ui_draw_text(
            canvas,
            nk_rect(detail.x + 11.0f, detail.y + 9.0f, 60.0f, 20.0f),
            reference,
            fonts->source_value,
            UI_MUTED,
            NK_TEXT_LEFT
        );
        {
            const struct nk_rect badge = nk_rect(
                detail.x + detail.w - badge_width - 10.0f,
                detail.y + 8.0f,
                badge_width,
                20.0f
            );
            const int is_sum = cell->kind == XLS_CELL_SUM;
            nk_fill_rect(canvas, badge, 7.0f, is_sum ? UI_SUM_BG : UI_LABEL_BG);
            nk_stroke_rect(
                canvas,
                badge,
                7.0f,
                1.0f,
                is_sum ? UI_SUM_BORDER : UI_LABEL_BORDER
            );
            ui_draw_text(
                canvas,
                badge,
                kind_text,
                fonts->body,
                is_sum ? UI_SUM_FG : UI_LABEL_FG,
                NK_TEXT_CENTERED
            );
        }
        {
            const char *display_text =
                cell->display_value == NULL || cell->display_value[0] == '\0'
                    ? app_tr("空值")
                    : cell->display_value;
            ui_draw_text_elided(
                canvas,
                nk_rect(
                    detail.x + 12.0f,
                    detail.y + 35.0f,
                    detail.w - 24.0f,
                    31.0f
                ),
                display_text,
                ui_text_has_non_ascii(display_text)
                    ? fonts->body
                    : fonts->numeric,
                UI_TEXT,
                NK_TEXT_CENTERED
            );
        }
        ui_draw_text_elided(
            canvas,
            nk_rect(detail.x + 12.0f, detail.y + 70.0f, detail.w - 24.0f, 20.0f),
            cell->is_overridden != 0u
                ? app_tr("当前使用手动修正后的单元格类型")
                : app_tr(cell->decision.reason),
            fonts->body,
            UI_MUTED,
            NK_TEXT_CENTERED
        );
        label_button = nk_rect(detail.x + detail.w * 0.5f - 45.0f, detail.y + 98.0f, 41.0f, 28.0f);
        sum_button = nk_rect(detail.x + detail.w * 0.5f + 4.0f, detail.y + 98.0f, 41.0f, 28.0f);
        nk_fill_rect(
            canvas,
            label_button,
            7.0f,
            cell->kind == XLS_CELL_LABEL ? UI_LABEL_BG : UI_BG2
        );
        nk_stroke_rect(
            canvas,
            label_button,
            7.0f,
            1.0f,
            cell->kind == XLS_CELL_LABEL ? UI_LABEL_BORDER : UI_BORDER
        );
        ui_draw_text(
            canvas,
            label_button,
            app_tr("标签"),
            fonts->body,
            cell->kind == XLS_CELL_LABEL ? UI_LABEL_FG : UI_TEXT,
            NK_TEXT_CENTERED
        );
        nk_fill_rect(
            canvas,
            sum_button,
            7.0f,
            cell->kind == XLS_CELL_SUM ? UI_SUM_BG : UI_BG2
        );
        nk_stroke_rect(
            canvas,
            sum_button,
            7.0f,
            1.0f,
            cell->kind == XLS_CELL_SUM ? UI_SUM_BORDER : UI_BORDER
        );
        ui_draw_text(
            canvas,
            sum_button,
            app_tr("求和"),
            fonts->body,
            cell->kind == XLS_CELL_SUM ? UI_SUM_FG : UI_TEXT,
            NK_TEXT_CENTERED
        );
        if (ui_clicked(context, label_button)) {
            app_set_selected_kind(app, XLS_CELL_LABEL);
        }
        if (ui_clicked(context, sum_button)) {
            app_set_selected_kind(app, XLS_CELL_SUM);
        }
        if (cell->is_overridden != 0u) {
            const struct nk_rect restore = nk_rect(
                detail.x + 72.0f,
                detail.y + 130.0f,
                detail.w - 144.0f,
                18.0f
            );
            ui_draw_text(
                canvas,
                restore,
                app_tr("恢复自动判断"),
                fonts->body,
                UI_MUTED,
                NK_TEXT_CENTERED
            );
            if (ui_clicked(context, restore)) {
                app_restore_selected_kind(app);
            }
        }

        memset(&overview, 0, sizeof(overview));
        overview_ok = xls_analyze_sources(
            cell->sources,
            cell->source_count,
            &overview,
            &error
        );
        source_card_height = 58.0f
            + (app->sources_expanded
                ? (float)cell->source_count
                    * (float)UI_SOURCE_ROW_HEIGHT
                : 0.0f);
        source_card = nk_rect(
            bounds.x + margin,
            detail.y + detail.h + 12.0f,
            bounds.w - margin * 2.0f,
            source_card_height
        );
        if (source_card.y + source_card.h > bounds.y + bounds.h - 12.0f) {
            source_card.h = bounds.y + bounds.h - 12.0f - source_card.y;
        }
        nk_fill_rect(canvas, source_card, 10.0f, UI_BG1);
        nk_stroke_rect(canvas, source_card, 10.0f, 1.0f, UI_BORDER_SOFT);
        {
            const struct nk_rect toggle = nk_rect(source_card.x + 10.0f, source_card.y + 7.0f, 100.0f, 23.0f);
            const float arrow_x = toggle.x + 3.0f;
            const float arrow_y = toggle.y + 8.0f;
            if (app->sources_expanded) {
                nk_fill_triangle(
                    canvas,
                    arrow_x,
                    arrow_y,
                    arrow_x + 8.0f,
                    arrow_y,
                    arrow_x + 4.0f,
                    arrow_y + 5.0f,
                    UI_TEXT
                );
            } else {
                nk_fill_triangle(
                    canvas,
                    arrow_x,
                    arrow_y,
                    arrow_x,
                    arrow_y + 8.0f,
                    arrow_x + 5.0f,
                    arrow_y + 4.0f,
                    UI_TEXT
                );
            }
            ui_draw_text(
                canvas,
                nk_rect(toggle.x + 14.0f, toggle.y, 78.0f, toggle.h),
                app_tr("来源明细"),
                fonts->body,
                UI_TEXT,
                NK_TEXT_LEFT
            );
            if (ui_clicked(context, toggle)) {
                app->sources_expanded = !app->sources_expanded;
            }
        }
        {
            char count[24];
            (void)snprintf(count, sizeof(count), "%zu", cell->source_count);
            nk_fill_rect(
                canvas,
                nk_rect(source_card.x + source_card.w - 31.0f, source_card.y + 8.0f, 21.0f, 18.0f),
                8.0f,
                UI_BG2
            );
            ui_draw_text(
                canvas,
                nk_rect(source_card.x + source_card.w - 31.0f, source_card.y + 8.0f, 21.0f, 18.0f),
                count,
                fonts->body,
                UI_MUTED,
                NK_TEXT_CENTERED
            );
        }
        {
            char summary[96];
            if (overview_ok) {
                (void)snprintf(
                    summary,
                    sizeof(summary),
                    app_tr("%zu 个有效"),
                    overview.value_count
                );
            } else {
                (void)snprintf(
                    summary,
                    sizeof(summary),
                    app_tr("%zu 个来源"),
                    cell->source_count
                );
            }
            ui_draw_text(
                canvas,
                nk_rect(source_card.x + 10.0f, source_card.y + 31.0f, source_card.w - 20.0f, 18.0f),
                summary,
                fonts->body,
                UI_MUTED,
                NK_TEXT_LEFT
            );
        }
        if (app->sources_expanded) {
            const struct nk_rect viewport = nk_rect(
                source_card.x + 1.0f,
                source_card.y + 57.0f,
                source_card.w - 2.0f,
                source_card.h - 58.0f
            );
            const double viewport_height = viewport.h > 0.0f
                ? (double)viewport.h
                : 0.0;
            const int pointer_in_viewport = ui_hovered(
                context, viewport
            );
            double content_width;
            app->source_scroll_maximum = xls_source_list_max_offset(
                cell->source_count,
                UI_SOURCE_ROW_HEIGHT,
                viewport_height
            );
            app->source_scroll_viewport_height = viewport_height;
            content_width = app->source_scroll_maximum > 0.0
                ? (double)viewport.w - 9.0
                : (double)viewport.w;
            app->source_scroll_offset = xls_source_list_clamp_offset(
                app->source_scroll_offset,
                cell->source_count,
                UI_SOURCE_ROW_HEIGHT,
                viewport_height
            );
            nk_push_scissor(canvas, viewport);
            for (source_index = 0; source_index < cell->source_count; ++source_index) {
                const xls_source_entry *source = &cell->sources[source_index];
                const float row_y = viewport.y
                    + (float)(
                        (double)source_index * UI_SOURCE_ROW_HEIGHT
                        - app->source_scroll_offset
                    );
                const char *value = source->state == XLS_SOURCE_MISSING
                    ? app_tr("缺失")
                    : source->state == XLS_SOURCE_EMPTY
                        ? app_tr("空值")
                        : source->value;
                const int outlier = overview_ok
                    && ui_source_is_outlier(&overview, source_index);
                const struct nk_rect row = nk_rect(
                    viewport.x,
                    row_y,
                    (float)content_width,
                    (float)UI_SOURCE_ROW_HEIGHT
                );
                const struct nk_rect heading = nk_rect(
                    row.x + 9.0f,
                    row.y + 4.0f,
                    row.w - 18.0f,
                    18.0f
                );
                const int row_hovered = pointer_in_viewport
                    && ui_hovered(context, row);
                const int heading_hovered = pointer_in_viewport
                    && ui_hovered(context, heading);
                const char *filename =
                    source->filename == NULL
                        || source->filename[0] == '\0'
                    ? app_tr("未知文件")
                    : source->filename;
                const char *copy_path =
                    source->filepath == NULL
                        || source->filepath[0] == '\0'
                    ? source->filename
                    : source->filepath;
                const int has_path =
                    copy_path != NULL && copy_path[0] != '\0';
                const int show_reveal = heading_hovered
                    && source->filepath != NULL
                    && source->filepath[0] != '\0';
                const char *reveal_text = app_tr("定位");
                const float reveal_width = ui_max_float(
                    42.0f,
                    ui_text_width(fonts->body, reveal_text) + 14.0f
                );
                const struct nk_rect reveal_button = nk_rect(
                    heading.x + heading.w - reveal_width,
                    heading.y - 1.0f,
                    reveal_width,
                    20.0f
                );
                const struct nk_rect filename_target = nk_rect(
                    heading.x,
                    heading.y,
                    heading.w - (show_reveal
                        ? reveal_width + 5.0f
                        : 0.0f),
                    heading.h
                );
                const struct nk_user_font *value_font =
                    ui_text_has_non_ascii(value)
                        ? fonts->body
                        : fonts->source_value;
                float value_width = ui_text_width(
                    value_font, value == NULL ? "" : value
                ) + 10.0f;
                struct nk_rect value_target;
                int filename_hovered;
                int value_hovered;
                if (row_y >= viewport.y + viewport.h) {
                    break;
                }
                if (row_y + (float)UI_SOURCE_ROW_HEIGHT
                    <= viewport.y) {
                    continue;
                }
                if (row_hovered) {
                    nk_fill_rect(canvas, row, 7.0f, UI_BG2);
                }
                if (source_index > 0) {
                    nk_stroke_line(
                        canvas,
                        row.x + 9.0f,
                        row_y,
                        row.x + row.w - 9.0f,
                        row_y,
                        1.0f,
                        UI_BORDER_SOFT
                    );
                }
                filename_hovered = pointer_in_viewport
                    && has_path
                    && ui_hovered(context, filename_target);
                if (filename_hovered) {
                    nk_fill_rect(
                        canvas,
                        filename_target,
                        5.0f,
                        UI_ACCENT_SOFT
                    );
                }
                ui_draw_text_elided(
                    canvas,
                    nk_rect(
                        filename_target.x + 3.0f,
                        filename_target.y,
                        filename_target.w - 6.0f,
                        filename_target.h
                    ),
                    filename,
                    fonts->body,
                    filename_hovered ? UI_ACCENT : UI_MUTED,
                    NK_TEXT_LEFT
                );
                if (show_reveal) {
                    const int reveal_hovered = ui_hovered(
                        context, reveal_button
                    );
                    if (reveal_hovered) {
                        nk_fill_rect(
                            canvas,
                            reveal_button,
                            6.0f,
                            UI_ACCENT_SOFT
                        );
                    }
                    ui_draw_text(
                        canvas,
                        reveal_button,
                        reveal_text,
                        fonts->body,
                        reveal_hovered ? UI_ACCENT : UI_MUTED,
                        NK_TEXT_CENTERED
                    );
                    if (pointer_in_viewport
                        && ui_clicked(context, reveal_button)) {
                        ++app->source_reveal_attempt_count;
                        if (!xls_platform_reveal_file(
                            source->filepath
                        )) {
                            ++app->source_reveal_failure_count;
                            app_set_interaction_feedback(
                                app, "找不到来源文件"
                            );
                        }
                    }
                }
                if (filename_hovered
                    && ui_clicked(context, filename_target)
                    && SDL_SetClipboardText(copy_path) == 0) {
                    app_set_interaction_feedback(
                        app, "已复制文件路径"
                    );
                }
                if (value_width > row.w - 18.0f) {
                    value_width = row.w - 18.0f;
                }
                value_target = nk_rect(
                    row.x + row.w - 9.0f - value_width,
                    row_y + 26.0f,
                    value_width,
                    21.0f
                );
                value_hovered = pointer_in_viewport
                    && source->state == XLS_SOURCE_VALUE
                    && value != NULL
                    && ui_hovered(context, value_target);
                if (value_hovered) {
                    nk_fill_rect(
                        canvas,
                        value_target,
                        5.0f,
                        UI_ACCENT_SOFT
                    );
                }
                ui_draw_text_elided(
                    canvas,
                    nk_rect(
                        value_target.x + 3.0f,
                        value_target.y,
                        value_target.w - 6.0f,
                        value_target.h
                    ),
                    value == NULL ? "" : value,
                    value_font,
                    value_hovered
                        ? UI_ACCENT
                        : outlier ? UI_WARNING : UI_TEXT,
                    NK_TEXT_RIGHT
                );
                if (value_hovered
                    && ui_clicked(context, value_target)
                    && SDL_SetClipboardText(value) == 0) {
                    app_set_interaction_feedback(
                        app, "已复制来源值"
                    );
                }
            }
            if (app->source_scroll_maximum > 0.0
                && viewport.h > 16.0f) {
                const struct nk_rect track = nk_rect(
                    viewport.x + viewport.w - 7.0f,
                    viewport.y + 4.0f,
                    4.0f,
                    viewport.h - 8.0f
                );
                const struct nk_rect track_target = nk_rect(
                    track.x - 3.0f,
                    track.y,
                    10.0f,
                    track.h
                );
                const double content_height =
                    (double)cell->source_count
                        * UI_SOURCE_ROW_HEIGHT;
                float thumb_height = (float)(
                    (double)track.h * viewport_height
                    / content_height
                );
                float thumb_y;
                struct nk_rect thumb;
                struct nk_rect thumb_target;
                if (thumb_height < 24.0f) {
                    thumb_height = 24.0f;
                }
                if (thumb_height > track.h) {
                    thumb_height = track.h;
                }
                thumb_y = track.y + (float)(
                    ((double)track.h - (double)thumb_height)
                    * app->source_scroll_offset
                    / app->source_scroll_maximum
                );
                thumb = nk_rect(
                    track.x, thumb_y, track.w, thumb_height
                );
                thumb_target = nk_rect(
                    track_target.x,
                    thumb.y,
                    track_target.w,
                    thumb.h
                );
                nk_fill_rect(
                    canvas, track, 2.0f, UI_BORDER_SOFT
                );
                nk_fill_rect(
                    canvas,
                    thumb,
                    2.0f,
                    ui_hovered(context, thumb_target)
                        || app->source_scroll_dragging
                        ? UI_MUTED
                        : UI_DISABLED
                );
                if (pointer_in_viewport
                    && ui_clicked(context, thumb_target)) {
                    app->source_scroll_dragging = 1;
                    app->source_scroll_drag_anchor =
                        (double)context->input.mouse.pos.y
                            - (double)thumb.y;
                } else if (pointer_in_viewport
                    && ui_clicked(context, track_target)) {
                    const double direction =
                        (double)context->input.mouse.pos.y
                            < (double)thumb.y
                        ? -1.0
                        : 1.0;
                    app->source_scroll_offset =
                        xls_source_list_clamp_offset(
                            app->source_scroll_offset
                                + direction * viewport_height,
                            cell->source_count,
                            UI_SOURCE_ROW_HEIGHT,
                            viewport_height
                        );
                }
                if (app->source_scroll_dragging) {
                    if (nk_input_is_mouse_down(
                        &context->input, NK_BUTTON_LEFT
                    )) {
                        const double travel =
                            (double)track.h
                                - (double)thumb_height;
                        const double position =
                            (double)context->input.mouse.pos.y
                                - (double)track.y
                                - app->source_scroll_drag_anchor;
                        app->source_scroll_offset =
                            travel <= 0.0
                            ? 0.0
                            : xls_source_list_clamp_offset(
                                position / travel
                                    * app->source_scroll_maximum,
                                cell->source_count,
                                UI_SOURCE_ROW_HEIGHT,
                                viewport_height
                            );
                    } else {
                        app->source_scroll_dragging = 0;
                    }
                }
            }
            nk_push_scissor(canvas, nk_rect(0.0f, 0.0f, 32768.0f, 32768.0f));
        } else {
            app->source_scroll_offset = 0.0;
            app->source_scroll_maximum = 0.0;
            app->source_scroll_viewport_height = 0.0;
            app->source_scroll_dragging = 0;
        }
        if (overview_ok) {
            xls_source_overview_free(&overview);
        }
    }
}

static void render_status_bar(
    struct nk_command_buffer *canvas,
    const app_state *app,
    const ui_fonts *fonts,
    float width,
    float y
)
{
    char viewing[512];
    const char *text = app->status;
    if (app->interaction_feedback_until > SDL_GetTicks64()) {
        text = app->status;
    } else if (app->merged_sheet_count > 0
        && app->selected_sheet < app->merged_sheet_count) {
        const xls_merged_sheet *sheet = &app->merged_sheets[app->selected_sheet];
        (void)snprintf(
            viewing,
            sizeof(viewing),
            app_tr("正在查看“%s”，共 %zu 行。"),
            sheet->sheet_name,
            sheet->row_count
        );
        text = viewing;
    }
    nk_fill_rect(canvas, nk_rect(0.0f, y, width, 17.0f), 0.0f, UI_BG0);
    nk_stroke_line(canvas, 0.0f, y + 0.5f, width, y + 0.5f, 1.0f, UI_BORDER);
    ui_draw_text_elided(
        canvas,
        nk_rect(2.0f, y, width - 4.0f, 17.0f),
        text,
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
}

static void render_correction_bar(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float y
)
{
    const float clear_width = ui_required_button_width(
        fonts->body,
        app_tr("清除所有修正"),
        101.0f,
        24.0f,
        0
    );
    const struct nk_rect clear_button = nk_rect(
        width - clear_width - 11.0f,
        y + 5.0f,
        clear_width,
        28.0f
    );
    nk_fill_rect(canvas, nk_rect(0.0f, y, width, 38.0f), 0.0f, UI_BG1);
    nk_stroke_line(canvas, 0.0f, y + 0.5f, width, y + 0.5f, 1.0f, UI_BORDER);
    ui_draw_text(
        canvas,
        nk_rect(
            14.0f,
            y,
            width - clear_width - 40.0f,
            38.0f
        ),
        app_tr("当前工作区包含手动修正。"),
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    if (ui_draw_button(
        context,
        canvas,
        clear_button,
        app_tr("清除所有修正"),
        fonts->body,
        UI_ICON_NONE,
        0
    )) {
        app_clear_overrides(app);
    }
}

static void render_dialog_backdrop(
    struct nk_command_buffer *canvas,
    float width,
    float height
)
{
    nk_fill_rect(
        canvas,
        nk_rect(0.0f, 0.0f, width, height),
        0.0f,
        nk_rgba(22, 29, 40, 90)
    );
}

static int render_dialog_close(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    struct nk_rect card
)
{
    const struct nk_rect close = nk_rect(
        card.x + card.w - 36.0f, card.y + 12.0f, 24.0f, 24.0f
    );
    if (ui_hovered(context, close)) {
        nk_fill_circle(canvas, close, UI_BORDER_SOFT);
    }
    ui_draw_icon(
        canvas,
        UI_ICON_CLOSE,
        close.x + 5.0f,
        close.y + 5.0f,
        UI_MUTED
    );
    return ui_clicked(context, close);
}

static void render_notice_dialog(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height
)
{
    const float button_width = ui_required_button_width(
        fonts->body,
        app_tr("确定"),
        80.0f,
        28.0f,
        0
    );
    float card_width = 500.0f;
    const float card_height = 248.0f;
    card_width = ui_max_float(
        card_width,
        ui_text_width(fonts->body, app->dialog_title)
            + 96.0f
    );
    card_width = ui_max_float(
        card_width,
        ui_multiline_max_width(
            fonts->body, app->dialog_message
        ) + 48.0f
    );
    card_width = ui_max_float(
        card_width, button_width + 48.0f
    );
    if (card_width > width - 60.0f) {
        card_width = width - 60.0f;
    }
    const struct nk_rect card = nk_rect(
        (width - card_width) * 0.5f,
        (height - card_height) * 0.5f,
        card_width,
        card_height
    );
    render_dialog_backdrop(canvas, width, height);
    nk_fill_rect(
        canvas,
        nk_rect(card.x + 3.0f, card.y + 7.0f, card.w, card.h),
        13.0f,
        nk_rgba(0, 0, 0, 24)
    );
    nk_fill_rect(canvas, card, 13.0f, UI_BG1);
    nk_stroke_rect(canvas, card, 13.0f, 1.0f, UI_BORDER);
    ui_draw_text(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 18.0f, card.w - 72.0f, 28.0f),
        app->dialog_title,
        fonts->body,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    ui_draw_multiline(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 64.0f, card.w - 48.0f, 110.0f),
        app->dialog_message,
        fonts->body,
        app->dialog_error ? nk_rgb(204, 53, 53) : UI_MUTED,
        23.0f
    );
    if (render_dialog_close(context, canvas, card)
        || ui_draw_button(
            context,
            canvas,
            nk_rect(
                card.x + card.w - button_width - 24.0f,
                card.y + card.h - 52.0f,
                button_width,
                32.0f
            ),
            app_tr("确定"),
            fonts->body,
            UI_ICON_NONE,
            1
        )) {
        app_close_dialog(app);
    }
}

static void render_update_dialog(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height
)
{
    const char *later_text = app_tr("稍后提醒");
    const char *download_text = app_tr("立即下载");
    const float later_width = ui_required_button_width(
        fonts->body, later_text, 96.0f, 28.0f, 0
    );
    const float download_width = ui_required_button_width(
        fonts->body, download_text, 104.0f, 28.0f, 0
    );
    float card_width = 540.0f;
    const float card_height = 360.0f;
    char version_message[256];
    struct nk_rect card;
    if (card_width > width - 60.0f) {
        card_width = width - 60.0f;
    }
    card = nk_rect(
        (width - card_width) * 0.5f,
        (height - card_height) * 0.5f,
        card_width,
        card_height
    );
    (void)snprintf(
        version_message,
        sizeof(version_message),
        app_tr("发现新版本 %s，可前往 Z-PULSE.CN 下载。"),
        app->update_version
    );
    render_dialog_backdrop(canvas, width, height);
    nk_fill_rect(
        canvas,
        nk_rect(card.x + 3.0f, card.y + 7.0f, card.w, card.h),
        13.0f,
        nk_rgba(0, 0, 0, 24)
    );
    nk_fill_rect(canvas, card, 13.0f, UI_BG1);
    nk_stroke_rect(canvas, card, 13.0f, 1.0f, UI_BORDER);
    ui_draw_text(
        canvas,
        nk_rect(
            card.x + 24.0f,
            card.y + 18.0f,
            card.w - 72.0f,
            28.0f
        ),
        app_tr("发现新版本"),
        fonts->body,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    ui_draw_text(
        canvas,
        nk_rect(
            card.x + 24.0f,
            card.y + 60.0f,
            card.w - 48.0f,
            24.0f
        ),
        version_message,
        fonts->body,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    ui_draw_text(
        canvas,
        nk_rect(
            card.x + 24.0f,
            card.y + 88.0f,
            card.w - 48.0f,
            22.0f
        ),
        app_tr("新版本已发布，建议您更新以获得更好的体验。"),
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    nk_fill_rect(
        canvas,
        nk_rect(
            card.x + 24.0f,
            card.y + 122.0f,
            card.w - 48.0f,
            154.0f
        ),
        8.0f,
        UI_BG2
    );
    nk_stroke_rect(
        canvas,
        nk_rect(
            card.x + 24.0f,
            card.y + 122.0f,
            card.w - 48.0f,
            154.0f
        ),
        8.0f,
        1.0f,
        UI_BORDER_SOFT
    );
    ui_draw_wrapped_text(
        canvas,
        nk_rect(
            card.x + 38.0f,
            card.y + 134.0f,
            card.w - 76.0f,
            130.0f
        ),
        app->update_changelog[0] == '\0'
            ? app_tr("新版本已发布，建议您更新以获得更好的体验。")
            : app->update_changelog,
        fonts->body,
        UI_MUTED,
        23.0f
    );
    if (render_dialog_close(context, canvas, card)
        || ui_draw_button(
            context,
            canvas,
            nk_rect(
                card.x + card.w - download_width
                    - later_width - 34.0f,
                card.y + card.h - 56.0f,
                later_width,
                34.0f
            ),
            later_text,
            fonts->body,
            UI_ICON_NONE,
            0
        )) {
        app_close_dialog(app);
        return;
    }
    if (ui_draw_button(
        context,
        canvas,
        nk_rect(
            card.x + card.w - download_width - 24.0f,
            card.y + card.h - 56.0f,
            download_width,
            34.0f
        ),
        download_text,
        fonts->body,
        UI_ICON_NONE,
        1
    )) {
        (void)xls_platform_open_url(app->update_download_url);
        app_close_dialog(app);
    }
}

static void render_rules_dialog(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height
)
{
    const float close_width = ui_required_button_width(
        fonts->body,
        app_tr("关闭"),
        80.0f,
        28.0f,
        0
    );
    const float save_width = ui_required_button_width(
        fonts->body,
        app_tr("保存规则..."),
        132.0f,
        28.0f,
        0
    );
    float card_width = 560.0f;
    card_width = ui_max_float(
        card_width,
        ui_text_width(fonts->body, app_tr("当前修正规则"))
            + 96.0f
    );
    card_width = ui_max_float(
        card_width,
        ui_text_width(fonts->body, app->dialog_message)
            + 48.0f
    );
    card_width = ui_max_float(
        card_width, close_width + save_width + 82.0f
    );
    if (card_width > width - 60.0f) {
        card_width = width - 60.0f;
    }
    const struct nk_rect card = nk_rect(
        (width - card_width) * 0.5f,
        (height - 420.0f) * 0.5f,
        card_width,
        420.0f
    );
    float y = card.y + 100.0f;
    size_t displayed = 0u;
    size_t sheet_index;
    render_dialog_backdrop(canvas, width, height);
    nk_fill_rect(canvas, card, 13.0f, UI_BG1);
    nk_stroke_rect(canvas, card, 13.0f, 1.0f, UI_BORDER);
    ui_draw_text(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 18.0f, card.w - 72.0f, 28.0f),
        app_tr("当前修正规则"),
        fonts->body,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 55.0f, card.w - 48.0f, 24.0f),
        app->dialog_message,
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    for (sheet_index = 0u;
         sheet_index < app->merged_sheet_count && displayed < 9u;
         ++sheet_index) {
        const xls_merged_sheet *sheet = &app->merged_sheets[sheet_index];
        size_t row;
        for (row = 0u; row < sheet->row_count && displayed < 9u; ++row) {
            size_t column;
            for (column = 0u;
                 column < sheet->column_count && displayed < 9u;
                 ++column) {
                const xls_merged_cell *cell = xls_merged_sheet_cell(
                    sheet, row, column
                );
                char letters[16];
                char line[256];
                if (cell == NULL || cell->is_overridden == 0u) {
                    continue;
                }
                ui_column_letters(column, letters, sizeof(letters));
                (void)snprintf(
                    line,
                    sizeof(line),
                    "%s · %s%zu    →    %s",
                    sheet->sheet_name,
                    letters,
                    row + 1u,
                    ui_kind_text(cell->kind)
                );
                nk_fill_rect(
                    canvas,
                    nk_rect(card.x + 24.0f, y, card.w - 48.0f, 27.0f),
                    5.0f,
                    displayed % 2u == 0u ? UI_BG2 : UI_BG1
                );
                ui_draw_text_elided(
                    canvas,
                    nk_rect(card.x + 34.0f, y, card.w - 68.0f, 27.0f),
                    line,
                    fonts->body,
                    UI_TEXT,
                    NK_TEXT_LEFT
                );
                y += 29.0f;
                ++displayed;
            }
        }
    }
    if (render_dialog_close(context, canvas, card)
        || ui_draw_button(
            context,
            canvas,
            nk_rect(
                card.x + card.w - close_width - 24.0f,
                card.y + card.h - 52.0f,
                close_width,
                32.0f
            ),
            app_tr("关闭"),
            fonts->body,
            UI_ICON_NONE,
            0
        )) {
        app_close_dialog(app);
    }
    if (app_override_count(app) > 0u
        && ui_draw_button(
            context,
            canvas,
            nk_rect(
                card.x + card.w - close_width - save_width - 34.0f,
                card.y + card.h - 52.0f,
                save_width,
                32.0f
            ),
            app_tr("保存规则..."),
            fonts->body,
            UI_ICON_NONE,
            1
        )) {
        app_close_dialog(app);
        app_save_rules(app);
    }
}

static void license_set_result(
    app_state *app,
    int success,
    const char *message
)
{
    app->dialog_error = !success;
    (void)snprintf(
        app->dialog_message,
        sizeof(app->dialog_message),
        "%s",
        app_tr(message == NULL ? "" : message)
    );
}

static void render_license_online_page(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    struct nk_rect panel
)
{
    char subtitle[256];
    float y = panel.y + 112.0f;
    const float copy_width = ui_required_button_width(
        fonts->body, app_tr("复制"), 52.0f, 20.0f, 0
    );
    const float trial_width = ui_required_button_width(
        fonts->body,
        app_tr("开始免费试用 14 天"),
        142.0f,
        12.0f,
        0
    );
    const float purchase_width = ui_required_button_width(
        fonts->body,
        app_tr("获取激活码"),
        108.0f,
        24.0f,
        0
    );
    if (app->license.state == XLS_LICENSE_ACTIVATED) {
        const int remaining = xls_license_remaining_days(&app->license);
        if (remaining > 0) {
            (void)snprintf(
                subtitle,
                sizeof(subtitle),
                app_tr("您的软件已激活，许可证剩余 %d 天。"),
                remaining
            );
        } else {
            (void)snprintf(
                subtitle,
                sizeof(subtitle),
                "%s",
                app_tr("您的软件已激活，可正常使用全部功能。")
            );
        }
    } else if (app->license.state == XLS_LICENSE_TRIAL) {
        (void)snprintf(
            subtitle,
            sizeof(subtitle),
            app_tr("试用期剩余 %d 天，期间所有功能开放。"),
            xls_license_remaining_days(&app->license)
        );
    } else if (app->license.state == XLS_LICENSE_EXPIRED) {
        (void)snprintf(
            subtitle,
            sizeof(subtitle),
            "%s",
            app_tr("许可证或试用期已过期，请输入激活码继续使用完整功能。")
        );
    } else {
        (void)snprintf(
            subtitle,
            sizeof(subtitle),
            "%s",
            app_tr("输入激活码，或先开始 14 天免费试用。")
        );
    }
    ui_draw_text_elided(
        canvas,
        nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 24.0f),
        subtitle,
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    y += 42.0f;
    if (app->license.state == XLS_LICENSE_ACTIVATED) {
        char valid_until[96];
        nk_fill_rect(
            canvas,
            nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 150.0f),
            9.0f,
            UI_BG2
        );
        nk_stroke_rect(
            canvas,
            nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 150.0f),
            9.0f,
            1.0f,
            UI_BORDER
        );
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 44.0f, y + 16.0f, 92.0f, 24.0f),
            app_tr("套餐类型"),
            fonts->body,
            UI_MUTED,
            NK_TEXT_LEFT
        );
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 140.0f, y + 16.0f, panel.w - 186.0f, 24.0f),
            app_tr(xls_license_plan_text(app->license.info.plan)),
            fonts->body,
            UI_LABEL_FG,
            NK_TEXT_RIGHT
        );
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 44.0f, y + 57.0f, 80.0f, 24.0f),
            app_tr("激活码"),
            fonts->body,
            UI_MUTED,
            NK_TEXT_LEFT
        );
        ui_draw_text_elided(
            canvas,
            nk_rect(
                panel.x + 120.0f,
                y + 57.0f,
                panel.w - copy_width - 174.0f,
                24.0f
            ),
            app->license.info.key_id,
            fonts->source_value,
            UI_TEXT,
            NK_TEXT_RIGHT
        );
        if (ui_draw_button(
            context,
            canvas,
            nk_rect(
                panel.x + panel.w - copy_width - 44.0f,
                y + 54.0f,
                copy_width,
                30.0f
            ),
            app_tr("复制"),
            fonts->body,
            UI_ICON_NONE,
            0
        )) {
            (void)SDL_SetClipboardText(app->license.info.key_id);
            license_set_result(app, 1, "激活码已复制");
        }
        if (app->license.info.expires_at > 0) {
            const time_t expiry = (time_t)app->license.info.expires_at;
            struct tm calendar;
#if defined(_WIN32)
            (void)localtime_s(&calendar, &expiry);
#else
            {
                const struct tm *local_calendar = localtime(&expiry);
                if (local_calendar != NULL) {
                    calendar = *local_calendar;
                } else {
                    memset(&calendar, 0, sizeof(calendar));
                }
            }
#endif
            (void)snprintf(
                valid_until,
                sizeof(valid_until),
                "%04d-%02d-%02d",
                calendar.tm_year + 1900,
                calendar.tm_mon + 1,
                calendar.tm_mday
            );
        } else {
            (void)snprintf(
                valid_until,
                sizeof(valid_until),
                "%s",
                app_tr("永久授权")
            );
        }
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 44.0f, y + 98.0f, 80.0f, 24.0f),
            app_tr("有效期"),
            fonts->body,
            UI_MUTED,
            NK_TEXT_LEFT
        );
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 120.0f, y + 98.0f, panel.w - 166.0f, 24.0f),
            valid_until,
            fonts->body,
            UI_TEXT,
            NK_TEXT_RIGHT
        );
        return;
    }
    ui_draw_text(
        canvas,
        nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 22.0f),
        app_tr("激活码"),
        fonts->body,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    y += 30.0f;
    {
        char compact[17];
        size_t compact_length = 0u;
        size_t input_index;
        const unsigned char *cursor =
            (const unsigned char *)app->license_key;
        while (*cursor != '\0' && compact_length < 16u) {
            if (isalnum(*cursor) != 0) {
                compact[compact_length++] = (char)*cursor;
            }
            ++cursor;
        }
        compact[compact_length] = '\0';
        for (input_index = 0u; input_index < 4u; ++input_index) {
            const float x = panel.x + 36.0f
                + (float)input_index * 96.0f;
            const struct nk_rect input = nk_rect(x, y, 72.0f, 38.0f);
            char part[5];
            size_t part_length = compact_length > input_index * 4u
                ? compact_length - input_index * 4u
                : 0u;
            if (part_length > 4u) {
                part_length = 4u;
            }
            memset(part, 0, sizeof(part));
            if (part_length > 0u) {
                memcpy(
                    part,
                    compact + input_index * 4u,
                    part_length
                );
            }
            nk_fill_rect(canvas, input, 8.0f, UI_BG1);
            nk_stroke_rect(
                canvas,
                input,
                8.0f,
                1.0f,
                compact_length < 16u
                    && input_index == compact_length / 4u
                    ? UI_ACCENT
                    : UI_BORDER
            );
            ui_draw_text(
                canvas,
                input,
                part,
                fonts->source_value,
                UI_TEXT,
                NK_TEXT_CENTERED
            );
            if (input_index < 3u) {
                ui_draw_text(
                    canvas,
                    nk_rect(x + 80.0f, y, 8.0f, 38.0f),
                    "-",
                    fonts->source_value,
                    UI_MUTED,
                    NK_TEXT_CENTERED
                );
            }
        }
    }
    y += 46.0f;
    {
        const struct nk_rect activate = nk_rect(
            panel.x + 36.0f, y, panel.w - 72.0f, 40.0f
        );
        const int complete = strlen(app->license_key) == 19u;
        int clicked = 0;
        if (complete) {
            clicked = ui_draw_button(
                context,
                canvas,
                activate,
                app_tr("激活"),
                fonts->body,
                UI_ICON_NONE,
                1
            );
        } else {
            nk_fill_rect(canvas, activate, 8.0f, nk_rgb(191, 211, 247));
            ui_draw_text(
                canvas,
                activate,
                app_tr("激活"),
                fonts->body,
                nk_rgb(255, 255, 255),
                NK_TEXT_CENTERED
            );
        }
        if (clicked) {
        char message[256];
        const int success = xls_license_activate(
            &app->license,
            app->license_key,
            message,
            sizeof(message)
        );
        license_set_result(app, success, message);
        }
    }
    y += 52.0f;
    if (app->license.state == XLS_LICENSE_UNACTIVATED
        && ui_clicked(
            context,
            nk_rect(
                panel.x + 36.0f,
                y,
                trial_width,
                32.0f
            )
        )) {
        char message[256];
        const int success = xls_license_request_trial(
            &app->license, message, sizeof(message)
        );
        license_set_result(app, success, message);
    }
    if (app->license.state == XLS_LICENSE_UNACTIVATED) {
        ui_draw_text(
            canvas,
            nk_rect(
                panel.x + 36.0f,
                y,
                trial_width,
                32.0f
            ),
            app_tr("开始免费试用 14 天"),
            fonts->body,
            UI_ACCENT,
            NK_TEXT_LEFT
        );
    }
    if (ui_draw_button(
        context,
        canvas,
        nk_rect(
            panel.x + panel.w - purchase_width - 36.0f,
            y,
            purchase_width,
            34.0f
        ),
        app_tr("获取激活码"),
        fonts->body,
        UI_ICON_NONE,
        0
    )) {
        (void)xls_platform_open_url("https://z-pulse.cn/xlsone/buy.html");
    }
}

static void render_license_offline_page(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    struct nk_rect panel
)
{
    float y = panel.y + 112.0f;
    const float open_width = ui_required_button_width(
        fonts->body,
        app_tr("打开离线激活页面"),
        168.0f,
        28.0f,
        0
    );
    const float import_width = ui_required_button_width(
        fonts->body,
        app_tr("导入授权文件..."),
        168.0f,
        28.0f,
        0
    );
    const float copy_width = ui_required_button_width(
        fonts->body, app_tr("复制"), 71.0f, 20.0f, 0
    );
    const char *steps[] = {
        "1   打开离线激活页面，粘贴设备码并提交",
        "2   下载生成的授权文件（.license）",
        "3   回到这里导入授权文件完成激活"
    };
    size_t index;
    nk_fill_rect(
        canvas,
        nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 250.0f),
        9.0f,
        UI_BG2
    );
    nk_stroke_rect(
        canvas,
        nk_rect(panel.x + 30.0f, y, panel.w - 60.0f, 250.0f),
        9.0f,
        1.0f,
        UI_BORDER
    );
    for (index = 0u; index < sizeof(steps) / sizeof(steps[0]); ++index) {
        ui_draw_text(
            canvas,
            nk_rect(panel.x + 45.0f, y + 14.0f + (float)index * 34.0f, panel.w - 90.0f, 26.0f),
            app_tr(steps[index]),
            fonts->body,
            UI_TEXT,
            NK_TEXT_LEFT
        );
    }
    ui_draw_text(
        canvas,
        nk_rect(panel.x + 45.0f, y + 121.0f, 100.0f, 22.0f),
        app_tr("本机设备码"),
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    ui_draw_text_elided(
        canvas,
        nk_rect(
            panel.x + 45.0f,
            y + 146.0f,
            panel.w - copy_width - 154.0f,
            28.0f
        ),
        app->license.device_fingerprint,
        fonts->source_value,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    if (ui_draw_button(
        context,
        canvas,
        nk_rect(
            panel.x + panel.w - copy_width - 45.0f,
            y + 143.0f,
            copy_width,
            30.0f
        ),
        app_tr("复制"),
        fonts->body,
        UI_ICON_NONE,
        0
    )) {
        (void)SDL_SetClipboardText(app->license.device_fingerprint);
        license_set_result(app, 1, "设备码已复制");
    }
    if (ui_draw_button(
        context,
        canvas,
        nk_rect(
            panel.x + 45.0f,
            y + 190.0f,
            open_width,
            36.0f
        ),
        app_tr("打开离线激活页面"),
        fonts->body,
        UI_ICON_NONE,
        1
    )) {
        (void)xls_platform_open_url("https://z-pulse.cn/xlsone/offline");
    }
    if (ui_draw_button(
        context,
        canvas,
        nk_rect(
            panel.x + panel.w - import_width - 45.0f,
            y + 190.0f,
            import_width,
            36.0f
        ),
        app_tr("导入授权文件..."),
        fonts->body,
        UI_ICON_NONE,
        0
    )) {
        char *path = NULL;
        if (xls_platform_open_license_file(&path)) {
            char message[256];
            const int success = xls_license_import_file(
                &app->license, path, message, sizeof(message)
            );
            license_set_result(app, success, message);
            free(path);
        }
    }
}

static void render_license_dialog(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height
)
{
    float brand_width = app_license_brand_width(fonts);
    const float desired_panel_width =
        app_license_panel_width(fonts);
    float card_width = brand_width + desired_panel_width;
    const float card_height = height < 634.0f ? height - 54.0f : 580.0f;
    if (card_width > width - 60.0f) {
        card_width = width - 60.0f;
    }
    if (card_width - brand_width < 440.0f) {
        brand_width = card_width - 440.0f;
    }
    if (brand_width < 240.0f) {
        brand_width = 240.0f;
    }
    const struct nk_rect card = nk_rect(
        (width - card_width) * 0.5f,
        (height - card_height) * 0.5f,
        card_width,
        card_height
    );
    const struct nk_rect panel = nk_rect(
        card.x + brand_width,
        card.y,
        card.w - brand_width,
        card.h
    );
    render_dialog_backdrop(canvas, width, height);
    nk_fill_rect(canvas, card, 13.0f, UI_BG1);
    nk_stroke_rect(canvas, card, 13.0f, 1.0f, UI_BORDER);
    nk_fill_rect(
        canvas,
        nk_rect(card.x, card.y, brand_width, card.h),
        13.0f,
        UI_ACCENT_SOFT
    );
    nk_fill_rect(
        canvas,
        nk_rect(card.x + brand_width - 13.0f, card.y, 13.0f, card.h),
        0.0f,
        UI_ACCENT_SOFT
    );
    nk_fill_rect(
        canvas,
        nk_rect(card.x + brand_width * 0.5f - 28.0f, card.y + 112.0f, 56.0f, 56.0f),
        14.0f,
        UI_ACCENT
    );
    nk_fill_rect(
        canvas,
        nk_rect(
            card.x + brand_width * 0.5f - 14.0f,
            card.y + 122.0f,
            28.0f,
            36.0f
        ),
        4.0f,
        nk_rgb(255, 255, 255)
    );
    nk_stroke_line(
        canvas,
        card.x + brand_width * 0.5f - 8.0f,
        card.y + 133.0f,
        card.x + brand_width * 0.5f + 8.0f,
        card.y + 133.0f,
        2.0f,
        UI_SUM_BORDER
    );
    nk_stroke_line(
        canvas,
        card.x + brand_width * 0.5f - 8.0f,
        card.y + 141.0f,
        card.x + brand_width * 0.5f + 8.0f,
        card.y + 141.0f,
        2.0f,
        UI_SUM_BORDER
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 187.0f, brand_width - 48.0f, 30.0f),
        app_tr("表表归一"),
        fonts->title,
        UI_TEXT,
        NK_TEXT_CENTERED
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 24.0f, card.y + 223.0f, brand_width - 48.0f, 48.0f),
        app_tr("多张同格式 Excel 报表一键汇总"),
        fonts->body,
        UI_MUTED,
        NK_TEXT_CENTERED
    );
    {
        static const char *const pills[] = {"快速", "安全", "原生"};
        float pill_widths[3];
        float pill_x;
        float total_width = 0.0f;
        size_t pill_index;
        for (pill_index = 0u; pill_index < 3u; ++pill_index) {
            pill_widths[pill_index] = ui_required_button_width(
                fonts->body,
                app_tr(pills[pill_index]),
                56.0f,
                20.0f,
                0
            );
            total_width += pill_widths[pill_index];
            if (pill_index > 0u) {
                total_width += 12.0f;
            }
        }
        pill_x = card.x + (brand_width - total_width) * 0.5f;
        for (pill_index = 0u; pill_index < 3u; ++pill_index) {
            const struct nk_rect pill = nk_rect(
                pill_x,
                card.y + card.h - 55.0f,
                pill_widths[pill_index],
                24.0f
            );
            nk_fill_rect(canvas, pill, 12.0f, UI_BG1);
            ui_draw_text(
                canvas,
                pill,
                app_tr(pills[pill_index]),
                fonts->body,
                UI_MUTED,
                NK_TEXT_CENTERED
            );
            pill_x += pill_widths[pill_index] + 12.0f;
        }
    }
    ui_draw_text(
        canvas,
        nk_rect(panel.x + 36.0f, panel.y + 24.0f, panel.w - 82.0f, 28.0f),
        app_tr("许可证"),
        fonts->title,
        UI_TEXT,
        NK_TEXT_LEFT
    );
    if (app->license.state != XLS_LICENSE_ACTIVATED) {
        const char *online_text = app_tr("在线激活");
        const char *offline_text = app_tr("离线激活");
        float online_width =
            ui_text_width(fonts->body, online_text) + 18.0f;
        float offline_width =
            ui_text_width(fonts->body, offline_text) + 18.0f;
        if (online_width < 82.0f) {
            online_width = 82.0f;
        }
        if (offline_width < 82.0f) {
            offline_width = 82.0f;
        }
        const struct nk_rect online = nk_rect(
            panel.x + 30.0f, panel.y + 63.0f, online_width, 30.0f
        );
        const struct nk_rect offline = nk_rect(
            online.x + online.w + 2.0f,
            panel.y + 63.0f,
            offline_width,
            30.0f
        );
        ui_draw_text(
            canvas,
            online,
            online_text,
            fonts->body,
            app->license_page == 0 ? UI_TEXT : UI_MUTED,
            NK_TEXT_CENTERED
        );
        ui_draw_text(
            canvas,
            offline,
            offline_text,
            fonts->body,
            app->license_page == 1 ? UI_TEXT : UI_MUTED,
            NK_TEXT_CENTERED
        );
        nk_fill_rect(
            canvas,
            nk_rect(
                app->license_page == 0 ? online.x : offline.x,
                panel.y + 91.0f,
                app->license_page == 0 ? online.w : offline.w,
                2.0f
            ),
            0.0f,
            UI_ACCENT
        );
        if (ui_clicked(context, online)) {
            app->license_page = 0;
        }
        if (ui_clicked(context, offline)) {
            app->license_page = 1;
        }
    }
    if (app->dialog_message[0] != '\0') {
        ui_draw_text_elided(
            canvas,
            nk_rect(panel.x + 30.0f, panel.y + panel.h - 48.0f, panel.w - 60.0f, 24.0f),
            app->dialog_message,
            fonts->body,
            app->dialog_error ? nk_rgb(204, 53, 53) : UI_LABEL_FG,
            NK_TEXT_LEFT
        );
    }
    if (app->license_page == 0
        || app->license.state == XLS_LICENSE_ACTIVATED) {
        render_license_online_page(
            context, canvas, app, fonts, panel
        );
    } else {
        render_license_offline_page(
            context, canvas, app, fonts, panel
        );
    }
    if (render_dialog_close(context, canvas, card)) {
        app_close_dialog(app);
    }
}

static void render_dialog(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width,
    float height
)
{
    switch (app->dialog) {
    case APP_DIALOG_LICENSE:
        render_license_dialog(
            context, canvas, app, fonts, width, height
        );
        break;
    case APP_DIALOG_RULES:
        render_rules_dialog(
            context, canvas, app, fonts, width, height
        );
        break;
    case APP_DIALOG_NOTICE:
        render_notice_dialog(
            context, canvas, app, fonts, width, height
        );
        break;
    case APP_DIALOG_UPDATE:
        render_update_dialog(
            context, canvas, app, fonts, width, height
        );
        break;
    case APP_DIALOG_NONE:
    default:
        break;
    }
}

static void render_drop_feedback_toast(
    struct nk_command_buffer *canvas,
    const app_state *app,
    const ui_fonts *fonts,
    float width,
    float status_y
)
{
    char message[160];
    const char *text;
    struct nk_color border = UI_ACCENT;
    struct nk_color foreground = UI_SUM_FG;
    struct nk_color background = UI_ACCENT_SOFT;
    float toast_width;
    struct nk_rect toast;
    if (app->drop_feedback == APP_DROP_SUCCESS) {
        (void)snprintf(
            message,
            sizeof(message),
            app_tr("已接收并导入 %zu 个工作簿"),
            app->last_drop_count
        );
        text = message;
        border = UI_LABEL_BORDER;
        foreground = UI_LABEL_FG;
        background = UI_LABEL_BG;
    } else if (app->drop_feedback == APP_DROP_REJECTED) {
        text = app_tr("未能导入投放的文件");
        border = nk_rgb(238, 178, 178);
        foreground = nk_rgb(174, 45, 45);
        background = nk_rgb(255, 242, 242);
    } else {
        return;
    }
    toast_width = ui_text_width(fonts->body, text) + 38.0f;
    if (toast_width < 180.0f) {
        toast_width = 180.0f;
    }
    if (toast_width > width - 40.0f) {
        toast_width = width - 40.0f;
    }
    toast = nk_rect(
        (width - toast_width) * 0.5f,
        status_y - 48.0f,
        toast_width,
        34.0f
    );
    nk_fill_rect(canvas, toast, 17.0f, background);
    nk_stroke_rect(canvas, toast, 17.0f, 1.0f, border);
    ui_draw_text_elided(
        canvas,
        nk_rect(toast.x + 14.0f, toast.y, toast.w - 28.0f, toast.h),
        text,
        fonts->body,
        foreground,
        NK_TEXT_CENTERED
    );
}

static void render_workspace(
    struct nk_context *context,
    app_state *app,
    const ui_fonts *fonts,
    int width,
    int height
)
{
    const float status_y = (float)height - 17.0f;
    const int has_workspace = app->workbook_count > 0;
    const int has_overrides = app_has_overrides(app);
    const float correction_height = has_overrides ? 38.0f : 0.0f;
    const float content_bottom = status_y - correction_height;
    struct nk_command_buffer *canvas;
    if (nk_begin(
        context,
        "xlsOne",
        nk_rect(0.0f, 0.0f, (float)width, (float)height),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND
    )) {
        canvas = nk_window_get_canvas(context);
        nk_fill_rect(
            canvas,
            nk_rect(0.0f, 0.0f, (float)width, (float)height),
            0.0f,
            UI_BG0
        );
        ui_input_blocked = app->active_menu != APP_MENU_NONE
            || app->dialog != APP_DIALOG_NONE;
        render_toolbar(context, canvas, app, fonts, (float)width, has_workspace);
        if (!has_workspace) {
            render_empty_state(
                context,
                canvas,
                app,
                fonts,
                (float)width,
                (float)height,
                status_y
            );
        } else if (app->merged_sheet_count > 0) {
            const float inspector_width = width >= 1120 ? 288.0f : 280.0f;
            const float table_width = (float)width - inspector_width;
            render_sheet_strip(context, canvas, app, fonts, (float)width);
            render_grid(
                context,
                canvas,
                app,
                fonts,
                nk_rect(
                    0.0f,
                    UI_SHEET_BOTTOM,
                    table_width,
                    content_bottom - UI_SHEET_BOTTOM
                )
            );
            render_inspector(
                context,
                canvas,
                app,
                fonts,
                nk_rect(
                    table_width,
                    UI_SHEET_BOTTOM,
                    inspector_width,
                    content_bottom - UI_SHEET_BOTTOM
                )
            );
        } else {
            size_t index;
            nk_fill_rect(
                canvas,
                nk_rect(
                    0.0f,
                    UI_TOOLBAR_BOTTOM,
                    (float)width,
                    content_bottom - UI_TOOLBAR_BOTTOM
                ),
                0.0f,
                UI_BG0
            );
            ui_draw_text(
                canvas,
                nk_rect(
                    36.0f,
                    UI_TOOLBAR_BOTTOM + 60.0f,
                    (float)width - 72.0f,
                    32.0f
                ),
                app_tr("模板结构不一致，当前没有可汇总工作表。"),
                fonts->title,
                UI_TEXT,
                NK_TEXT_CENTERED
            );
            for (index = 0; index < app->validation.issue_count && index < 8; ++index) {
                ui_draw_text_elided(
                    canvas,
                    nk_rect(
                        80.0f,
                        UI_TOOLBAR_BOTTOM + 105.0f
                            + (float)index * 25.0f,
                        (float)width - 160.0f,
                        22.0f
                    ),
                    app_tr(app->validation.issues[index].message),
                    fonts->body,
                    UI_MUTED,
                    NK_TEXT_CENTERED
                );
            }
        }
        if (has_overrides) {
            render_correction_bar(
                context,
                canvas,
                app,
                fonts,
                (float)width,
                content_bottom
            );
        }
        render_status_bar(canvas, app, fonts, (float)width, status_y);
        if (has_workspace) {
            render_drop_feedback_toast(
                canvas,
                app,
                fonts,
                (float)width,
                status_y
            );
        }
        ui_input_blocked = app->dialog != APP_DIALOG_NONE;
        render_menu_bar(
            context, canvas, app, fonts, (float)width
        );
        ui_input_blocked = app->dialog != APP_DIALOG_NONE;
        render_active_menu(context, canvas, app, fonts);
        ui_input_blocked = 0;
        render_dialog(
            context,
            canvas,
            app,
            fonts,
            (float)width,
            (float)height
        );
        ui_input_blocked = 0;
    }
    nk_end(context);
}

static const char *find_font_path(xls_ui_language language)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    (void)language;
#endif
#if defined(_WIN32) || defined(__APPLE__)
    static const char *const japanese_candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/YuGothR.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
#else
        "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
        "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/PingFang.ttc",
#endif
        NULL
    };
#endif
    static const char *const candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/YuGothR.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
#elif defined(__APPLE__)
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
#endif
        NULL
    };
    const char *const *selected_candidates = candidates;
    size_t index;
#if defined(_WIN32) || defined(__APPLE__)
    if (language == XLS_UI_LANGUAGE_JAPANESE) {
        selected_candidates = japanese_candidates;
    }
#endif
    for (index = 0; selected_candidates[index] != NULL; ++index) {
        FILE *file = fopen(selected_candidates[index], "rb");
        if (file != NULL) {
            fclose(file);
            return selected_candidates[index];
        }
    }
    return NULL;
}

static const nk_rune *ui_glyph_ranges(void)
{
    static const nk_rune ranges[] = {
        0x0020, 0x00ff,
        0x3000, 0x30ff,
        0x31f0, 0x31ff,
        0x4e00, 0x9faf,
        0xff00, 0xffef,
        0
    };
    return ranges;
}

static const nk_rune *title_glyph_ranges(void)
{
    static const nk_rune ranges[] = {
        0x0020, 0x007e,
        0x3000, 0x30ff,
        0x4e00, 0x4e00,
        0x4e0d, 0x4e0d,
        0x4ef6, 0x4ef6,
        0x4f5c, 0x4f5c,
        0x4fe1, 0x4fe1,
        0x5165, 0x5165,
        0x524d, 0x524d,
        0x533a, 0x533a,
        0x5373, 0x5373,
        0x53d7, 0x53d7,
        0x53ef, 0x53ef,
        0x5728, 0x5728,
        0x5bfc, 0x5bfc,
        0x5de5, 0x5de5,
        0x5df2, 0x5df2,
        0x5e30, 0x5e30,
        0x5f0f, 0x5f0f,
        0x5f52, 0x5f52,
        0x5f53, 0x5f53,
        0x603b, 0x603b,
        0x624b, 0x624b,
        0x62d6, 0x62d6,
        0x6301, 0x6301,
        0x63a5, 0x63a5,
        0x652f, 0x652f,
        0x6536, 0x6536,
        0x6587, 0x6587,
        0x6709, 0x6709,
        0x677e, 0x677f,
        0x6784, 0x6784,
        0x683c, 0x683c,
        0x69cb, 0x69cb,
        0x6a21, 0x6a21,
        0x6b63, 0x6b63,
        0x6ca1, 0x6ca1,
        0x6c47, 0x6c47,
        0x7ed3, 0x7ed3,
        0x80fd, 0x80fd,
        0x81f4, 0x81f4,
        0x8868, 0x8868,
        0x8a08, 0x8a08,
        0x8bb8, 0x8bc1,
        0x9020, 0x9020,
        0x96c6, 0x96c6,
        0xff0c, 0xff0c,
        0
    };
    return ranges;
}

static const char *find_bold_font_path(void)
{
    static const char *const candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/arialbd.ttf",
        "C:/Windows/Fonts/msyhbd.ttc",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/STHeiti Medium.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
#endif
        NULL
    };
    size_t index;
    for (index = 0; candidates[index] != NULL; ++index) {
        FILE *file = fopen(candidates[index], "rb");
        if (file != NULL) {
            fclose(file);
            return candidates[index];
        }
    }
    return NULL;
}

static const char *find_monospace_font_path(void)
{
    static const char *const candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/consolab.ttf",
        "C:/Windows/Fonts/consola.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.dfont",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Bold.ttf",
#endif
        NULL
    };
    size_t index;
    for (index = 0; candidates[index] != NULL; ++index) {
        FILE *file = fopen(candidates[index], "rb");
        if (file != NULL) {
            fclose(file);
            return candidates[index];
        }
    }
    return NULL;
}

static void app_select_language_fonts(
    struct nk_context *context,
    ui_fonts *active,
    const ui_fonts *default_fonts,
    const ui_fonts *japanese_fonts
)
{
    *active = xls_i18n_language() == XLS_UI_LANGUAGE_JAPANESE
        ? *japanese_fonts
        : *default_fonts;
    if (active->body != NULL) {
        nk_style_set_font(context, active->body);
    }
}

static void app_select_reference(app_state *app, const char *reference)
{
    size_t column = 0;
    size_t row;
    const char *cursor = reference;
    char *end = NULL;
    if (reference == NULL || reference[0] == '\0') {
        return;
    }
    while ((*cursor >= 'A' && *cursor <= 'Z')
        || (*cursor >= 'a' && *cursor <= 'z')) {
        const unsigned char uppercase = (unsigned char)(
            *cursor >= 'a' && *cursor <= 'z'
                ? *cursor - ('a' - 'A')
                : *cursor
        );
        column = column * 26u
            + (size_t)(uppercase - (unsigned char)'A')
            + (size_t)1u;
        ++cursor;
    }
    if (column == 0 || *cursor < '1' || *cursor > '9') {
        return;
    }
    row = (size_t)strtoul(cursor, &end, 10);
    if (end == cursor || *end != '\0' || row == 0) {
        return;
    }
    --column;
    --row;
    if (app->selected_sheet < app->merged_sheet_count
        && row < app->merged_sheets[app->selected_sheet].row_count
        && column < app->merged_sheets[app->selected_sheet].column_count) {
        app->selected_row = row;
        app->selected_column = column;
        app->has_selection = 1;
    }
}

static void app_set_license_key_input(
    app_state *app,
    const char *current,
    const char *additional
)
{
    char raw[17];
    size_t raw_length = 0u;
    const char *parts[2];
    size_t part_index;
    size_t output_index = 0u;
    parts[0] = current == NULL ? "" : current;
    parts[1] = additional == NULL ? "" : additional;
    for (part_index = 0u; part_index < 2u; ++part_index) {
        const unsigned char *cursor =
            (const unsigned char *)parts[part_index];
        while (*cursor != '\0' && raw_length < sizeof(raw) - 1u) {
            if (isalnum(*cursor) != 0) {
                raw[raw_length++] = (char)toupper(*cursor);
            }
            ++cursor;
        }
    }
    raw[raw_length] = '\0';
    for (part_index = 0u; part_index < raw_length; ++part_index) {
        if (part_index > 0u && part_index % 4u == 0u) {
            app->license_key[output_index++] = '-';
        }
        app->license_key[output_index++] = raw[part_index];
    }
    app->license_key[output_index] = '\0';
}

static void app_backspace_license_key(app_state *app)
{
    char raw[17];
    size_t raw_length = 0u;
    const unsigned char *cursor =
        (const unsigned char *)app->license_key;
    while (*cursor != '\0' && raw_length < sizeof(raw) - 1u) {
        if (isalnum(*cursor) != 0) {
            raw[raw_length++] = (char)*cursor;
        }
        ++cursor;
    }
    if (raw_length > 0u) {
        --raw_length;
    }
    raw[raw_length] = '\0';
    app->license_key[0] = '\0';
    app_set_license_key_input(app, "", raw);
}

static app_menu app_menu_from_name(const char *name)
{
    if (name == NULL) {
        return APP_MENU_NONE;
    }
    if (strcmp(name, "file") == 0) {
        return APP_MENU_FILE;
    }
    if (strcmp(name, "edit") == 0) {
        return APP_MENU_EDIT;
    }
    if (strcmp(name, "rules") == 0) {
        return APP_MENU_RULES;
    }
    if (strcmp(name, "license") == 0) {
        return APP_MENU_LICENSE;
    }
    if (strcmp(name, "language") == 0) {
        return APP_MENU_LANGUAGE;
    }
    if (strcmp(name, "help") == 0) {
        return APP_MENU_HELP;
    }
    return APP_MENU_NONE;
}

static int save_renderer_bmp(
    SDL_Renderer *renderer,
    const char *path
)
{
    SDL_Surface *surface;
    int width;
    int height;
    int result;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        fprintf(stderr, "could not read renderer size: %s\n", SDL_GetError());
        return 0;
    }
    surface = SDL_CreateRGBSurfaceWithFormat(
        0,
        width,
        height,
        32,
        SDL_PIXELFORMAT_ARGB8888
    );
    if (surface == NULL) {
        fprintf(stderr, "could not allocate screenshot: %s\n", SDL_GetError());
        return 0;
    }
    result = SDL_RenderReadPixels(
        renderer,
        NULL,
        SDL_PIXELFORMAT_ARGB8888,
        surface->pixels,
        surface->pitch
    );
    if (result == 0) {
        result = SDL_SaveBMP(surface, path);
    }
    if (result != 0) {
        fprintf(stderr, "could not save screenshot: %s\n", SDL_GetError());
    }
    SDL_FreeSurface(surface);
    return result == 0;
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    struct nk_context *context;
    struct nk_font_atlas *atlas;
    struct nk_font *body_font = NULL;
    struct nk_font *title_font = NULL;
    struct nk_font *japanese_body_font = NULL;
    struct nk_font *japanese_title_font = NULL;
    struct nk_font *numeric_font = NULL;
    struct nk_font *source_value_font = NULL;
    ui_fonts fonts;
    ui_fonts default_fonts;
    ui_fonts japanese_fonts;
    const char *font_path;
    const char *japanese_font_path;
    const char *bold_font_path;
    const char *monospace_font_path;
    const char *screenshot_path;
    const char *screenshot_selection;
    const char *screenshot_menu;
    const char *screenshot_dialog;
    const char *screenshot_drop_path;
    const char *screenshot_drop_argument;
    const char *screenshot_drop_target;
    const char *screenshot_language_sequence;
    const char *screenshot_sheet_number;
    const char *screenshot_source_index;
    const char *screenshot_source_action;
    const char *screenshot_expected_clipboard = NULL;
    int screenshot_perform_source_click = 0;
    const char *screenshot_require_workbooks;
    xls_platform_drop_target native_drop_target;
    app_state app;
    int screenshot_saved = 0;
    int screenshot_failed = 0;
    int index;
    memset(&app, 0, sizeof(app));
    memset(&fonts, 0, sizeof(fonts));
    memset(&default_fonts, 0, sizeof(default_fonts));
    memset(&japanese_fonts, 0, sizeof(japanese_fonts));
    memset(&native_drop_target, 0, sizeof(native_drop_target));
    app.running = 1;
    app.sources_expanded = 1;
    xls_license_manager_init(&app.license);

#if defined(SDL_HINT_WINDOWS_DPI_AWARENESS)
    (void)SDL_SetHint(
        SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2"
    );
#endif
#if defined(SDL_HINT_WINDOWS_DPI_SCALING)
    (void)SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    app.update_event_type = SDL_RegisterEvents(1);
    app_load_language(&app);
    app_set_status(&app, "可拖入或打开多个 Excel 工作簿。");
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    window = SDL_CreateWindow(
        app_tr("表表归一"),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        696,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
    );
    if (window == NULL) {
        fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    app.window = window;
    if (!xls_platform_ensure_application_shortcuts()) {
        fprintf(
            stderr,
            "could not register the installed application "
            "or create its desktop shortcut\n"
        );
    }
    SDL_SetWindowMinimumSize(window, 980, 600);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
#if SDL_VERSION_ATLEAST(2, 0, 5)
    SDL_EventState(SDL_DROPTEXT, SDL_ENABLE);
    SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
    SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);
#endif
    if (!xls_platform_drop_target_init(
        &native_drop_target, window
    )) {
        fprintf(
            stderr,
            "native drag hover is unavailable; "
            "falling back to SDL drop events\n"
        );
    }
    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (renderer == NULL) {
        renderer = SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_SOFTWARE
        );
    }
    if (renderer == NULL) {
        fprintf(stderr, "renderer creation failed: %s\n", SDL_GetError());
        xls_platform_drop_target_shutdown(&native_drop_target);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    {
        int logical_width;
        int logical_height;
        SDL_GetWindowSize(window, &logical_width, &logical_height);
        if (SDL_RenderSetLogicalSize(
            renderer, logical_width, logical_height
        ) != 0) {
            fprintf(
                stderr,
                "could not configure UI scaling: %s\n",
                SDL_GetError()
            );
        }
    }
    context = nk_sdl_init(window, renderer);
    nk_sdl_font_stash_begin(&atlas);
    font_path = find_font_path(XLS_UI_LANGUAGE_ZH_HANS);
    japanese_font_path = find_font_path(
        XLS_UI_LANGUAGE_JAPANESE
    );
    bold_font_path = find_bold_font_path();
    monospace_font_path = find_monospace_font_path();
    if (font_path != NULL) {
        const float body_size = app_body_font_size();
        const float title_size = app_title_font_size();
        struct nk_font_config body_config = nk_font_config(body_size);
        struct nk_font_config title_config = nk_font_config(title_size);
        body_config.range = ui_glyph_ranges();
        body_config.oversample_h = 1;
        body_config.oversample_v = 1;
        body_config.pixel_snap = 1;
        title_config.range = title_glyph_ranges();
        title_config.oversample_h = 1;
        title_config.oversample_v = 1;
        title_config.pixel_snap = 1;
        body_font = nk_font_atlas_add_from_file(
            atlas, font_path, body_size, &body_config
        );
        title_font = nk_font_atlas_add_from_file(
            atlas, font_path, title_size, &title_config
        );
    }
    if (japanese_font_path != NULL
        && (font_path == NULL
            || strcmp(japanese_font_path, font_path) != 0)) {
        const float body_size = app_body_font_size();
        const float title_size = app_title_font_size();
        struct nk_font_config body_config =
            nk_font_config(body_size);
        struct nk_font_config title_config =
            nk_font_config(title_size);
        body_config.range = ui_glyph_ranges();
        body_config.oversample_h = 1;
        body_config.oversample_v = 1;
        body_config.pixel_snap = 1;
        title_config.range = title_glyph_ranges();
        title_config.oversample_h = 1;
        title_config.oversample_v = 1;
        title_config.pixel_snap = 1;
        japanese_body_font = nk_font_atlas_add_from_file(
            atlas,
            japanese_font_path,
            body_size,
            &body_config
        );
        japanese_title_font = nk_font_atlas_add_from_file(
            atlas,
            japanese_font_path,
            title_size,
            &title_config
        );
    } else {
        japanese_body_font = body_font;
        japanese_title_font = title_font;
    }
    if (monospace_font_path != NULL) {
        const float numeric_size = app_numeric_font_size();
        struct nk_font_config numeric_config =
            nk_font_config(numeric_size);
        numeric_config.range = nk_font_default_glyph_ranges();
        numeric_config.oversample_h = 2;
        numeric_config.oversample_v = 2;
        numeric_config.pixel_snap = 1;
        numeric_font = nk_font_atlas_add_from_file(
            atlas, monospace_font_path, numeric_size, &numeric_config
        );
    }
    if (bold_font_path != NULL) {
        const float source_size = app_source_font_size();
        struct nk_font_config source_value_config =
            nk_font_config(source_size);
        source_value_config.range = nk_font_default_glyph_ranges();
        source_value_config.oversample_h = 2;
        source_value_config.oversample_v = 2;
        source_value_config.pixel_snap = 1;
        source_value_font = nk_font_atlas_add_from_file(
            atlas, bold_font_path, source_size, &source_value_config
        );
    }
    nk_sdl_font_stash_end();
    if (sdl.ogl.font_tex == NULL) {
        fprintf(
            stderr,
            "font atlas upload failed (%d x %d): %s\n",
            atlas->tex_width,
            atlas->tex_height,
            SDL_GetError()
        );
    }
    default_fonts.body = body_font == NULL
        ? context->style.font
        : &body_font->handle;
    default_fonts.title = title_font == NULL
        ? default_fonts.body
        : &title_font->handle;
    default_fonts.numeric = numeric_font == NULL
        ? default_fonts.title
        : &numeric_font->handle;
    default_fonts.source_value = source_value_font == NULL
        ? default_fonts.body
        : &source_value_font->handle;
    japanese_fonts.body = japanese_body_font == NULL
        ? default_fonts.body
        : &japanese_body_font->handle;
    japanese_fonts.title = japanese_title_font == NULL
        ? japanese_fonts.body
        : &japanese_title_font->handle;
    japanese_fonts.numeric = default_fonts.numeric;
    japanese_fonts.source_value = default_fonts.source_value;
    app_select_language_fonts(
        context,
        &fonts,
        &default_fonts,
        &japanese_fonts
    );
    apply_theme(context);
    app.language_layout_dirty = 1;
    app_apply_language_layout(&app, &fonts);
    screenshot_language_sequence = getenv(
        "XLSONE_SCREENSHOT_LANGUAGE_SEQUENCE"
    );
    if (screenshot_language_sequence != NULL
        && screenshot_language_sequence[0] != '\0') {
        const size_t sequence_length =
            strlen(screenshot_language_sequence);
        char *sequence = (char *)malloc(sequence_length + 1u);
        if (sequence != NULL) {
            char *token;
            memcpy(
                sequence,
                screenshot_language_sequence,
                sequence_length + 1u
            );
            token = strtok(sequence, ",");
            while (token != NULL) {
                xls_ui_language language;
                if (xls_i18n_parse_language(token, &language)) {
                    app.language_index = (int)language;
                    xls_i18n_set_language(
                        app_effective_language(language)
                    );
                    SDL_SetWindowTitle(
                        app.window, app_tr("表表归一")
                    );
                    app.language_layout_dirty = 1;
                    app_select_language_fonts(
                        context,
                        &fonts,
                        &default_fonts,
                        &japanese_fonts
                    );
                    app_apply_language_layout(&app, &fonts);
                }
                token = strtok(NULL, ",");
            }
            app_set_status(
                &app, "可拖入或打开多个 Excel 工作簿。"
            );
            free(sequence);
        }
    }
    screenshot_path = getenv("XLSONE_SCREENSHOT_PATH");
    screenshot_selection = getenv("XLSONE_SCREENSHOT_SELECT");
    screenshot_menu = getenv("XLSONE_SCREENSHOT_MENU");
    screenshot_dialog = getenv("XLSONE_SCREENSHOT_DIALOG");
    screenshot_drop_path = getenv("XLSONE_SCREENSHOT_DROP_PATH");
    screenshot_drop_argument = getenv(
        "XLSONE_SCREENSHOT_DROP_ARGUMENT"
    );
    screenshot_drop_target = getenv("XLSONE_SCREENSHOT_DROP_TARGET");
    screenshot_sheet_number = getenv(
        "XLSONE_SCREENSHOT_SHEET_NUMBER"
    );
    screenshot_source_index = getenv(
        "XLSONE_SCREENSHOT_SOURCE_INDEX"
    );
    screenshot_source_action = getenv(
        "XLSONE_SCREENSHOT_SOURCE_ACTION"
    );
    screenshot_require_workbooks = getenv(
        "XLSONE_SCREENSHOT_REQUIRE_WORKBOOKS"
    );
    {
        const char *disable_auto_update = getenv(
            "XLSONE_DISABLE_AUTO_UPDATE"
        );
        if (app.update_event_type != (Uint32)-1
            && (screenshot_path == NULL
                || screenshot_path[0] == '\0')
            && (disable_auto_update == NULL
                || strcmp(disable_auto_update, "1") != 0)) {
            app.automatic_update_pending = 1;
            app.automatic_update_at = SDL_GetTicks64() + 800u;
        }
    }

    if (screenshot_drop_argument != NULL
        && screenshot_drop_argument[0] != '\0'
        && argc > 1) {
        screenshot_drop_path = argv[1];
    } else {
        for (index = 1; index < argc; ++index) {
            (void)app_add_path(&app, argv[index]);
        }
    }
    if (argc > 1
        && (screenshot_drop_argument == NULL
            || screenshot_drop_argument[0] == '\0')) {
        (void)app_recompute(&app);
        app_select_reference(&app, screenshot_selection);
    }
    if (screenshot_sheet_number != NULL
        && screenshot_sheet_number[0] != '\0') {
        char *end = NULL;
        const unsigned long number = strtoul(
            screenshot_sheet_number, &end, 10
        );
        if (end != screenshot_sheet_number
            && *end == '\0'
            && number > 0u
            && number <= app.merged_sheet_count) {
            app.selected_sheet = (size_t)(number - 1u);
            app.first_visible_sheet = app.selected_sheet;
            app.has_selection = 0;
        }
    }
    if (screenshot_source_index != NULL
        && screenshot_source_index[0] != '\0'
        && app.has_selection
        && app.selected_sheet < app.merged_sheet_count) {
        char *end = NULL;
        const unsigned long number = strtoul(
            screenshot_source_index, &end, 10
        );
        xls_merged_cell *selected_cell =
            xls_merged_sheet_cell_mutable(
                &app.merged_sheets[app.selected_sheet],
                app.selected_row,
                app.selected_column
            );
        if (end != screenshot_source_index
            && *end == '\0'
            && number > 0u
            && selected_cell != NULL
            && number <= selected_cell->source_count) {
            int screenshot_width;
            int screenshot_height;
            float inspector_width;
            const float detail_height =
                selected_cell->is_overridden != 0u
                    ? 157.0f
                    : 137.0f;
            const float source_viewport_y =
                UI_SHEET_BOTTOM + 12.0f
                    + detail_height + 12.0f + 57.0f;
            int target_x;
            int target_y;
            app.source_scroll_offset =
                (double)(number - 1u) * UI_SOURCE_ROW_HEIGHT;
            app.source_scroll_selection_valid = 1;
            app.source_scroll_sheet = app.selected_sheet;
            app.source_scroll_row = app.selected_row;
            app.source_scroll_column = app.selected_column;
            SDL_GetWindowSize(
                window, &screenshot_width, &screenshot_height
            );
            (void)screenshot_height;
            inspector_width = screenshot_width >= 1120
                ? 288.0f
                : 280.0f;
            target_x = (int)((float)screenshot_width
                - inspector_width + 28.0f);
            target_y = (int)(source_viewport_y + 12.0f);
            if (screenshot_source_action != NULL
                && strcmp(
                    screenshot_source_action, "value"
                ) == 0) {
                screenshot_perform_source_click = 1;
                target_x = screenshot_width - 34;
                target_y = (int)(source_viewport_y + 36.0f);
                if (selected_cell->sources[number - 1u].state
                        == XLS_SOURCE_VALUE
                    && selected_cell->sources[number - 1u].value
                        != NULL) {
                    screenshot_expected_clipboard =
                        selected_cell->sources[number - 1u].value;
                }
            } else if (screenshot_source_action != NULL
                && strcmp(
                    screenshot_source_action, "path"
                ) == 0) {
                screenshot_perform_source_click = 1;
                screenshot_expected_clipboard =
                    selected_cell->sources[number - 1u].filepath;
            } else if (screenshot_source_action != NULL
                && strcmp(
                    screenshot_source_action, "reveal"
                ) == 0) {
                screenshot_perform_source_click = 1;
                target_x = screenshot_width - 34;
            }
            SDL_WarpMouseInWindow(
                window,
                target_x,
                target_y
            );
            if (screenshot_perform_source_click) {
                SDL_Event source_event;
                memset(&source_event, 0, sizeof(source_event));
                source_event.type = SDL_MOUSEMOTION;
                source_event.motion.windowID =
                    SDL_GetWindowID(window);
                source_event.motion.x = target_x;
                source_event.motion.y = target_y;
                (void)SDL_PushEvent(&source_event);
                memset(&source_event, 0, sizeof(source_event));
                source_event.type = SDL_MOUSEBUTTONDOWN;
                source_event.button.windowID =
                    SDL_GetWindowID(window);
                source_event.button.button = SDL_BUTTON_LEFT;
                source_event.button.state = SDL_PRESSED;
                source_event.button.x = target_x;
                source_event.button.y = target_y;
                (void)SDL_PushEvent(&source_event);
                memset(&source_event, 0, sizeof(source_event));
                source_event.type = SDL_MOUSEBUTTONUP;
                source_event.button.windowID =
                    SDL_GetWindowID(window);
                source_event.button.button = SDL_BUTTON_LEFT;
                source_event.button.state = SDL_RELEASED;
                source_event.button.x = target_x;
                source_event.button.y = target_y;
                (void)SDL_PushEvent(&source_event);
            }
        }
    }
    app.active_menu = app_menu_from_name(screenshot_menu);
    if (screenshot_dialog != NULL
        && strcmp(screenshot_dialog, "license") == 0) {
        app_show_license(&app);
    } else if (screenshot_dialog != NULL
        && strcmp(screenshot_dialog, "update") == 0) {
        xls_update_info preview;
        memset(&preview, 0, sizeof(preview));
        (void)snprintf(
            preview.latest_version,
            sizeof(preview.latest_version),
            "%s",
            "9.9.9"
        );
        (void)snprintf(
            preview.changelog,
            sizeof(preview.changelog),
            "%s",
            "文件定位会复用已打开的目录窗口并高亮目标文件；"
            "启动后会自动检查新版本。"
        );
        (void)snprintf(
            preview.download_url,
            sizeof(preview.download_url),
            "%s",
            "https://z-pulse.cn/products/xlsone/download.html"
        );
        app_show_update(&app, &preview);
    }
    if (screenshot_drop_target != NULL
        && screenshot_drop_target[0] != '\0') {
        app.drop_feedback = APP_DROP_HOVER;
    }
    if (screenshot_drop_path != NULL
        && screenshot_drop_path[0] != '\0') {
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_DROPBEGIN;
        (void)SDL_PushEvent(&event);
        memset(&event, 0, sizeof(event));
        event.type = SDL_DROPFILE;
        event.drop.file = SDL_strdup(screenshot_drop_path);
        if (event.drop.file == NULL || SDL_PushEvent(&event) <= 0) {
            SDL_free(event.drop.file);
            fprintf(stderr, "could not queue drop smoke event\n");
        }
        memset(&event, 0, sizeof(event));
        event.type = SDL_DROPCOMPLETE;
        (void)SDL_PushEvent(&event);
    }

    while (app.running) {
        SDL_Event event;
        int width;
        int height;
        nk_input_begin(context);
        while (SDL_PollEvent(&event)) {
            if (event.type == app.update_event_type) {
                app_handle_update_result(
                    &app,
                    (app_update_result *)event.user.data1
                );
            } else if (event.type == SDL_QUIT) {
                app.running = 0;
            } else if (event.type == SDL_DROPFILE) {
                if (xls_drop_path_is_workbook(event.drop.file)) {
                    if (!app_queue_dropped_path(
                        event.drop.file, &app
                    )) {
                        app.drop_feedback = APP_DROP_REJECTED;
                        app.drop_feedback_until =
                            SDL_GetTicks64() + 1400u;
                    }
                } else {
                    app_set_status(
                        &app,
                        "仅支持 .xlsx 和 .xls 工作簿。"
                    );
                    app.drop_feedback = APP_DROP_REJECTED;
                    app.drop_feedback_until =
                        SDL_GetTicks64() + 1400u;
                }
                SDL_free(event.drop.file);
#if SDL_VERSION_ATLEAST(2, 0, 5)
            } else if (event.type == SDL_DROPTEXT) {
                if (xls_drop_paths_from_text(
                    event.drop.file,
                    app_queue_dropped_path,
                    &app
                ) == 0u) {
                    app_set_status(
                        &app,
                        "仅支持 .xlsx 和 .xls 工作簿。"
                    );
                    app.drop_feedback = APP_DROP_REJECTED;
                    app.drop_feedback_until =
                        SDL_GetTicks64() + 1400u;
                }
                SDL_free(event.drop.file);
            } else if (event.type == SDL_DROPBEGIN) {
                app.drop_feedback = APP_DROP_HOVER;
                app.drop_feedback_until = 0u;
            } else if (event.type == SDL_DROPCOMPLETE) {
                if (app.pending_drop_count > 0u) {
                    app.pending_drop_complete = 1;
                    app.drop_feedback = APP_DROP_IMPORTING;
                } else if (app.drop_feedback == APP_DROP_HOVER) {
                    app.drop_feedback = APP_DROP_IDLE;
                }
#endif
            } else if (event.type == SDL_WINDOWEVENT
                && (event.window.event == SDL_WINDOWEVENT_RESIZED
                    || event.window.event
                        == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                (void)SDL_RenderSetLogicalSize(
                    renderer,
                    event.window.data1,
                    event.window.data2
                );
            } else if (event.type == SDL_MOUSEWHEEL
                && app.dialog == APP_DIALOG_NONE
                && app.active_menu == APP_MENU_NONE
                && app.selected_sheet < app.merged_sheet_count) {
                int event_width;
                int event_height;
                int mouse_x;
                int mouse_y;
                float inspector_width;
                int pointer_in_inspector;
                SDL_GetWindowSize(
                    window, &event_width, &event_height
                );
                (void)SDL_GetMouseState(&mouse_x, &mouse_y);
                inspector_width = event_width >= 1120
                    ? 288.0f
                    : 280.0f;
                pointer_in_inspector =
                    (float)mouse_x
                        >= (float)event_width - inspector_width
                    && (float)mouse_y >= UI_SHEET_BOTTOM
                    && mouse_y < event_height - 17;
                if (pointer_in_inspector) {
                    double wheel_delta = (double)event.wheel.y;
                    const xls_merged_cell *selected_cell =
                        xls_merged_sheet_cell(
                            &app.merged_sheets[
                                app.selected_sheet
                            ],
                            app.selected_row,
                            app.selected_column
                        );
#if SDL_VERSION_ATLEAST(2, 0, 4)
                    if (event.wheel.direction
                        == SDL_MOUSEWHEEL_FLIPPED) {
                        wheel_delta = -wheel_delta;
                    }
#endif
                    if (selected_cell != NULL
                        && app.sources_expanded) {
                        app.source_scroll_offset =
                            xls_source_list_scroll_wheel(
                                app.source_scroll_offset,
                                wheel_delta,
                                selected_cell->source_count,
                                UI_SOURCE_ROW_HEIGHT,
                                app.source_scroll_viewport_height
                            );
                    }
                } else if ((SDL_GetModState() & KMOD_SHIFT) != 0
                    || event.wheel.x != 0) {
                    const int delta = event.wheel.x != 0
                        ? event.wheel.x
                        : event.wheel.y;
                    if (delta > 0) {
                        const size_t amount = (size_t)delta;
                        app.first_visible_column = amount > app.first_visible_column
                            ? 0
                            : app.first_visible_column - amount;
                    } else if (delta < 0) {
                        app.first_visible_column += (size_t)(-delta);
                    }
                } else if (event.wheel.y > 0) {
                    const size_t amount = (size_t)event.wheel.y * 3u;
                    app.first_visible_row = amount > app.first_visible_row
                        ? 0
                        : app.first_visible_row - amount;
                } else if (event.wheel.y < 0) {
                    app.first_visible_row += (size_t)(-event.wheel.y) * 3u;
                }
            } else if (event.type == SDL_TEXTINPUT
                && app.dialog == APP_DIALOG_LICENSE
                && app.license_page == 0
                && app.license.state != XLS_LICENSE_ACTIVATED) {
                app_set_license_key_input(
                    &app, app.license_key, event.text.text
                );
            } else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                const SDL_Keymod modifiers =
                    (SDL_Keymod)event.key.keysym.mod;
                const int command = (modifiers & (KMOD_CTRL | KMOD_GUI)) != 0;
                if (key == SDLK_ESCAPE) {
                    if (app.dialog != APP_DIALOG_NONE) {
                        app_close_dialog(&app);
                    } else {
                        app.active_menu = APP_MENU_NONE;
                    }
                } else if (app.dialog == APP_DIALOG_LICENSE
                    && app.license_page == 0
                    && app.license.state != XLS_LICENSE_ACTIVATED
                    && key == SDLK_BACKSPACE) {
                    app_backspace_license_key(&app);
                } else if (app.dialog == APP_DIALOG_LICENSE
                    && app.license_page == 0
                    && app.license.state != XLS_LICENSE_ACTIVATED
                    && command
                    && key == SDLK_v) {
                    char *clipboard = SDL_GetClipboardText();
                    if (clipboard != NULL) {
                        app_set_license_key_input(
                            &app, app.license_key, clipboard
                        );
                        SDL_free(clipboard);
                    }
                } else if (app.dialog == APP_DIALOG_NONE
                    && key == SDLK_F1) {
                    app_execute_action(&app, APP_ACTION_HELP);
                } else if (app.dialog == APP_DIALOG_NONE && command) {
                    if (key == SDLK_PAGEUP
                        && app.selected_sheet > 0u) {
                        --app.selected_sheet;
                        app.first_visible_sheet =
                            app.selected_sheet;
                        app.has_selection = 0;
                        app.first_visible_row = 0u;
                        app.first_visible_column = 0u;
                    } else if (key == SDLK_PAGEDOWN
                        && app.selected_sheet + 1u
                            < app.merged_sheet_count) {
                        ++app.selected_sheet;
                        app.first_visible_sheet =
                            app.selected_sheet;
                        app.has_selection = 0;
                        app.first_visible_row = 0u;
                        app.first_visible_column = 0u;
                    } else if (key == SDLK_o) {
                        app_open_files(
                            &app,
                            (modifiers & KMOD_SHIFT) != 0
                        );
                    } else if (key == SDLK_s) {
                        app_export(&app);
                    } else if (key == SDLK_n) {
                        app_clear(&app);
                    } else if (key == SDLK_r) {
                        (void)app_recompute(&app);
                    } else if (key == SDLK_z) {
                        app_undo_last_override(&app);
                    } else if (key == SDLK_COMMA) {
                        app_show_rules(&app);
                    } else if (key == SDLK_q) {
                        app.running = 0;
                    }
                }
            }
            (void)nk_sdl_handle_event(&event);
        }
        if (app.automatic_update_pending
            && SDL_GetTicks64() >= app.automatic_update_at) {
            app.automatic_update_pending = 0;
            app_start_update_check(&app, 1);
        }
        nk_sdl_handle_grab();
        nk_input_end(context);
        if (app.language_layout_dirty) {
            app_select_language_fonts(
                context,
                &fonts,
                &default_fonts,
                &japanese_fonts
            );
            app_apply_language_layout(&app, &fonts);
        }
        SDL_GetWindowSize(window, &width, &height);
        if (app.drop_feedback_until != 0u
            && SDL_GetTicks64() >= app.drop_feedback_until) {
            app.drop_feedback = APP_DROP_IDLE;
            app.drop_feedback_until = 0u;
        }
        render_workspace(context, &app, &fonts, width, height);
        SDL_SetRenderDrawColor(renderer, 246, 247, 250, 255);
        SDL_RenderClear(renderer);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        if (!screenshot_saved
            && screenshot_path != NULL
            && screenshot_path[0] != '\0'
            && (screenshot_drop_path == NULL
                || app.pending_drop_count == 0u)) {
            if (screenshot_require_workbooks != NULL
                && screenshot_require_workbooks[0] != '\0'
                && app.workbook_count == 0u) {
                fprintf(
                    stderr,
                    "screenshot smoke test did not import any workbooks: %s\n",
                    app.status
                );
                screenshot_failed = 1;
            } else {
                if (screenshot_expected_clipboard != NULL) {
                    char *clipboard = SDL_GetClipboardText();
                    if (clipboard == NULL
                        || strcmp(
                            clipboard,
                            screenshot_expected_clipboard
                        ) != 0) {
                        fprintf(
                            stderr,
                            "source click did not copy expected text\n"
                        );
                        screenshot_failed = 1;
                    }
                    SDL_free(clipboard);
                }
                if (screenshot_source_action != NULL
                    && strcmp(
                        screenshot_source_action, "reveal"
                    ) == 0
                    && (app.source_reveal_attempt_count != 1u
                        || app.source_reveal_failure_count
                            != 0u)) {
                    fprintf(
                        stderr,
                        "source reveal action did not locate the file\n"
                    );
                    screenshot_failed = 1;
                }
                screenshot_saved = save_renderer_bmp(
                    renderer, screenshot_path
                );
            }
            app.running = 0;
        }
        SDL_RenderPresent(renderer);
        app_process_pending_drop(&app);
    }

    app_clear_pending_drop(&app);
    app_clear(&app);
    if (app.update_thread != NULL) {
        SDL_Event update_event;
        SDL_WaitThread(app.update_thread, NULL);
        app.update_thread = NULL;
        while (SDL_PeepEvents(
            &update_event,
            1,
            SDL_GETEVENT,
            app.update_event_type,
            app.update_event_type
        ) > 0) {
            free(update_event.user.data1);
        }
    }
    nk_sdl_shutdown();
    xls_platform_drop_target_shutdown(&native_drop_target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return screenshot_failed ? 2 : 0;
}
