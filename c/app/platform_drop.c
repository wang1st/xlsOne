#include "platform_drop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#define COBJMACROS
#include <SDL_syswm.h>
#include <ole2.h>
#include <shellapi.h>
#include <windows.h>

#ifdef interface
#undef interface
#endif

typedef struct xls_windows_drop_target {
    IDropTarget interface;
    LONG reference_count;
    HWND window;
    Uint32 window_id;
    int hovering;
    int registered;
    int ole_initialized;
} xls_windows_drop_target;

static xls_windows_drop_target *windows_drop_target(
    IDropTarget *interface
)
{
    return (xls_windows_drop_target *)interface;
}

static void windows_push_drop_event(
    xls_windows_drop_target *target,
    Uint32 type,
    char *path
)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.drop.windowID = target->window_id;
    event.drop.file = path;
    if (SDL_PushEvent(&event) <= 0) {
        SDL_free(path);
    }
}

static int windows_drop_supports_files(IDataObject *object)
{
    FORMATETC format;
    memset(&format, 0, sizeof(format));
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    return object != NULL
        && SUCCEEDED(IDataObject_QueryGetData(object, &format));
}

static char *windows_drop_path_utf8(const wchar_t *path)
{
    int length;
    char *utf8;
    if (path == NULL) {
        return NULL;
    }
    length = WideCharToMultiByte(
        CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL
    );
    if (length <= 0) {
        return NULL;
    }
    utf8 = (char *)SDL_malloc((size_t)length);
    if (utf8 == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(
        CP_UTF8, 0, path, -1, utf8, length, NULL, NULL
    ) <= 0) {
        SDL_free(utf8);
        return NULL;
    }
    return utf8;
}

