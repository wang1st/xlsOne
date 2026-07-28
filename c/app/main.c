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
    size_t first_visible_row;
    size_t first_visible_column;
    int sources_expanded;
    int drop_targeted;
    char status[512];
} app_state;

typedef struct ui_fonts {
    const struct nk_user_font *body;
    const struct nk_user_font *title;
    const struct nk_user_font *numeric;
    const struct nk_user_font *source_value;
} ui_fonts;

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
    app->first_visible_row = 0;
    app->first_visible_column = 0;
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

static int ui_hovered(const struct nk_context *context, struct nk_rect rect)
{
    return nk_input_is_mouse_hovering_rect(&context->input, rect) != 0;
}

static int ui_clicked(const struct nk_context *context, struct nk_rect rect)
{
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
        return "求和";
    case XLS_CELL_MIXED:
        return "混合";
    case XLS_CELL_SINGLE:
        return "单值";
    case XLS_CELL_LABEL:
    default:
        return "标签";
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
    const struct nk_rect toolbar = nk_rect(0.0f, 0.0f, width, 40.0f);
    nk_fill_rect(canvas, toolbar, 0.0f, UI_CHROME);
    nk_stroke_line(canvas, 0.0f, 39.5f, width, 39.5f, 1.0f, UI_BORDER);

    if (has_workspace) {
        const struct nk_rect group = nk_rect(12.0f, 7.0f, 167.0f, 32.0f);
        nk_fill_rect(canvas, group, 11.0f, UI_BG1);
        nk_stroke_rect(
            canvas,
            nk_rect(group.x + 0.5f, group.y + 0.5f, group.w - 1.0f, group.h - 1.0f),
            11.0f,
            1.0f,
            UI_BORDER
        );
        if (ui_draw_utility_button(
            context, canvas, nk_rect(16.0f, 10.0f, 51.0f, 26.0f),
            "追加", fonts->body, UI_ICON_PLUS
        )) {
            app_open_files(app);
        }
        if (ui_draw_utility_button(
            context, canvas, nk_rect(68.0f, 10.0f, 54.0f, 26.0f),
            "刷新", fonts->body, UI_ICON_REFRESH
        )) {
            (void)app_recompute(app);
        }
        if (ui_draw_utility_button(
            context, canvas, nk_rect(123.0f, 10.0f, 52.0f, 26.0f),
            "清空", fonts->body, UI_ICON_CLOSE
        )) {
            app_clear(app);
        }
    }

    {
        const float license_x = has_workspace ? width - 208.0f : width - 105.0f;
        const struct nk_rect license = nk_rect(license_x, 11.0f, 94.0f, 23.0f);
        nk_fill_rect(canvas, license, 11.0f, nk_rgba(255, 255, 255, 110));
        nk_stroke_rect(
            canvas,
            nk_rect(license.x + 0.5f, license.y + 0.5f, license.w - 1.0f, license.h - 1.0f),
            11.0f,
            1.0f,
            UI_ACCENT
        );
        ui_draw_text(
            canvas,
            license,
            "纯 C 版 · 1.1.1",
            fonts->body,
            UI_ACCENT,
            NK_TEXT_CENTERED
        );
    }

    if (has_workspace) {
        if (ui_draw_button(
            context,
            canvas,
            nk_rect(width - 102.0f, 7.0f, 91.0f, 32.0f),
            "导出 XLSX",
            fonts->body,
            UI_ICON_EXPORT,
            1
        )) {
            app_export(app);
        }
    }
}

