#!/bin/sh

case "${0##*/}" in
    xdg-mime)
        if [ "$#" -ne 3 ] \
            || [ "$1" != "query" ] \
            || [ "$2" != "default" ] \
            || [ "$3" != "inode/directory" ]; then
            exit 2
        fi
        printf '%s\n' 'org.gnome.Nautilus.desktop'
        ;;
    nautilus)
        if [ -z "${XLSONE_TEST_FILE_MANAGER_LOG:-}" ]; then
            exit 2
        fi
        printf '%s\n' "$@" > "$XLSONE_TEST_FILE_MANAGER_LOG"
        ;;
    *)
        exit 2
        ;;
esac
