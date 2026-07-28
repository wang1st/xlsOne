#include "xlsone/xlsone.h"
#include "platform_dialog.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct app_state {
    xls_workbook *workbooks;
    size_t workbook_count;
    xls_validation_report validation;
    xls_merged_sheet *merged_sheets;
    size_t merged_sheet_count;
    size_t selected_sheet;
    size_t selected_row;
    size_t selected_column;
    int has_selection;
    char status[512];
} app_state;

static void app_set_status(app_state *app, const char *message)
{
    (void)snprintf(
        app->status,
        sizeof(app->status),
        "%s",
        message == NULL ? "" : message
    );
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
    app->has_selection = 0;
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
    (void)snprintf(
        app->status,
        sizeof(app->status),
        "已导入 %zu 个工作簿，识别 %zu 个可汇总工作表。",
        app->workbook_count,
        app->merged_sheet_count
    );
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
    if (app_has_path(app, path)) {
        return 1;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (!xls_parse_file(path, &parsed, &error)) {
        app_set_status(app, error.message);
        return 0;
    }
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

static void app_open_files(app_state *app)
{
    char **paths = NULL;
    size_t path_count = 0;
    size_t index;
    int changed = 0;
    if (!xls_platform_open_files(&paths, &path_count)) {
        return;
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
            "",
            &error
        );
    } else {
        ok = xls_export_xlsx(
            app->workbooks[app->validation.template_workbook_index].filepath,
            app->merged_sheets,
            app->merged_sheet_count,
            path,
            "",
            &error
        );
    }
    if (ok) {
        (void)snprintf(
            app->status,
            sizeof(app->status),
            "导出完成：%s",
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
    if (cell == NULL || !xls_merged_cell_set_kind(cell, kind, &error)) {
        app_set_status(
            app,
            cell == NULL ? "没有选中的汇总单元格。" : error.message
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
    if (cell == NULL || !xls_merged_cell_restore_automatic(cell, &error)) {
        app_set_status(
            app,
            cell == NULL ? "没有选中的汇总单元格。" : error.message
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
    app_set_status(app, "已清除全部手动修正。");
}

static void apply_theme(struct nk_context *context)
{
    struct nk_color table[NK_COLOR_COUNT];
    table[NK_COLOR_TEXT] = nk_rgba(31, 41, 55, 255);
    table[NK_COLOR_WINDOW] = nk_rgba(247, 248, 250, 255);
    table[NK_COLOR_HEADER] = nk_rgba(237, 242, 239, 255);
    table[NK_COLOR_BORDER] = nk_rgba(211, 218, 214, 255);
    table[NK_COLOR_BUTTON] = nk_rgba(30, 113, 82, 255);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgba(24, 126, 88, 255);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(20, 92, 68, 255);
    table[NK_COLOR_TOGGLE] = nk_rgba(226, 232, 228, 255);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(212, 226, 218, 255);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(30, 113, 82, 255);
    table[NK_COLOR_SELECT] = nk_rgba(226, 236, 231, 255);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgba(30, 113, 82, 255);
    table[NK_COLOR_SLIDER] = nk_rgba(226, 232, 228, 255);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgba(30, 113, 82, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(24, 126, 88, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(20, 92, 68, 255);
    table[NK_COLOR_PROPERTY] = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_EDIT] = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgba(31, 41, 55, 255);
    table[NK_COLOR_COMBO] = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_CHART] = nk_rgba(255, 255, 255, 255);
    table[NK_COLOR_CHART_COLOR] = nk_rgba(30, 113, 82, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(223, 111, 54, 255);
    table[NK_COLOR_SCROLLBAR] = nk_rgba(236, 239, 237, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(180, 192, 185, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(150, 169, 158, 255);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(120, 151, 134, 255);
    table[NK_COLOR_TAB_HEADER] = nk_rgba(232, 238, 234, 255);
    nk_style_from_table(context, table);
    context->style.window.padding = nk_vec2(12.0f, 12.0f);
    context->style.button.rounding = 5.0f;
    context->style.window.rounding = 0.0f;
}

static void render_empty_state(struct nk_context *context)
{
    nk_layout_row_dynamic(context, 90, 1);
    nk_spacing(context, 1);
    nk_layout_row_dynamic(context, 34, 1);
    nk_label(context, "把多个同模板 Excel 汇总成一张表", NK_TEXT_CENTERED);
    nk_layout_row_dynamic(context, 28, 1);
    nk_label(
        context,
        "拖入 .xlsx / .xls 文件，或点击上方“打开文件”",
        NK_TEXT_CENTERED
    );
    nk_layout_row_dynamic(context, 44, 3);
    nk_spacing(context, 1);
    nk_label(context, "支持多工作表、来源穿透和模板格式导出", NK_TEXT_CENTERED);
    nk_spacing(context, 1);
}

static void render_files_panel(struct nk_context *context, app_state *app)
{
    size_t index;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "已导入文件", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 140, 1);
    if (nk_group_begin(context, "imported-files", NK_WINDOW_BORDER)) {
        for (index = 0; index < app->workbook_count; ++index) {
            nk_layout_row_dynamic(context, 24, 1);
            nk_label(
                context,
                app->workbooks[index].filename,
                NK_TEXT_LEFT
            );
        }
        nk_group_end(context);
    }
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "单元格来源", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 310, 1);
    if (nk_group_begin(context, "cell-sources", NK_WINDOW_BORDER)) {
        if (app->has_selection && app->selected_sheet < app->merged_sheet_count) {
            xls_merged_cell *cell = xls_merged_sheet_cell_mutable(
                &app->merged_sheets[app->selected_sheet],
                app->selected_row,
                app->selected_column
            );
            if (cell != NULL) {
                char position[96];
                (void)snprintf(
                    position,
                    sizeof(position),
                    "第 %zu 行，第 %zu 列  ·  %s",
                    app->selected_row + 1,
                    app->selected_column + 1,
                    xls_cell_kind_name(cell->kind)
                );
                nk_layout_row_dynamic(context, 24, 1);
                nk_label(context, position, NK_TEXT_LEFT);
                nk_layout_row_dynamic(context, 28, 1);
                nk_label(context, cell->display_value, NK_TEXT_LEFT);
                nk_layout_row_dynamic(context, 28, 4);
                if (nk_button_label(context, "求和")) {
                    app_set_selected_kind(app, XLS_CELL_SUM);
                }
                if (nk_button_label(context, "标签")) {
                    app_set_selected_kind(app, XLS_CELL_LABEL);
                }
                if (nk_button_label(context, "多值")) {
                    app_set_selected_kind(app, XLS_CELL_MIXED);
                }
                if (nk_button_label(context, "自动")) {
                    app_restore_selected_kind(app);
                }
                for (index = 0; index < cell->source_count; ++index) {
                    const xls_source_entry *source = &cell->sources[index];
                    nk_layout_row_dynamic(context, 20, 1);
                    nk_label(context, source->filename, NK_TEXT_LEFT);
                    nk_layout_row_dynamic(context, 22, 1);
                    nk_label(
                        context,
                        source->state == XLS_SOURCE_MISSING
                            ? "缺失"
                            : source->state == XLS_SOURCE_EMPTY
                                ? "空值"
                                : source->value,
                        NK_TEXT_LEFT
                    );
                }
            }
        } else {
            nk_layout_row_dynamic(context, 24, 1);
            nk_label(context, "点击汇总单元格查看各文件原值。", NK_TEXT_LEFT);
        }
        nk_group_end(context);
    }
}

static void render_grid(struct nk_context *context, app_state *app)
{
    xls_merged_sheet *sheet;
    size_t row;
    size_t column;
    size_t visible_rows;
    size_t visible_columns;
    if (app->selected_sheet >= app->merged_sheet_count) {
        return;
    }
    sheet = &app->merged_sheets[app->selected_sheet];
    visible_rows = sheet->row_count > 1000 ? 1000 : sheet->row_count;
    visible_columns = sheet->column_count > 80 ? 80 : sheet->column_count;
    if (nk_group_begin(
        context,
        "summary-grid",
        NK_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE
    )) {
        nk_layout_row_begin(
            context,
            NK_STATIC,
            28,
            (int)visible_columns + 1
        );
        nk_layout_row_push(context, 52);
        nk_label(context, "", NK_TEXT_CENTERED);
        for (column = 0; column < visible_columns; ++column) {
            char letters[16];
            size_t number = column;
            size_t length = 0;
            do {
                memmove(letters + 1, letters, length);
                letters[0] = (char)('A' + number % 26);
                ++length;
                number = number / 26;
                if (number > 0) {
                    --number;
                }
            } while (number > 0 && length + 1 < sizeof(letters));
            letters[length] = '\0';
            nk_layout_row_push(context, 128);
            nk_label(context, letters, NK_TEXT_CENTERED);
        }
        nk_layout_row_end(context);
        for (row = 0; row < visible_rows; ++row) {
            char row_number[32];
            nk_layout_row_begin(
                context,
                NK_STATIC,
                29,
                (int)visible_columns + 1
            );
            nk_layout_row_push(context, 52);
            (void)snprintf(row_number, sizeof(row_number), "%zu", row + 1);
            nk_label(context, row_number, NK_TEXT_CENTERED);
            for (column = 0; column < visible_columns; ++column) {
                const xls_merged_cell *cell
                    = xls_merged_sheet_cell(sheet, row, column);
                nk_layout_row_push(context, 128);
                if (nk_button_label(
                    context,
                    cell == NULL || cell->display_value == NULL
                        ? ""
                        : cell->display_value
                )) {
                    app->selected_row = row;
                    app->selected_column = column;
                    app->has_selection = 1;
                }
            }
            nk_layout_row_end(context);
        }
        nk_group_end(context);
    }
}

static void render_workspace(
    struct nk_context *context,
    app_state *app,
    int width,
    int height
)
{
    size_t index;
    float body_height = (float)height - 114.0f;
    if (nk_begin(
        context,
        "xlsOne",
        nk_rect(0, 0, (float)width, (float)height),
        NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND
    )) {
        nk_layout_row_begin(context, NK_STATIC, 38, 6);
        nk_layout_row_push(context, 104);
        if (nk_button_label(context, "打开文件")) {
            app_open_files(app);
        }
        nk_layout_row_push(context, 76);
        if (nk_button_label(context, "导出")) {
            app_export(app);
        }
        nk_layout_row_push(context, 68);
        if (nk_button_label(context, "清空")) {
            app_clear(app);
        }
        nk_layout_row_push(context, 94);
        if (nk_button_label(context, "清除修正")) {
            app_clear_overrides(app);
        }
        nk_layout_row_push(context, 120);
        nk_label(context, "xlsOne 纯 C 版", NK_TEXT_LEFT);
        nk_layout_row_push(context, (float)width - 526.0f);
        nk_label(context, "零 Qt 依赖", NK_TEXT_RIGHT);
        nk_layout_row_end(context);

        if (app->workbook_count == 0) {
            render_empty_state(context);
        } else {
            nk_layout_row_begin(context, NK_STATIC, body_height, 2);
            nk_layout_row_push(context, 266);
            if (nk_group_begin(
                context,
                "left-panel",
                NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR
            )) {
                render_files_panel(context, app);
                nk_group_end(context);
            }
            nk_layout_row_push(context, (float)width - 306.0f);
            if (nk_group_begin(
                context,
                "workspace",
                NK_WINDOW_NO_SCROLLBAR
            )) {
                if (app->merged_sheet_count > 0) {
                    nk_layout_row_dynamic(context, 32, (int)app->merged_sheet_count);
                    for (index = 0; index < app->merged_sheet_count; ++index) {
                        nk_bool selected = index == app->selected_sheet;
                        if (nk_selectable_label(
                            context,
                            app->merged_sheets[index].sheet_name,
                            NK_TEXT_CENTERED,
                            &selected
                        ) && selected) {
                            app->selected_sheet = index;
                            app->has_selection = 0;
                        }
                    }
                    nk_layout_row_dynamic(context, body_height - 52.0f, 1);
                    render_grid(context, app);
                } else {
                    nk_layout_row_dynamic(context, 32, 1);
                    nk_label(
                        context,
                        "模板结构不一致，当前没有可汇总工作表。",
                        NK_TEXT_LEFT
                    );
                    for (index = 0; index < app->validation.issue_count; ++index) {
                        nk_layout_row_dynamic(context, 24, 1);
                        nk_label(
                            context,
                            app->validation.issues[index].message,
                            NK_TEXT_LEFT
                        );
                    }
                }
                nk_group_end(context);
            }
            nk_layout_row_end(context);
        }
        nk_layout_row_dynamic(context, 26, 1);
        nk_label(context, app->status, NK_TEXT_LEFT);
    }
    nk_end(context);
}

static const char *find_font_path(void)
{
    static const char *const candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
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
    struct nk_font *font = NULL;
    const char *font_path;
    const char *screenshot_path;
    app_state app;
    int running = 1;
    int screenshot_saved = 0;
    int index;
    memset(&app, 0, sizeof(app));
    app_set_status(&app, "可拖入或打开多个 Excel 工作簿。");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    window = SDL_CreateWindow(
        "xlsOne — 纯 C 版",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        800,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
    );
    if (window == NULL) {
        fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 980, 640);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
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
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    context = nk_sdl_init(window, renderer);
    nk_sdl_font_stash_begin(&atlas);
    font_path = find_font_path();
    if (font_path != NULL) {
        struct nk_font_config config = nk_font_config(18.0f);
        config.range = nk_font_chinese_glyph_ranges();
        config.oversample_h = 1;
        config.oversample_v = 1;
        config.pixel_snap = 1;
        font = nk_font_atlas_add_from_file(
            atlas, font_path, 18.0f, &config
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
    if (font != NULL) {
        nk_style_set_font(context, &font->handle);
    }
    apply_theme(context);
    screenshot_path = getenv("XLSONE_SCREENSHOT_PATH");

    for (index = 1; index < argc; ++index) {
        (void)app_add_path(&app, argv[index]);
    }
    if (argc > 1) {
        (void)app_recompute(&app);
    }

    while (running) {
        SDL_Event event;
        int width;
        int height;
        nk_input_begin(context);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_DROPFILE) {
                if (app_add_path(&app, event.drop.file)) {
                    (void)app_recompute(&app);
                }
                SDL_free(event.drop.file);
            }
            (void)nk_sdl_handle_event(&event);
        }
        nk_sdl_handle_grab();
        nk_input_end(context);
        SDL_GetWindowSize(window, &width, &height);
        render_workspace(context, &app, width, height);
        SDL_SetRenderDrawColor(renderer, 247, 248, 250, 255);
        SDL_RenderClear(renderer);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        if (!screenshot_saved
            && screenshot_path != NULL
            && screenshot_path[0] != '\0') {
            screenshot_saved = save_renderer_bmp(renderer, screenshot_path);
            running = 0;
        }
        SDL_RenderPresent(renderer);
    }

    app_clear(&app);
    nk_sdl_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
