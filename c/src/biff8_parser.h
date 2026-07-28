#ifndef XLSONE_BIFF8_PARSER_H
#define XLSONE_BIFF8_PARSER_H

#include "xlsone/xlsone.h"

int xls_parse_biff8_file(
    const char *path,
    xls_workbook *workbook,
    xls_error *error
);

#endif
