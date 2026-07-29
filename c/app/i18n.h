#ifndef XLSONE_I18N_H
#define XLSONE_I18N_H

#include <stddef.h>

typedef enum xls_ui_language {
    XLS_UI_LANGUAGE_SYSTEM = 0,
    XLS_UI_LANGUAGE_ENGLISH = 1,
    XLS_UI_LANGUAGE_ZH_HANS = 2,
    XLS_UI_LANGUAGE_ZH_HANT = 3,
    XLS_UI_LANGUAGE_JAPANESE = 4
} xls_ui_language;

int xls_i18n_parse_language(
    const char *code,
    xls_ui_language *language
);
const char *xls_i18n_language_code(xls_ui_language language);
xls_ui_language xls_i18n_resolve_language(
    xls_ui_language preference,
    const char *system_locale
);

void xls_i18n_set_language(xls_ui_language language);
xls_ui_language xls_i18n_language(void);
const char *xls_i18n_translate(const char *source);

int xls_i18n_read_preference(
    const char *path,
    xls_ui_language *language
);
int xls_i18n_write_preference(
    const char *path,
    xls_ui_language language
);

#endif