static HRESULT STDMETHODCALLTYPE windows_drop_query_interface(
    IDropTarget *interface,
    REFIID identifier,
    void **object
)
{
    if (object == NULL) {
        return E_POINTER;
    }
    *object = NULL;
    if (IsEqualIID(identifier, &IID_IUnknown)
        || IsEqualIID(identifier, &IID_IDropTarget)) {
        *object = interface;
        IDropTarget_AddRef(interface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE windows_drop_add_ref(
    IDropTarget *interface
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    return (ULONG)InterlockedIncrement(&target->reference_count);
}

static ULONG STDMETHODCALLTYPE windows_drop_release(
    IDropTarget *interface
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    const LONG remaining =
        InterlockedDecrement(&target->reference_count);
    if (remaining == 0) {
        free(target);
    }
    return (ULONG)remaining;
}

static HRESULT STDMETHODCALLTYPE windows_drop_enter(
    IDropTarget *interface,
    IDataObject *object,
    DWORD key_state,
    POINTL point,
    DWORD *effect
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    const int supported = windows_drop_supports_files(object);
    (void)key_state;
    (void)point;
    if (effect == NULL) {
        return E_POINTER;
    }
    if (supported && ((*effect & DROPEFFECT_COPY) != 0u)) {
        *effect = DROPEFFECT_COPY;
        if (!target->hovering) {
            target->hovering = 1;
            windows_push_drop_event(target, SDL_DROPBEGIN, NULL);
        }
    } else {
        *effect = DROPEFFECT_NONE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE windows_drop_over(
    IDropTarget *interface,
    DWORD key_state,
    POINTL point,
    DWORD *effect
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    (void)key_state;
    (void)point;
    if (effect == NULL) {
        return E_POINTER;
    }
    *effect = target->hovering
        && ((*effect & DROPEFFECT_COPY) != 0u)
        ? DROPEFFECT_COPY
        : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE windows_drop_leave(
    IDropTarget *interface
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    if (target->hovering) {
        target->hovering = 0;
        windows_push_drop_event(target, SDL_DROPCOMPLETE, NULL);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE windows_drop_files(
    IDropTarget *interface,
    IDataObject *object,
    DWORD key_state,
    POINTL point,
    DWORD *effect
)
{
    xls_windows_drop_target *target =
        windows_drop_target(interface);
    FORMATETC format;
    STGMEDIUM medium;
    HRESULT result;
    (void)key_state;
    (void)point;
    if (effect == NULL) {
        return E_POINTER;
    }
    *effect = DROPEFFECT_NONE;
    memset(&format, 0, sizeof(format));
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    memset(&medium, 0, sizeof(medium));
    result = IDataObject_GetData(object, &format, &medium);
    if (SUCCEEDED(result)) {
        const HDROP drop = (HDROP)medium.hGlobal;
        if (drop != NULL) {
            const UINT count =
                DragQueryFileW(drop, 0xffffffffu, NULL, 0u);
            UINT index;
            if (!target->hovering) {
                target->hovering = 1;
                windows_push_drop_event(
                    target, SDL_DROPBEGIN, NULL
                );
            }
            for (index = 0u; index < count; ++index) {
                const UINT length =
                    DragQueryFileW(drop, index, NULL, 0u);
                wchar_t *wide = (wchar_t *)malloc(
                    ((size_t)length + 1u) * sizeof(*wide)
                );
                if (wide != NULL
                    && DragQueryFileW(
                        drop, index, wide, length + 1u
                    ) > 0u) {
                    char *path = windows_drop_path_utf8(wide);
                    if (path != NULL) {
                        windows_push_drop_event(
                            target, SDL_DROPFILE, path
                        );
                    }
                }
                free(wide);
            }
            *effect = DROPEFFECT_COPY;
        }
        ReleaseStgMedium(&medium);
    }
    if (target->hovering) {
        target->hovering = 0;
        windows_push_drop_event(target, SDL_DROPCOMPLETE, NULL);
    }
    return S_OK;
}

static const IDropTargetVtbl windows_drop_vtable = {
    windows_drop_query_interface,
    windows_drop_add_ref,
    windows_drop_release,
    windows_drop_enter,
    windows_drop_over,
    windows_drop_leave,
    windows_drop_files
};

#endif

int xls_platform_drop_target_init(
    xls_platform_drop_target *target,
    SDL_Window *window
)
{
    if (target == NULL || window == NULL) {
        return 0;
    }
    target->implementation = NULL;
#if defined(_WIN32)
    {
        SDL_SysWMinfo information;
        xls_windows_drop_target *implementation;
        HRESULT result;
        SDL_VERSION(&information.version);
        if (!SDL_GetWindowWMInfo(window, &information)
            || information.subsystem != SDL_SYSWM_WINDOWS) {
            return 0;
        }
        implementation = (xls_windows_drop_target *)calloc(
            1u, sizeof(*implementation)
        );
        if (implementation == NULL) {
            return 0;
        }
        implementation->interface.lpVtbl = &windows_drop_vtable;
        implementation->reference_count = 1;
        implementation->window = information.info.win.window;
        implementation->window_id = SDL_GetWindowID(window);
        result = OleInitialize(NULL);
        if (result == S_OK || result == S_FALSE) {
            implementation->ole_initialized = 1;
        } else {
            free(implementation);
            return 0;
        }
        result = RegisterDragDrop(
            implementation->window,
            &implementation->interface
        );
        if (FAILED(result)) {
            OleUninitialize();
            free(implementation);
            return 0;
        }
        implementation->registered = 1;
        DragAcceptFiles(implementation->window, FALSE);
        target->implementation = implementation;
    }
#else
    (void)window;
#endif
    return 1;
}

void xls_platform_drop_target_shutdown(
    xls_platform_drop_target *target
)
{
    if (target == NULL) {
        return;
    }
#if defined(_WIN32)
    {
        xls_windows_drop_target *implementation =
            (xls_windows_drop_target *)target->implementation;
        if (implementation != NULL) {
            if (implementation->registered) {
                (void)RevokeDragDrop(implementation->window);
            }
            if (implementation->ole_initialized) {
                OleUninitialize();
            }
            IDropTarget_Release(&implementation->interface);
        }
    }
#endif
    target->implementation = NULL;
}