static void render_sheet_strip(
    struct nk_context *context,
    struct nk_command_buffer *canvas,
    app_state *app,
    const ui_fonts *fonts,
    float width
)
{
    float x = 9.0f;
    size_t index;
    nk_fill_rect(canvas, nk_rect(0.0f, 40.0f, width, 78.0f), 0.0f, UI_CHROME);
    nk_stroke_line(canvas, 0.0f, 117.5f, width, 117.5f, 1.0f, UI_BORDER);
    for (index = 0; index < app->merged_sheet_count; ++index) {
        const char *name = app->merged_sheets[index].sheet_name;
        const int selected = index == app->selected_sheet;
        const float button_width = ui_text_width(fonts->body, name) + 16.0f;
        const struct nk_rect button = nk_rect(x, 70.0f, button_width, 26.0f);
        nk_fill_rect(
            canvas,
            button,
            13.0f,
            selected ? UI_ACCENT_SOFT : UI_BG2
        );
        nk_stroke_rect(
            canvas,
            nk_rect(button.x + 0.5f, button.y + 0.5f, button.w - 1.0f, button.h - 1.0f),
            13.0f,
            1.0f,
            selected ? UI_SUM_BORDER : UI_BORDER
        );
        ui_draw_text(
            canvas,
            button,
            name,
            fonts->body,
            selected ? UI_TEXT : UI_MUTED,
            NK_TEXT_CENTERED
        );
        if (ui_clicked(context, button)) {
            app->selected_sheet = index;
            app->has_selection = 0;
            app->first_visible_row = 0;
            app->first_visible_column = 0;
        }
        x += button_width + 8.0f;
    }
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
    const float body_top = 40.0f;
    const float body_height = status_y - body_top;
    float card_width = width - 64.0f;
    float card_height = body_height - 64.0f;
    struct nk_rect card;
    struct nk_rect open_button;
    if (card_width > 416.0f) {
        card_width = 416.0f;
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
        nk_rgba(0, 0, 0, app->drop_targeted ? 30 : 18)
    );
    nk_fill_rect(canvas, card, 16.0f, UI_BG1);
    nk_stroke_rect(
        canvas,
        nk_rect(card.x + 0.5f, card.y + 0.5f, card.w - 1.0f, card.h - 1.0f),
        16.0f,
        app->drop_targeted ? 2.0f : 1.0f,
        app->drop_targeted ? UI_ACCENT : UI_BORDER
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
        "表表归一",
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
        app->drop_targeted ? "松手即可导入" : "拖入 Excel 文件",
        fonts->title,
        UI_TEXT,
        NK_TEXT_CENTERED
    );
    ui_draw_text(
        canvas,
        nk_rect(card.x + 38.0f, card.y + 217.0f, card.w - 76.0f, 28.0f),
        app->drop_targeted
            ? "支持多个 .xlsx / .xls"
            : "支持多个 .xlsx / .xls，自动识别表头与可汇总列",
        fonts->body,
        UI_MUTED,
        NK_TEXT_CENTERED
    );
    open_button = nk_rect(card.x + card.w * 0.5f - 53.0f, card.y + card.h - 74.0f, 106.0f, 32.0f);
    if (ui_draw_button(
        context,
        canvas,
        open_button,
        "选择文件",
        fonts->body,
        UI_ICON_FOLDER,
        1
    )) {
        app_open_files(app);
    }
    ui_draw_text(
        canvas,
        nk_rect(card.x, card.y + card.h - 31.0f, card.w, 16.0f),
        "也可以按 Ctrl+O 选择文件",
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
        nk_fill_rect(canvas, placeholder, 9.0f, UI_BG1);
        nk_stroke_rect(canvas, placeholder, 9.0f, 1.0f, UI_BORDER_SOFT);
        ui_draw_text(
            canvas,
            nk_rect(placeholder.x + 10.0f, placeholder.y, placeholder.w - 20.0f, placeholder.h),
            "选择一个单元格查看来源。",
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
        ui_draw_text_elided(
            canvas,
            nk_rect(detail.x + 12.0f, detail.y + 35.0f, detail.w - 24.0f, 31.0f),
            cell->display_value == NULL || cell->display_value[0] == '\0'
                ? "空值"
                : cell->display_value,
            fonts->numeric,
            UI_TEXT,
            NK_TEXT_CENTERED
        );
        ui_draw_text_elided(
            canvas,
            nk_rect(detail.x + 12.0f, detail.y + 70.0f, detail.w - 24.0f, 20.0f),
            cell->is_overridden != 0u
                ? "当前使用手动修正后的单元格类型"
                : cell->decision.reason,
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
            "标签",
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
            "求和",
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
            ui_draw_text(canvas, restore, "恢复自动判断", fonts->body, UI_MUTED, NK_TEXT_CENTERED);
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
            + (app->sources_expanded ? (float)cell->source_count * 56.0f : 0.0f);
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
                "来源明细",
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
                (void)snprintf(summary, sizeof(summary), "%zu 个有效", overview.value_count);
            } else {
                (void)snprintf(summary, sizeof(summary), "%zu 个来源", cell->source_count);
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
            nk_push_scissor(
                canvas,
                nk_rect(source_card.x + 1.0f, source_card.y + 57.0f, source_card.w - 2.0f, source_card.h - 58.0f)
            );
            for (source_index = 0; source_index < cell->source_count; ++source_index) {
                const xls_source_entry *source = &cell->sources[source_index];
                const float row_y = source_card.y + 58.0f + (float)source_index * 56.0f;
                const char *value = source->state == XLS_SOURCE_MISSING
                    ? "缺失"
                    : source->state == XLS_SOURCE_EMPTY
                        ? "空值"
                        : source->value;
                const int outlier = overview_ok
                    && ui_source_is_outlier(&overview, source_index);
                if (row_y >= source_card.y + source_card.h) {
                    break;
                }
                if (source_index > 0) {
                    nk_stroke_line(
                        canvas,
                        source_card.x + 10.0f,
                        row_y,
                        source_card.x + source_card.w - 10.0f,
                        row_y,
                        1.0f,
                        UI_BORDER_SOFT
                    );
                }
                ui_draw_text_elided(
                    canvas,
                    nk_rect(source_card.x + 10.0f, row_y + 4.0f, source_card.w - 20.0f, 17.0f),
                    source->filename,
                    fonts->body,
                    UI_MUTED,
                    NK_TEXT_LEFT
                );
                ui_draw_text_elided(
                    canvas,
                    nk_rect(source_card.x + 10.0f, row_y + 25.0f, source_card.w - 20.0f, 20.0f),
                    value == NULL ? "" : value,
                    fonts->source_value,
                    outlier ? UI_WARNING : UI_TEXT,
                    NK_TEXT_RIGHT
                );
            }
            nk_push_scissor(canvas, nk_rect(0.0f, 0.0f, 32768.0f, 32768.0f));
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
    if (app->merged_sheet_count > 0
        && app->selected_sheet < app->merged_sheet_count) {
        const xls_merged_sheet *sheet = &app->merged_sheets[app->selected_sheet];
        (void)snprintf(
            viewing,
            sizeof(viewing),
            "正在查看“%s”，共 %zu 行。",
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
    const struct nk_rect clear_button = nk_rect(width - 112.0f, y + 5.0f, 101.0f, 28.0f);
    nk_fill_rect(canvas, nk_rect(0.0f, y, width, 38.0f), 0.0f, UI_BG1);
    nk_stroke_line(canvas, 0.0f, y + 0.5f, width, y + 0.5f, 1.0f, UI_BORDER);
    ui_draw_text(
        canvas,
        nk_rect(14.0f, y, width - 140.0f, 38.0f),
        "当前工作区包含手动修正。",
        fonts->body,
        UI_MUTED,
        NK_TEXT_LEFT
    );
    if (ui_draw_button(
        context,
        canvas,
        clear_button,
        "清除所有修正",
        fonts->body,
        UI_ICON_NONE,
        0
    )) {
        app_clear_overrides(app);
    }
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
                nk_rect(0.0f, 118.0f, table_width, content_bottom - 118.0f)
            );
            render_inspector(
                context,
                canvas,
                app,
                fonts,
                nk_rect(table_width, 118.0f, inspector_width, content_bottom - 118.0f)
            );
        } else {
            size_t index;
            nk_fill_rect(
                canvas,
                nk_rect(0.0f, 40.0f, (float)width, content_bottom - 40.0f),
                0.0f,
                UI_BG0
            );
            ui_draw_text(
                canvas,
                nk_rect(36.0f, 100.0f, (float)width - 72.0f, 32.0f),
                "模板结构不一致，当前没有可汇总工作表。",
                fonts->title,
                UI_TEXT,
                NK_TEXT_CENTERED
            );
            for (index = 0; index < app->validation.issue_count && index < 8; ++index) {
                ui_draw_text_elided(
                    canvas,
                    nk_rect(80.0f, 145.0f + (float)index * 25.0f, (float)width - 160.0f, 22.0f),
                    app->validation.issues[index].message,
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

static const nk_rune *title_glyph_ranges(void)
{
    static const nk_rune ranges[] = {
        0x0020, 0x007e,
        0x3002, 0x3002,
        0x4e00, 0x4e00,
        0x4e0d, 0x4e0d,
        0x4ef6, 0x4ef6,
        0x4f5c, 0x4f5c,
        0x5165, 0x5165,
        0x524d, 0x524d,
        0x533a, 0x533a,
        0x5373, 0x5373,
        0x53ef, 0x53ef,
        0x5bfc, 0x5bfc,
        0x5de5, 0x5de5,
        0x5f52, 0x5f52,
        0x5f53, 0x5f53,
        0x603b, 0x603b,
        0x624b, 0x624b,
        0x62d6, 0x62d6,
        0x6587, 0x6587,
        0x6709, 0x6709,
        0x677e, 0x677f,
        0x6784, 0x6784,
        0x6a21, 0x6a21,
        0x6ca1, 0x6ca1,
        0x6c47, 0x6c47,
        0x7ed3, 0x7ed3,
        0x81f4, 0x81f4,
        0x8868, 0x8868,
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
    struct nk_font *numeric_font = NULL;
    struct nk_font *source_value_font = NULL;
    ui_fonts fonts;
    const char *font_path;
    const char *bold_font_path;
    const char *monospace_font_path;
    const char *screenshot_path;
    const char *screenshot_selection;
    app_state app;
    int running = 1;
    int screenshot_saved = 0;
    int index;
    memset(&app, 0, sizeof(app));
    memset(&fonts, 0, sizeof(fonts));
    app.sources_expanded = 1;
    app_set_status(&app, "可拖入或打开多个 Excel 工作簿。");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    window = SDL_CreateWindow(
        "表表归一",
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
    SDL_SetWindowMinimumSize(window, 980, 600);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
#if SDL_VERSION_ATLEAST(2, 0, 5)
    SDL_EventState(SDL_DROPBEGIN, SDL_ENABLE);
    SDL_EventState(SDL_DROPCOMPLETE, SDL_ENABLE);
#endif
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
    bold_font_path = find_bold_font_path();
    monospace_font_path = find_monospace_font_path();
    if (font_path != NULL) {
        struct nk_font_config body_config = nk_font_config(12.0f);
        struct nk_font_config title_config = nk_font_config(20.0f);
        body_config.range = nk_font_chinese_glyph_ranges();
        body_config.oversample_h = 1;
        body_config.oversample_v = 1;
        body_config.pixel_snap = 1;
        title_config.range = title_glyph_ranges();
        title_config.oversample_h = 1;
        title_config.oversample_v = 1;
        title_config.pixel_snap = 1;
        body_font = nk_font_atlas_add_from_file(
            atlas, font_path, 12.0f, &body_config
        );
        title_font = nk_font_atlas_add_from_file(
            atlas, font_path, 20.0f, &title_config
        );
    }
    if (monospace_font_path != NULL) {
        struct nk_font_config numeric_config = nk_font_config(20.0f);
        numeric_config.range = nk_font_default_glyph_ranges();
        numeric_config.oversample_h = 2;
        numeric_config.oversample_v = 2;
        numeric_config.pixel_snap = 1;
        numeric_font = nk_font_atlas_add_from_file(
            atlas, monospace_font_path, 20.0f, &numeric_config
        );
    }
    if (bold_font_path != NULL) {
        struct nk_font_config source_value_config = nk_font_config(14.0f);
        source_value_config.range = nk_font_default_glyph_ranges();
        source_value_config.oversample_h = 2;
        source_value_config.oversample_v = 2;
        source_value_config.pixel_snap = 1;
        source_value_font = nk_font_atlas_add_from_file(
            atlas, bold_font_path, 14.0f, &source_value_config
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
    if (body_font != NULL) {
        nk_style_set_font(context, &body_font->handle);
    }
    fonts.body = body_font == NULL ? context->style.font : &body_font->handle;
    fonts.title = title_font == NULL ? fonts.body : &title_font->handle;
    fonts.numeric = numeric_font == NULL ? fonts.title : &numeric_font->handle;
    fonts.source_value = source_value_font == NULL
        ? fonts.body
        : &source_value_font->handle;
    apply_theme(context);
    screenshot_path = getenv("XLSONE_SCREENSHOT_PATH");
    screenshot_selection = getenv("XLSONE_SCREENSHOT_SELECT");

    for (index = 1; index < argc; ++index) {
        (void)app_add_path(&app, argv[index]);
    }
    if (argc > 1) {
        (void)app_recompute(&app);
        app_select_reference(&app, screenshot_selection);
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
#if SDL_VERSION_ATLEAST(2, 0, 5)
            } else if (event.type == SDL_DROPBEGIN) {
                app.drop_targeted = 1;
            } else if (event.type == SDL_DROPCOMPLETE) {
                app.drop_targeted = 0;
#endif
            } else if (event.type == SDL_MOUSEWHEEL
                && app.selected_sheet < app.merged_sheet_count) {
                if ((SDL_GetModState() & KMOD_SHIFT) != 0
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
            } else if (event.type == SDL_KEYDOWN
                && (event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
                if (event.key.keysym.sym == SDLK_o) {
                    app_open_files(&app);
                } else if (event.key.keysym.sym == SDLK_s) {
                    app_export(&app);
                } else if (event.key.keysym.sym == SDLK_n) {
                    app_clear(&app);
                }
            }
            (void)nk_sdl_handle_event(&event);
        }
        nk_sdl_handle_grab();
        nk_input_end(context);
        SDL_GetWindowSize(window, &width, &height);
        render_workspace(context, &app, &fonts, width, height);
        SDL_SetRenderDrawColor(renderer, 246, 247, 250, 255);
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
