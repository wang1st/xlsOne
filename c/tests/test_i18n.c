#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

static void test_language_codes(void)
{
    xls_ui_language language = XLS_UI_LANGUAGE_SYSTEM;
    CHECK(xls_i18n_parse_language("ja", &language));
    CHECK(language == XLS_UI_LANGUAGE_JAPANESE);
    CHECK(xls_i18n_parse_language("zh_TW", &language));
    CHECK(language == XLS_UI_LANGUAGE_ZH_HANT);
    CHECK(xls_i18n_parse_language("zh-CN", &language));
    CHECK(language == XLS_UI_LANGUAGE_ZH_HANS);
    CHECK(!xls_i18n_parse_language("invalid", &language));
    CHECK(xls_i18n_resolve_language(
        XLS_UI_LANGUAGE_SYSTEM, "ja-JP"
    ) == XLS_UI_LANGUAGE_JAPANESE);
    CHECK(xls_i18n_resolve_language(
        XLS_UI_LANGUAGE_SYSTEM, "zh-HK"
    ) == XLS_UI_LANGUAGE_ZH_HANT);
    CHECK(xls_i18n_resolve_language(
        XLS_UI_LANGUAGE_SYSTEM, "fr-FR"
    ) == XLS_UI_LANGUAGE_ENGLISH);
}

static void test_catalog(void)
{
    xls_i18n_set_language(XLS_UI_LANGUAGE_JAPANESE);
    CHECK(strcmp(xls_i18n_translate("文件"), "ファイル") == 0);
    CHECK(strcmp(xls_i18n_translate("求和"), "合計") == 0);
    CHECK(strcmp(
        xls_i18n_translate("许可证"), "ライセンス"
    ) == 0);
    CHECK(strcmp(
        xls_i18n_translate("已接收，正在导入..."),
        "受信しました。インポートしています..."
    ) == 0);
    CHECK(strcmp(
        xls_i18n_translate("可拖入或打开多个 Excel 工作簿。"),
        "複数の Excel ブックをドラッグするか、開いてください。"
    ) == 0);
    xls_i18n_set_language(XLS_UI_LANGUAGE_ENGLISH);
    CHECK(strcmp(xls_i18n_translate("文件"), "File") == 0);
    CHECK(strcmp(
        xls_i18n_translate("文件格式不受支持"),
        "Unsupported file format"
    ) == 0);
    xls_i18n_set_language(XLS_UI_LANGUAGE_ZH_HANT);
    CHECK(strcmp(xls_i18n_translate("文件"), "檔案") == 0);
    CHECK(strcmp(
        xls_i18n_translate("正在解析工作簿，请稍候"),
        "正在解析活頁簿，請稍候"
    ) == 0);
    xls_i18n_set_language(XLS_UI_LANGUAGE_ZH_HANS);
    CHECK(strcmp(xls_i18n_translate("文件"), "文件") == 0);
}

static void test_preference_round_trip(void)
{
    const char *path = "xlsone-i18n-test.preference";
    xls_ui_language language = XLS_UI_LANGUAGE_SYSTEM;
    CHECK(xls_i18n_write_preference(path, XLS_UI_LANGUAGE_JAPANESE));
    CHECK(xls_i18n_read_preference(path, &language));
    CHECK(language == XLS_UI_LANGUAGE_JAPANESE);
    CHECK(remove(path) == 0);
}

int main(void)
{
    test_language_codes();
    test_catalog();
    test_preference_round_trip();
    if (failures != 0) {
        fprintf(stderr, "%d i18n assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all pure C i18n tests passed");
    return EXIT_SUCCESS;
}
