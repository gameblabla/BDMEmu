#include "bdm_frontend.h"
#include "bdm_win32_audio.h"
#include "bdm_win32_sdl_input.h"
#include "bdm_win32_video.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#if !defined(BDM_WIN32_WIN31)
#include <commdlg.h>
#endif
#if defined(BDM_WIN64_FRONTEND)
#include <shellapi.h>
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC 0x01
#endif
#ifndef HID_USAGE_GENERIC_KEYBOARD
#define HID_USAGE_GENERIC_KEYBOARD 0x06
#endif

#if defined(BDM_WIN32_WIN31)
#ifndef GWLP_USERDATA
#define GWLP_USERDATA GWL_USERDATA
#endif
#ifndef GWL_STYLE
#define GWL_STYLE (-16)
#endif
#ifndef GWL_EXSTYLE
#define GWL_EXSTYLE (-20)
#endif
#ifndef WS_CLIPSIBLINGS
#define WS_CLIPSIBLINGS 0x04000000L
#endif
#ifndef WS_CLIPCHILDREN
#define WS_CLIPCHILDREN 0x02000000L
#endif
#ifndef IDC_CROSS
#define IDC_CROSS MAKEINTRESOURCE(32515)
#endif
#ifndef SS_LEFTNOWORDWRAP
#define SS_LEFTNOWORDWRAP 0x0000000CL
#endif
#ifndef GetWindowLongPtr
#define GetWindowLongPtr(hwnd, index) ((LONG_PTR)GetWindowLongA((hwnd), (index)))
#endif
#ifndef SetWindowLongPtr
#define SetWindowLongPtr(hwnd, index, value) ((LONG_PTR)SetWindowLongA((hwnd), (index), (LONG)(value)))
#endif
#endif

#define BDM_UI_PATH_MAX 4096
#define BDM_UI_RECENT_MAX 8
#define BDM_UI_STATUS_H 24

#define IDM_FILE_OPEN_CART       1001
#define IDM_FILE_OPEN_PAIR       1002
#define IDM_FILE_OPEN_MEDIA      1003
#define IDM_FILE_OPEN_BIOS       1004
#define IDM_FILE_RELOAD          1005
#define IDM_FILE_LOAD_STATE      1010
#define IDM_FILE_SAVE_STATE      1011
#define IDM_FILE_QUICK_SAVE      1012
#define IDM_FILE_QUICK_LOAD      1013
#define IDM_FILE_SAVE_FRAME      1014
#define IDM_FILE_SAVE_WAV        1015
#define IDM_FILE_EXIT            1099
#define IDM_FILE_RECENT_BASE     1200

#define IDM_EMU_PAUSE            2001
#define IDM_EMU_RESET            2002
#define IDM_EMU_AUTO_CAL         2003
#define IDM_EMU_AUTO_TITLE       2004
#define IDM_EMU_AUTO_MENU        2005
#define IDM_EMU_AUTO_MODE1       2006

#define IDM_VIDEO_GDI            3001
#define IDM_VIDEO_D3D11          3002
#define IDM_VIDEO_INTEGER        3010
#define IDM_VIDEO_ASPECT         3011
#define IDM_VIDEO_SCALE_BASE     3100
#define IDM_VIDEO_FULLSCREEN     3199

#define IDM_AUDIO_ENABLE         4001
#define IDM_AUDIO_WAVEOUT        4010
#define IDM_AUDIO_WASAPI         4011
#define IDM_AUDIO_NONE           4012

#define IDM_INPUT_OFFSET_X_DEC   5001
#define IDM_INPUT_OFFSET_X_INC   5002
#define IDM_INPUT_OFFSET_Y_DEC   5003
#define IDM_INPUT_OFFSET_Y_INC   5004
#define IDM_INPUT_HOLD_DEC       5010
#define IDM_INPUT_HOLD_INC       5011
#define IDM_INPUT_DEBUG          5012
#define IDM_INPUT_RESET_OFFSET   5013
#define IDM_INPUT_CROSSHAIR      5014
#define IDM_INPUT_VISIBLE_PANEL   5015
#define IDM_INPUT_PANEL_BASE      5100
#define IDM_INPUT_PANEL_A         (IDM_INPUT_PANEL_BASE + 0)
#define IDM_INPUT_PANEL_B         (IDM_INPUT_PANEL_BASE + 1)
#define IDM_INPUT_PANEL_C         (IDM_INPUT_PANEL_BASE + 2)
#define IDM_INPUT_PANEL_D         (IDM_INPUT_PANEL_BASE + 3)
#define IDM_INPUT_PANEL_E         (IDM_INPUT_PANEL_BASE + 4)
#define IDM_INPUT_PANEL_LEFT      (IDM_INPUT_PANEL_BASE + 5)
#define IDM_INPUT_PANEL_RIGHT     (IDM_INPUT_PANEL_BASE + 6)

#define IDM_HELP_INPUT           9001
#define IDM_HELP_ABOUT           9002

typedef struct bdm_win32_recent {
    char cart[BDM_UI_PATH_MAX];
    char media[BDM_UI_PATH_MAX];
} bdm_win32_recent_t;

typedef struct bdm_win32_app {
    HINSTANCE instance;
    HWND hwnd;
    HWND video_hwnd;
    HWND status_hwnd;
    HWND panel_buttons[7];
    HWND panel_bg[3];
    HMENU main_menu;
    HMENU recent_menu;
    HACCEL accel;

    bdm_fe_options_t opt;
    bdm_fe_machine_t machine;
    bdm_fe_touch_state_t touch;
    uint8_t joy_panel_down[BDM_BUTTON_COUNT];
    uint8_t click_panel_latch[BDM_BUTTON_COUNT];
    unsigned click_panel_latch_frames[BDM_BUTTON_COUNT];
    int machine_loaded;
    int machine_failed;

    bdm_win32_video_t *video_backend;
    bdm_win32_audio_t *audio_backend;
    bdm_win32_sdl_input_t *sdl_input;

    int quit_requested;
    int paused;
    int runtime_fullscreen;
    RECT windowed_rect;
    DWORD windowed_style;
    DWORD windowed_exstyle;

    uint64_t frame_counter;
    uint64_t fps_window_ms;
    uint64_t fps_window_frames;
    unsigned measured_fps;

    char cart_path[BDM_UI_PATH_MAX];
    char media_path[BDM_UI_PATH_MAX];
    char bios_path[BDM_UI_PATH_MAX];
    char load_sram_path[BDM_UI_PATH_MAX];
    char save_sram_path[BDM_UI_PATH_MAX];
    char load_state_path[BDM_UI_PATH_MAX];
    char save_state_path[BDM_UI_PATH_MAX];
    char state_slot_path[BDM_UI_PATH_MAX];
    char dump_wav_path[BDM_UI_PATH_MAX];
    char video_backend_name[32];
    char audio_backend_name[32];
    char recent_store_path[BDM_UI_PATH_MAX];
    bdm_win32_recent_t recent[BDM_UI_RECENT_MAX];
    int recent_count;
    int touch_crosshair_cursor;
    int visible_panel_buttons;
    int timer_period_set;
} bdm_win32_app_t;

static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK video_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

#define BDM_WIN32_APP_ICON_ID 1

static HICON app_load_icon(HINSTANCE instance, int small) {
    HICON icon = LoadIconA(instance, MAKEINTRESOURCEA(BDM_WIN32_APP_ICON_ID));
#if !defined(BDM_WIN32_WIN31)
    if (!icon) {
        icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(BDM_WIN32_APP_ICON_ID), IMAGE_ICON,
                                 small ? GetSystemMetrics(SM_CXSMICON) : GetSystemMetrics(SM_CXICON),
                                 small ? GetSystemMetrics(SM_CYSMICON) : GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    }
#else
    (void)small;
#endif
    if (!icon) icon = LoadIcon(NULL, IDI_APPLICATION);
    return icon;
}

static uint64_t ticks_ms(void) {
#if defined(BDM_WIN64_FRONTEND)
    return GetTickCount64();
#else
    return (uint64_t)timeGetTime();
#endif
}

static uint64_t qpc_now_ns(void) {
#if defined(BDM_WIN64_FRONTEND)
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000000000ull) / freq.QuadPart);
#else
    return (uint64_t)timeGetTime() * 1000000ull;
#endif
}

static void sleep_until_ns(uint64_t target_ns) {
    for (;;) {
        uint64_t now = qpc_now_ns();
        if (now >= target_ns) break;
        uint64_t remain = target_ns - now;
        if (remain > 3000000ull) {
            DWORD ms = (DWORD)((remain - 1000000ull) / 1000000ull);
            Sleep(ms ? ms : 1u);
        } else if (remain > 500000ull) {
            Sleep(0);
        } else {
            /* Sub-millisecond tail: keep this short to avoid visible judder without burning a full frame. */
        }
    }
}

static int str_eq_i(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static const char *path_base(const char *p) {
    const char *b1, *b2;
    if (!p || !*p) return "(none)";
    b1 = strrchr(p, '\\');
    b2 = strrchr(p, '/');
    if (b1 && b2) return (b1 > b2 ? b1 : b2) + 1;
    if (b1) return b1 + 1;
    if (b2) return b2 + 1;
    return p;
}

static void copy_path(char *dst, size_t cap, const char *src) {
    if (!dst || !cap) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

static const char *nullable_path(const char *p) { return p && *p ? p : NULL; }


#if !defined(BDM_WIN32_WIN31)
static DWORD bdm_openfilename_struct_size(void) {
#if defined(BDM_WIN32_WIN95) && defined(OPENFILENAME_SIZE_VERSION_400)
    return OPENFILENAME_SIZE_VERSION_400;
#else
    return (DWORD)sizeof(OPENFILENAMEA);
#endif
}
#endif

static void app_sync_options(bdm_win32_app_t *app) {
    if (!app) return;
    app->opt.cart_path = nullable_path(app->cart_path);
    app->opt.media_path = nullable_path(app->media_path);
    app->opt.bios_path = nullable_path(app->bios_path);
    app->opt.load_sram_path = nullable_path(app->load_sram_path);
    app->opt.save_sram_path = nullable_path(app->save_sram_path);
    app->opt.load_state_path = nullable_path(app->load_state_path);
    app->opt.save_state_path = nullable_path(app->save_state_path);
    app->opt.state_slot_path = nullable_path(app->state_slot_path);
    app->opt.dump_wav_path = nullable_path(app->dump_wav_path);
    app->opt.video_backend = nullable_path(app->video_backend_name);
    app->opt.audio_backend = nullable_path(app->audio_backend_name);
}

static void app_capture_option_paths(bdm_win32_app_t *app, const bdm_fe_options_t *src) {
    if (!app || !src) return;
    copy_path(app->cart_path, sizeof(app->cart_path), src->cart_path);
    copy_path(app->media_path, sizeof(app->media_path), src->media_path);
    copy_path(app->bios_path, sizeof(app->bios_path), src->bios_path);
    copy_path(app->load_sram_path, sizeof(app->load_sram_path), src->load_sram_path);
    copy_path(app->save_sram_path, sizeof(app->save_sram_path), src->save_sram_path);
    copy_path(app->load_state_path, sizeof(app->load_state_path), src->load_state_path);
    copy_path(app->save_state_path, sizeof(app->save_state_path), src->save_state_path);
    copy_path(app->state_slot_path, sizeof(app->state_slot_path), src->state_slot_path ? src->state_slot_path : "bdm_state.bdmst");
    copy_path(app->dump_wav_path, sizeof(app->dump_wav_path), src->dump_wav_path);
    copy_path(app->video_backend_name, sizeof(app->video_backend_name), src->video_backend ? src->video_backend :
#if defined(BDM_WIN64_FRONTEND)
              "d3d11"
#else
              "gdi"
#endif
              );
    copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), src->audio_backend ? src->audio_backend :
#if defined(BDM_WIN64_FRONTEND)
              "wasapi"
#else
              "waveout"
#endif
              );
    app->opt = *src;
    app_sync_options(app);
}

static void app_message(HWND hwnd, UINT icon, const char *title, const char *text) {
    MessageBoxA(hwnd, text ? text : "", title ? title : "Bandai Design Master", MB_OK | icon);
}

static void app_error(bdm_win32_app_t *app, const char *text) {
    app_message(app ? app->hwnd : NULL, MB_ICONERROR, "Bandai Design Master", text);
}

#if defined(BDM_WIN32_WIN31)
#define BDM_PATH_PROMPT_EDIT   6101
#define BDM_PATH_PROMPT_OK     6102
#define BDM_PATH_PROMPT_CANCEL 6103

typedef struct bdm_path_prompt {
    HWND owner;
    HWND hwnd;
    HWND edit;
    const char *title;
    const char *label;
    char *out;
    size_t out_cap;
    int done;
    int ok;
} bdm_path_prompt_t;

static LRESULT CALLBACK path_prompt_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bdm_path_prompt_t *p = (bdm_path_prompt_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    case WM_CREATE: {
        p = (bdm_path_prompt_t *)((CREATESTRUCT *)lp)->lpCreateParams;
        CreateWindowA("STATIC", p && p->label ? p->label : "Path:", WS_CHILD | WS_VISIBLE,
                      8, 10, 456, 18, hwnd, NULL, GetModuleHandle(NULL), NULL);
        p->edit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                8, 32, 456, 24, hwnd, (HMENU)(UINT_PTR)BDM_PATH_PROMPT_EDIT, GetModuleHandle(NULL), NULL);
        CreateWindowA("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      296, 68, 80, 24, hwnd, (HMENU)(UINT_PTR)BDM_PATH_PROMPT_OK, GetModuleHandle(NULL), NULL);
        CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      384, 68, 80, 24, hwnd, (HMENU)(UINT_PTR)BDM_PATH_PROMPT_CANCEL, GetModuleHandle(NULL), NULL);
        SetFocus(p->edit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == BDM_PATH_PROMPT_OK) {
            if (p && p->out && p->out_cap) {
                GetWindowTextA(p->edit, p->out, (int)p->out_cap);
                p->ok = p->out[0] != 0;
            }
            if (p) p->done = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == BDM_PATH_PROMPT_CANCEL) {
            if (p) { p->ok = 0; p->done = 1; }
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (p) { p->ok = 0; p->done = 1; }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (p) p->done = 1;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int prompt_path(HWND owner, const char *title, const char *label, char *out, size_t out_cap) {
    static int class_registered = 0;
    bdm_path_prompt_t prompt;
    WNDCLASSA wc;
    RECT pr = {0, 0, 480, 124};
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    MSG msg;
    if (!out || !out_cap) return 0;
    out[0] = 0;
    if (!class_registered) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = path_prompt_wndproc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "BDMWin32PathPrompt";
        if (!RegisterClassA(&wc)) return 0;
        class_registered = 1;
    }
    AdjustWindowRect(&pr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    if (owner) {
        RECT orc;
        GetWindowRect(owner, &orc);
        x = orc.left + ((orc.right - orc.left) - (pr.right - pr.left)) / 2;
        y = orc.top + ((orc.bottom - orc.top) - (pr.bottom - pr.top)) / 2;
    }
    memset(&prompt, 0, sizeof(prompt));
    prompt.owner = owner;
    prompt.title = title;
    prompt.label = label;
    prompt.out = out;
    prompt.out_cap = out_cap;
    prompt.hwnd = CreateWindowA("BDMWin32PathPrompt", title ? title : "Enter path",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                x, y, pr.right - pr.left, pr.bottom - pr.top,
                                owner, NULL, GetModuleHandle(NULL), &prompt);
    if (!prompt.hwnd) return 0;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(prompt.hwnd, SW_SHOW);
    UpdateWindow(prompt.hwnd);
    while (!prompt.done && GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(prompt.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    return prompt.ok;
}

static int browse_open_file(HWND hwnd, const char *title, const char *filter, char *out, size_t out_cap) {
    (void)filter;
    return prompt_path(hwnd, title ? title : "Open file", "Enter full file path:", out, out_cap);
}

static int browse_save_file(HWND hwnd, const char *title, const char *filter, const char *def_ext, char *out, size_t out_cap) {
    char label[128];
    (void)filter;
    if (def_ext && *def_ext) snprintf(label, sizeof(label), "Enter output path (*.%s):", def_ext);
    else copy_path(label, sizeof(label), "Enter output path:");
    return prompt_path(hwnd, title ? title : "Save file", label, out, out_cap);
}
#else
static int browse_open_file(HWND hwnd, const char *title, const char *filter, char *out, size_t out_cap) {
    OPENFILENAMEA ofn;
    char path[BDM_UI_PATH_MAX];
    memset(&ofn, 0, sizeof(ofn));
    memset(path, 0, sizeof(path));
    ofn.lStructSize = bdm_openfilename_struct_size();
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter ? filter : "All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return 0;
    copy_path(out, out_cap, path);
    return 1;
}

static int browse_save_file(HWND hwnd, const char *title, const char *filter, const char *def_ext, char *out, size_t out_cap) {
    OPENFILENAMEA ofn;
    char path[BDM_UI_PATH_MAX];
    memset(&ofn, 0, sizeof(ofn));
    memset(path, 0, sizeof(path));
    ofn.lStructSize = bdm_openfilename_struct_size();
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter ? filter : "All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = def_ext;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return 0;
    copy_path(out, out_cap, path);
    return 1;
}
#endif

static void app_set_recent_store_path(bdm_win32_app_t *app) {
    if (!app) return;
#if defined(BDM_WIN64_FRONTEND)
    char base[BDM_UI_PATH_MAX];
    DWORD n = GetEnvironmentVariableA("APPDATA", base, sizeof(base));
    if (n > 0 && n < sizeof(base)) {
        char dir[BDM_UI_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s\\BDMEmu", base);
        CreateDirectoryA(dir, NULL);
        snprintf(app->recent_store_path, sizeof(app->recent_store_path), "%s\\recent.txt", dir);
    } else {
        copy_path(app->recent_store_path, sizeof(app->recent_store_path), "bdm_recent.txt");
    }
#else
    /* Win32/Win95 profile: no Shell/AppData dependency. Keep recents local. */
    copy_path(app->recent_store_path, sizeof(app->recent_store_path), "bdm_recent.txt");
#endif
}


static void app_recent_load(bdm_win32_app_t *app) {
    FILE *f;
    char line[BDM_UI_PATH_MAX * 2];
    if (!app || !*app->recent_store_path) return;
    f = fopen(app->recent_store_path, "r");
    if (!f) return;
    while (app->recent_count < BDM_UI_RECENT_MAX && fgets(line, sizeof(line), f)) {
        char *tab, *nl;
        nl = strchr(line, '\n'); if (nl) *nl = 0;
        tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            copy_path(app->recent[app->recent_count].cart, sizeof(app->recent[0].cart), line);
            copy_path(app->recent[app->recent_count].media, sizeof(app->recent[0].media), tab + 1);
        } else {
            copy_path(app->recent[app->recent_count].cart, sizeof(app->recent[0].cart), line);
            app->recent[app->recent_count].media[0] = 0;
        }
        if (app->recent[app->recent_count].cart[0]) ++app->recent_count;
    }
    fclose(f);
}

static void app_recent_save(bdm_win32_app_t *app) {
    FILE *f;
    if (!app || !*app->recent_store_path) return;
    f = fopen(app->recent_store_path, "w");
    if (!f) return;
    for (int i = 0; i < app->recent_count; ++i) fprintf(f, "%s\t%s\n", app->recent[i].cart, app->recent[i].media);
    fclose(f);
}

static void rebuild_recent_menu(bdm_win32_app_t *app) {
    if (!app || !app->recent_menu) return;
    while (GetMenuItemCount(app->recent_menu) > 0) DeleteMenu(app->recent_menu, 0, MF_BYPOSITION);
    if (app->recent_count == 0) {
        AppendMenuA(app->recent_menu, MF_GRAYED | MF_STRING, IDM_FILE_RECENT_BASE, "(empty)");
        return;
    }
    for (int i = 0; i < app->recent_count; ++i) {
        char item[512];
        if (app->recent[i].media[0]) snprintf(item, sizeof(item), "&%d  %s + %s", i + 1, path_base(app->recent[i].cart), path_base(app->recent[i].media));
        else snprintf(item, sizeof(item), "&%d  %s", i + 1, path_base(app->recent[i].cart));
        AppendMenuA(app->recent_menu, MF_STRING, IDM_FILE_RECENT_BASE + (UINT)i, item);
    }
}

static void app_recent_add(bdm_win32_app_t *app, const char *cart, const char *media) {
    if (!app || !cart || !*cart) return;
    for (int i = 0; i < app->recent_count; ++i) {
        if (str_eq_i(app->recent[i].cart, cart) && str_eq_i(app->recent[i].media, media ? media : "")) {
            bdm_win32_recent_t r = app->recent[i];
            memmove(&app->recent[1], &app->recent[0], (size_t)i * sizeof(app->recent[0]));
            app->recent[0] = r;
            app_recent_save(app);
            rebuild_recent_menu(app);
            return;
        }
    }
    if (app->recent_count < BDM_UI_RECENT_MAX) ++app->recent_count;
    if (app->recent_count > 1) memmove(&app->recent[1], &app->recent[0], (size_t)(app->recent_count - 1) * sizeof(app->recent[0]));
    copy_path(app->recent[0].cart, sizeof(app->recent[0].cart), cart);
    copy_path(app->recent[0].media, sizeof(app->recent[0].media), media);
    app_recent_save(app);
    rebuild_recent_menu(app);
}

static void app_update_menu_state(bdm_win32_app_t *app) {
    HMENU m;
    if (!app || !app->main_menu) return;
    m = app->main_menu;
    EnableMenuItem(m, IDM_FILE_RELOAD, MF_BYCOMMAND | (app->cart_path[0] || app->bios_path[0] ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_OPEN_MEDIA, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_LOAD_STATE, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_SAVE_STATE, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_QUICK_SAVE, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_QUICK_LOAD, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_SAVE_FRAME, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_FILE_SAVE_WAV, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_EMU_PAUSE, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_EMU_RESET, MF_BYCOMMAND | (app->machine_loaded ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(m, IDM_EMU_PAUSE, MF_BYCOMMAND | (app->paused ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_EMU_AUTO_CAL, MF_BYCOMMAND | (app->opt.auto_calibrate ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_EMU_AUTO_TITLE, MF_BYCOMMAND | (app->opt.auto_title ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_EMU_AUTO_MENU, MF_BYCOMMAND | (app->opt.auto_menu ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_EMU_AUTO_MODE1, MF_BYCOMMAND | (app->opt.auto_mode1 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_VIDEO_GDI, MF_BYCOMMAND | (str_eq_i(app->video_backend_name, "gdi") ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_VIDEO_D3D11, MF_BYCOMMAND | (str_eq_i(app->video_backend_name, "d3d11") ? MF_CHECKED : MF_UNCHECKED));
#if defined(BDM_WIN64_FRONTEND)
    EnableMenuItem(m, IDM_VIDEO_D3D11, MF_BYCOMMAND | MF_ENABLED);
#else
    EnableMenuItem(m, IDM_VIDEO_D3D11, MF_BYCOMMAND | MF_GRAYED);
#endif
    CheckMenuItem(m, IDM_VIDEO_INTEGER, MF_BYCOMMAND | (app->opt.integer_scaling ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_VIDEO_ASPECT, MF_BYCOMMAND | (!app->opt.integer_scaling ? MF_CHECKED : MF_UNCHECKED));
    for (unsigned i = 1; i <= 6; ++i) CheckMenuItem(m, IDM_VIDEO_SCALE_BASE + i, MF_BYCOMMAND | (app->opt.scale == i ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_VIDEO_FULLSCREEN, MF_BYCOMMAND | (app->runtime_fullscreen ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_AUDIO_ENABLE, MF_BYCOMMAND | (app->opt.enable_audio ? MF_CHECKED : MF_UNCHECKED));
#if defined(BDM_WIN64_FRONTEND)
    CheckMenuItem(m, IDM_AUDIO_WAVEOUT, MF_BYCOMMAND | (str_eq_i(app->audio_backend_name, "waveout") ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_AUDIO_WASAPI, MF_BYCOMMAND | (str_eq_i(app->audio_backend_name, "wasapi") ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_AUDIO_NONE, MF_BYCOMMAND | (str_eq_i(app->audio_backend_name, "none") || !app->opt.enable_audio ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(m, IDM_AUDIO_WASAPI, MF_BYCOMMAND | MF_ENABLED);
#endif
    CheckMenuItem(m, IDM_INPUT_CROSSHAIR, MF_BYCOMMAND | (app->touch_crosshair_cursor ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_INPUT_VISIBLE_PANEL, MF_BYCOMMAND | (app->visible_panel_buttons ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_INPUT_DEBUG, MF_BYCOMMAND | (app->opt.touch_debug ? MF_CHECKED : MF_UNCHECKED));
    DrawMenuBar(app->hwnd);
}

static HMENU app_create_menu(bdm_win32_app_t *app) {
    HMENU main_menu = CreateMenu();
    HMENU file_menu = CreatePopupMenu();
    HMENU emu_menu = CreatePopupMenu();
    HMENU video_menu = CreatePopupMenu();
    HMENU audio_menu = CreatePopupMenu();
    HMENU input_menu = CreatePopupMenu();
    HMENU help_menu = CreatePopupMenu();
    HMENU scale_menu = CreatePopupMenu();

    app->recent_menu = CreatePopupMenu();
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_OPEN_CART, "&Open Program ROM...\tCtrl+O");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_OPEN_PAIR, "Open Program + &Media ROM...\tCtrl+Shift+O");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_OPEN_MEDIA, "Replace &Media ROM...");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_OPEN_BIOS, "Open optional &BIOS ROM...");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_RELOAD, "&Reload ROMs");
    AppendMenuA(file_menu, MF_POPUP, (UINT_PTR)app->recent_menu, "Open &Recent");
    AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_LOAD_STATE, "&Load State...\tCtrl+L");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_SAVE_STATE, "&Save State...\tCtrl+S");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_QUICK_SAVE, "Quick Save\tF5");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_QUICK_LOAD, "Quick Load\tF8");
    AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_SAVE_FRAME, "Save LCD Frame as &PPM...");
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_SAVE_WAV, "Save Captured &WAV...");
    AppendMenuA(file_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file_menu, MF_STRING, IDM_FILE_EXIT, "E&xit\tAlt+F4");

    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_PAUSE, "&Pause\tP");
    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_RESET, "&Reset\tCtrl+R");
    AppendMenuA(emu_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_AUTO_CAL, "Auto-&calibrate on load/reset	F10");
    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_AUTO_TITLE, "Run to &title on load/reset");
    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_AUTO_MENU, "Run to &menu on load/reset");
    AppendMenuA(emu_menu, MF_STRING, IDM_EMU_AUTO_MODE1, "Run to mode &1 on load/reset");

    AppendMenuA(video_menu, MF_STRING, IDM_VIDEO_GDI, "&GDI renderer");
#if defined(BDM_WIN64_FRONTEND)
    AppendMenuA(video_menu, MF_STRING, IDM_VIDEO_D3D11, "&D3D11 shader renderer");
#endif
    AppendMenuA(video_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(video_menu, MF_STRING, IDM_VIDEO_INTEGER, "&Integer scale");
    AppendMenuA(video_menu, MF_STRING, IDM_VIDEO_ASPECT, "&Aspect fit");
    AppendMenuA(video_menu, MF_SEPARATOR, 0, NULL);
    for (unsigned i = 1; i <= 6; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "%ux", i);
        AppendMenuA(scale_menu, MF_STRING, IDM_VIDEO_SCALE_BASE + i, label);
    }
    AppendMenuA(video_menu, MF_POPUP, (UINT_PTR)scale_menu, "Window &scale");
    AppendMenuA(video_menu, MF_STRING, IDM_VIDEO_FULLSCREEN, "&Fullscreen\tF11");

    AppendMenuA(audio_menu, MF_STRING, IDM_AUDIO_ENABLE, "&Enable audio");
#if defined(BDM_WIN64_FRONTEND)
    AppendMenuA(audio_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(audio_menu, MF_STRING, IDM_AUDIO_WAVEOUT, "&waveOut");
    AppendMenuA(audio_menu, MF_STRING, IDM_AUDIO_WASAPI, "&WASAPI");
    AppendMenuA(audio_menu, MF_STRING, IDM_AUDIO_NONE, "&None");
#endif

    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_RESET_OFFSET, "&Reset touch offset\tCtrl+0");
    AppendMenuA(input_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_OFFSET_X_DEC, "Touch offset X -1");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_OFFSET_X_INC, "Touch offset X +1");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_OFFSET_Y_DEC, "Touch offset Y -1");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_OFFSET_Y_INC, "Touch offset Y +1");
    AppendMenuA(input_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_HOLD_DEC, "Minimum touch hold -5 ms");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_HOLD_INC, "Minimum touch hold +5 ms");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_DEBUG, "Touch debug logging");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_CROSSHAIR, "Use touch &crosshair cursor");
    AppendMenuA(input_menu, MF_STRING, IDM_INPUT_VISIBLE_PANEL, "Show hardware &panel buttons");

    AppendMenuA(help_menu, MF_STRING, IDM_HELP_INPUT, "&Input Mapping");
    AppendMenuA(help_menu, MF_STRING, IDM_HELP_ABOUT, "&About");

    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)file_menu, "&File");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)emu_menu, "&Emulation");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)video_menu, "&Video");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)audio_menu, "&Audio");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)input_menu, "&Input");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)help_menu, "&Help");
    rebuild_recent_menu(app);
    return main_menu;
}

static HACCEL app_create_accel(void) {
    ACCEL acc[] = {
        { FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN_CART },
        { FVIRTKEY | FCONTROL | FSHIFT, 'O', IDM_FILE_OPEN_PAIR },
        { FVIRTKEY | FCONTROL, 'L', IDM_FILE_LOAD_STATE },
        { FVIRTKEY | FCONTROL, 'S', IDM_FILE_SAVE_STATE },
        { FVIRTKEY, VK_F5, IDM_FILE_QUICK_SAVE },
        { FVIRTKEY, VK_F8, IDM_FILE_QUICK_LOAD },
        { FVIRTKEY | FCONTROL, 'R', IDM_EMU_RESET },
        { FVIRTKEY, VK_F10, IDM_EMU_AUTO_CAL },
        { FVIRTKEY | FCONTROL, '0', IDM_INPUT_RESET_OFFSET },
        { FVIRTKEY, VK_F9, IDM_VIDEO_INTEGER },
        { FVIRTKEY, VK_F11, IDM_VIDEO_FULLSCREEN },
        { FVIRTKEY, 'P', IDM_EMU_PAUSE }
    };
    return CreateAcceleratorTableA(acc, (int)(sizeof(acc) / sizeof(acc[0])));
}

static void app_redraw_shell(bdm_win32_app_t *app) {
    if (!app || !app->hwnd) return;
    /*
     * The LCD child window is moved when the optional hardware-panel buttons are
     * shown/hidden.  Without forcing the parent frame through a real paint pass,
     * the newly exposed margin can retain stale LCD pixels until the next user
     * resize.  Redraw the shell and children once after every layout-affecting
     * toggle so the gap behind A-E/Page controls is cleared immediately.
     */
    RedrawWindow(app->hwnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_ALLCHILDREN | RDW_UPDATENOW);
    for (int i = 0; i < 3; ++i) {
        if (app->panel_bg[i]) RedrawWindow(app->panel_bg[i], NULL, NULL,
                                           RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW);
    }
    for (int i = 0; i < 7; ++i) {
        if (app->panel_buttons[i]) RedrawWindow(app->panel_buttons[i], NULL, NULL,
                                               RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW);
    }
    if (app->video_hwnd) InvalidateRect(app->video_hwnd, NULL, FALSE);
}

static void app_layout_children(bdm_win32_app_t *app) {
    RECT rc;
    int status_h = BDM_UI_STATUS_H;
    int panel_h = app && app->visible_panel_buttons ? 30 : 0;
    int side_w = app && app->visible_panel_buttons ? 42 : 0;
    int content_h, content_w, video_y;
    if (!app || !app->hwnd) return;
    GetClientRect(app->hwnd, &rc);
    content_w = rc.right - rc.left;
    content_h = rc.bottom - rc.top;
    if (content_h < status_h) status_h = 0;
    if (content_h - status_h < panel_h) panel_h = 0;
    if (content_w < side_w * 2 + 40) side_w = 0;
    video_y = panel_h;

    if (app->status_hwnd) MoveWindow(app->status_hwnd, 0, content_h - status_h, content_w, status_h, TRUE);

    for (int i = 0; i < 3; ++i) {
        if (app->panel_bg[i]) ShowWindow(app->panel_bg[i], app->visible_panel_buttons ? SW_SHOWNA : SW_HIDE);
    }
    for (int i = 0; i < 7; ++i) {
        if (app->panel_buttons[i]) ShowWindow(app->panel_buttons[i], app->visible_panel_buttons ? SW_SHOWNA : SW_HIDE);
    }
    if (app->visible_panel_buttons) {
        int bw = content_w / 9;
        if (bw < 34) bw = 34;
        if (bw > 72) bw = 72;
        int gap = 4;
        int total = 5 * bw + 4 * gap;
        int x = (content_w - total) / 2;
        int side_h = content_h - status_h - video_y - 4;
        if (x < 0) x = 0;
        if (side_h < 1) side_h = 1;
        if (app->panel_bg[0]) MoveWindow(app->panel_bg[0], 0, 0, content_w, panel_h, TRUE);
        if (app->panel_bg[1]) MoveWindow(app->panel_bg[1], 0, video_y, side_w, side_h + 4, TRUE);
        if (app->panel_bg[2]) MoveWindow(app->panel_bg[2], content_w - side_w, video_y, side_w, side_h + 4, TRUE);
        for (int i = 0; i < 5; ++i) MoveWindow(app->panel_buttons[i], x + i * (bw + gap), 2, bw, panel_h - 4, TRUE);
        MoveWindow(app->panel_buttons[5], 2, video_y + 2, side_w - 4, side_h, TRUE);
        MoveWindow(app->panel_buttons[6], content_w - side_w + 2, video_y + 2, side_w - 4, side_h, TRUE);
        for (int i = 0; i < 3; ++i) {
            if (app->panel_bg[i]) RedrawWindow(app->panel_bg[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW);
        }
    }

    if (app->video_hwnd) MoveWindow(app->video_hwnd, side_w, video_y, content_w - side_w * 2, content_h - status_h - video_y, TRUE);
}
static void app_update_title(bdm_win32_app_t *app) {
    char title[512];
    if (!app || !app->hwnd) return;
    if (app->machine_loaded) {
        snprintf(title, sizeof(title), "Bandai Design Master - %s%s", path_base(app->cart_path), app->paused ? " [Paused]" : "");
    } else {
        snprintf(title, sizeof(title), "Bandai Design Master%s", app->paused ? " [Paused]" : "");
    }
    SetWindowTextA(app->hwnd, title);
}

static void app_update_status(bdm_win32_app_t *app) {
    char status[1024];
    const char *run_state;
    if (!app || !app->status_hwnd) return;
    run_state = app->machine_loaded ? (app->paused ? "Paused" : "Running") : "No ROM loaded";
    snprintf(status, sizeof(status),
             " %s | ROM: %s%s%s | Video: %s | Audio: %s%s | Scale: %s | Touch: off(%d,%d) hold %ums calib %ums | FPS: %u",
             run_state,
             app->machine_loaded ? path_base(app->cart_path) : "-",
             app->machine_loaded && app->media_path[0] ? " + " : "",
             app->machine_loaded && app->media_path[0] ? path_base(app->media_path) : "",
             app->video_backend ? bdm_win32_video_active_backend(app->video_backend) : "none",
             app->audio_backend ? bdm_win32_audio_active_backend(app->audio_backend) : "none",
#if defined(BDM_WIN64_FRONTEND)
             app->sdl_input ? " / SDL3 input" : "",
#else
             " / WinMM input",
#endif
             app->opt.integer_scaling ? "integer" : "aspect",
             app->opt.touch_offset_x, app->opt.touch_offset_y, app->opt.touch_hold_ms, app->opt.calibration_touch_hold_ms,
             app->measured_fps);
    SetWindowTextA(app->status_hwnd, status);
    app_update_title(app);
    app_update_menu_state(app);
}

static void client_to_pen_fp(bdm_win32_app_t *app, int wx, int wy, int32_t *px, int32_t *py) {
    if (!app || !app->video_backend || !app->machine_loaded) {
        if (px) *px = 0;
        if (py) *py = 0;
        return;
    }
    bdm_win32_video_window_to_pen_fp(app->video_backend, app->machine.video, wx, wy,
                                     app->opt.touch_offset_x, app->opt.touch_offset_y, px, py);
}

static uint64_t app_touch_hold_steps(const bdm_win32_app_t *app, unsigned ms) {
    return bdm_fe_ms_to_steps(app ? app->opt.steps_per_second : BDM_FE_DEFAULT_STEPS_PER_SECOND, ms);
}

static void app_prepare_touch_down(bdm_win32_app_t *app) {
    if (!app) return;
    bdm_fe_touch_prepare_down_for_video(&app->touch,
                                        app->machine.video,
                                        app_touch_hold_steps(app, app->opt.touch_hold_ms),
                                        app_touch_hold_steps(app, app->opt.calibration_touch_hold_ms));
}

static void app_recreate_video(bdm_win32_app_t *app) {
    RECT rc;
    if (!app || !app->video_hwnd) return;
    bdm_win32_video_destroy(app->video_backend);
    app->video_backend = bdm_win32_video_create(app->video_hwnd, app->opt.scale, app->opt.integer_scaling,
                                                app->video_backend_name[0] ? app->video_backend_name : "gdi");
    GetClientRect(app->video_hwnd, &rc);
    if (app->video_backend) bdm_win32_video_resize(app->video_backend, (unsigned)(rc.right - rc.left), (unsigned)(rc.bottom - rc.top));
    InvalidateRect(app->video_hwnd, NULL, FALSE);
    app_update_status(app);
}

static void app_recreate_audio(bdm_win32_app_t *app) {
    if (!app) return;
    bdm_win32_audio_destroy(app->audio_backend);
    app->audio_backend = NULL;
    if (app->machine_loaded && app->opt.enable_audio) {
        app_sync_options(app);
        app->audio_backend = bdm_win32_audio_create(app->machine.sound, app->opt.sample_rate,
                                                    app->opt.audio_backend ? app->opt.audio_backend : app->audio_backend_name,
                                                    1);
    }
    app_update_status(app);
}

static void app_stop_machine(bdm_win32_app_t *app) {
    if (!app) return;
    bdm_win32_audio_destroy(app->audio_backend);
    app->audio_backend = NULL;
    if (app->machine_loaded || app->machine.core || app->machine.video || app->machine.input || app->machine.sound) {
        bdm_fe_machine_destroy(&app->machine);
    }
    memset(&app->touch, 0, sizeof(app->touch));
    memset(app->joy_panel_down, 0, sizeof(app->joy_panel_down));
    memset(app->click_panel_latch, 0, sizeof(app->click_panel_latch));
    memset(app->click_panel_latch_frames, 0, sizeof(app->click_panel_latch_frames));
    app->machine_loaded = 0;
    app->paused = 0;
    app_update_status(app);
}

static int app_start_machine(bdm_win32_app_t *app) {
    bdm_fe_options_t load_opt;
    if (!app) return 0;
    if (!app->cart_path[0] && !app->bios_path[0]) {
        app_error(app, "Open a program ROM first.");
        return 0;
    }
    app_stop_machine(app);
    load_opt = app->opt;
    app_sync_options(app);
    load_opt = app->opt;
    if (bdm_fe_machine_init(&app->machine, &load_opt) != 0) {
        bdm_fe_machine_destroy(&app->machine);
        app->machine_loaded = 0;
        app->machine_failed = 1;
        app_error(app, "Failed to initialize the machine. Check the ROM/media/state paths.");
        app_update_status(app);
        return 0;
    }
    app->machine_loaded = 1;
    app->machine_failed = 0;
    memset(&app->touch, 0, sizeof(app->touch));
    memset(app->joy_panel_down, 0, sizeof(app->joy_panel_down));
    memset(app->click_panel_latch, 0, sizeof(app->click_panel_latch));
    memset(app->click_panel_latch_frames, 0, sizeof(app->click_panel_latch_frames));
    app->touch.min_hold_steps = app_touch_hold_steps(app, app->opt.touch_hold_ms);
    app->touch.debug = app->opt.touch_debug;
    if (app->opt.enable_audio) app->audio_backend = bdm_win32_audio_create(app->machine.sound, app->opt.sample_rate,
                                                                            app->opt.audio_backend ? app->opt.audio_backend : app->audio_backend_name,
                                                                            1);
    app_recent_add(app, app->cart_path, app->media_path);
    app_update_status(app);
    return 1;
}

static void app_reload_machine(bdm_win32_app_t *app) {
    if (!app) return;
    (void)app_start_machine(app);
}

static void toggle_fullscreen(bdm_win32_app_t *app) {
    if (!app || !app->hwnd) return;
    app->runtime_fullscreen = !app->runtime_fullscreen;
    if (app->runtime_fullscreen) {
        app->windowed_style = (DWORD)GetWindowLongPtr(app->hwnd, GWL_STYLE);
        app->windowed_exstyle = (DWORD)GetWindowLongPtr(app->hwnd, GWL_EXSTYLE);
        GetWindowRect(app->hwnd, &app->windowed_rect);
        SetMenu(app->hwnd, NULL);
#if !defined(BDM_WIN64_FRONTEND)
        SetWindowLongPtr(app->hwnd, GWL_STYLE, (LONG_PTR)(app->windowed_style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(app->hwnd, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
#else
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(app->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtr(app->hwnd, GWL_STYLE, (LONG_PTR)(app->windowed_style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(app->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
#endif
        if (app->status_hwnd) ShowWindow(app->status_hwnd, SW_HIDE);
    } else {
        SetMenu(app->hwnd, app->main_menu);
        if (app->status_hwnd) ShowWindow(app->status_hwnd, SW_SHOW);
        SetWindowLongPtr(app->hwnd, GWL_STYLE, (LONG_PTR)app->windowed_style);
        SetWindowLongPtr(app->hwnd, GWL_EXSTYLE, (LONG_PTR)app->windowed_exstyle);
        SetWindowPos(app->hwnd, NULL, app->windowed_rect.left, app->windowed_rect.top,
                     app->windowed_rect.right - app->windowed_rect.left,
                     app->windowed_rect.bottom - app->windowed_rect.top,
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    app_layout_children(app);
    app_update_status(app);
}

static int write_ppm_frame(const char *path, const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb = bdm_video_framebuffer(video, &w, &h);
    FILE *f;
    if (!path || !fb || w != BDM_LCD_WIDTH || h != BDM_LCD_HEIGHT) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h);
    for (size_t i = 0; i < w * h; ++i) {
        unsigned char rgb[3];
        rgb[0] = (unsigned char)((fb[i] >> 16) & 0xffu);
        rgb[1] = (unsigned char)((fb[i] >> 8) & 0xffu);
        rgb[2] = (unsigned char)(fb[i] & 0xffu);
        fwrite(rgb, 1, 3, f);
    }
    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}

static void app_open_cart(bdm_win32_app_t *app) {
    char cart[BDM_UI_PATH_MAX];
    if (!browse_open_file(app->hwnd, "Open Bandai Design Master Program ROM", "ROM images\0*.bin;*.rom;*.dat;*.*\0All files\0*.*\0", cart, sizeof(cart))) return;
    copy_path(app->cart_path, sizeof(app->cart_path), cart);
    app->media_path[0] = 0;
    app->load_state_path[0] = 0;
    app_sync_options(app);
    app_start_machine(app);
}

static void app_open_pair(bdm_win32_app_t *app) {
    char cart[BDM_UI_PATH_MAX], media[BDM_UI_PATH_MAX];
    if (!browse_open_file(app->hwnd, "Open Program ROM", "ROM images\0*.bin;*.rom;*.dat;*.*\0All files\0*.*\0", cart, sizeof(cart))) return;
    if (!browse_open_file(app->hwnd, "Open Media ROM", "ROM images\0*.bin;*.rom;*.dat;*.*\0All files\0*.*\0", media, sizeof(media))) return;
    copy_path(app->cart_path, sizeof(app->cart_path), cart);
    copy_path(app->media_path, sizeof(app->media_path), media);
    app->load_state_path[0] = 0;
    app_sync_options(app);
    app_start_machine(app);
}


static void app_open_bios(bdm_win32_app_t *app) {
    char bios[BDM_UI_PATH_MAX];
    if (!browse_open_file(app->hwnd, "Open Optional H8 BIOS ROM", "ROM images\0*.bin;*.rom;*.dat;*.*\0All files\0*.*\0", bios, sizeof(bios))) return;
    copy_path(app->bios_path, sizeof(app->bios_path), bios);
    app_sync_options(app);
    if (app->cart_path[0] || app->machine_loaded) app_start_machine(app);
    else app_update_status(app);
}

static void app_open_media(bdm_win32_app_t *app) {
    char media[BDM_UI_PATH_MAX];
    if (!app->machine_loaded) { app_error(app, "Open a program ROM first."); return; }
    if (!browse_open_file(app->hwnd, "Replace Media ROM", "ROM images\0*.bin;*.rom;*.dat;*.*\0All files\0*.*\0", media, sizeof(media))) return;
    copy_path(app->media_path, sizeof(app->media_path), media);
    app_sync_options(app);
    app_start_machine(app);
}

static void app_load_state_dialog(bdm_win32_app_t *app) {
    char path[BDM_UI_PATH_MAX];
    if (!app->machine_loaded) { app_error(app, "Open the matching ROM before loading a state."); return; }
    if (!browse_open_file(app->hwnd, "Load BDM State", "BDM states\0*.bdmst\0All files\0*.*\0", path, sizeof(path))) return;
    if (bdm_fe_load_state_file(path, app->machine.core) != 0) app_error(app, "State load failed.");
    else {
        copy_path(app->state_slot_path, sizeof(app->state_slot_path), path);
        app_sync_options(app);
        bdm_fe_touch_force_clear(app->machine.input, &app->touch);
        InvalidateRect(app->video_hwnd, NULL, FALSE);
    }
    app_update_status(app);
}

static void app_save_state_dialog(bdm_win32_app_t *app) {
    char path[BDM_UI_PATH_MAX];
    if (!app->machine_loaded) { app_error(app, "No machine is running."); return; }
    if (!browse_save_file(app->hwnd, "Save BDM State", "BDM states\0*.bdmst\0All files\0*.*\0", "bdmst", path, sizeof(path))) return;
    if (bdm_fe_save_state_file(path, app->machine.core) != 0) app_error(app, "State save failed.");
    else copy_path(app->state_slot_path, sizeof(app->state_slot_path), path);
    app_sync_options(app);
    app_update_status(app);
}

static void app_quick_save(bdm_win32_app_t *app) {
    if (!app->machine_loaded) return;
    app_sync_options(app);
    if (bdm_fe_save_state_file(app->opt.state_slot_path, app->machine.core) != 0) app_error(app, "Quick save failed.");
    else app_update_status(app);
}

static void app_quick_load(bdm_win32_app_t *app) {
    if (!app->machine_loaded) return;
    app_sync_options(app);
    if (bdm_fe_load_state_file(app->opt.state_slot_path, app->machine.core) != 0) app_error(app, "Quick load failed.");
    else {
        bdm_fe_touch_force_clear(app->machine.input, &app->touch);
        InvalidateRect(app->video_hwnd, NULL, FALSE);
    }
    app_update_status(app);
}

static void app_save_frame_dialog(bdm_win32_app_t *app) {
    char path[BDM_UI_PATH_MAX];
    if (!app->machine_loaded) return;
    if (!browse_save_file(app->hwnd, "Save LCD Frame", "PPM images\0*.ppm\0All files\0*.*\0", "ppm", path, sizeof(path))) return;
    bdm_video_present_headless(app->machine.video);
    if (write_ppm_frame(path, app->machine.video) != 0) app_error(app, "Frame save failed.");
}

static void app_save_wav_dialog(bdm_win32_app_t *app) {
    char path[BDM_UI_PATH_MAX];
    size_t cap_frames = 0;
    const int16_t *cap;
    int rc;
    if (!app->machine_loaded) return;
    if (!browse_save_file(app->hwnd, "Save Captured Audio", "WAV audio\0*.wav\0All files\0*.*\0", "wav", path, sizeof(path))) return;
    cap = app->audio_backend ? bdm_win32_audio_capture(app->audio_backend, &cap_frames) : NULL;
    if (cap_frames) rc = bdm_fe_dump_wav_samples(path, cap, cap_frames, app->audio_backend ? bdm_win32_audio_sample_rate(app->audio_backend) : app->opt.sample_rate);
    else rc = bdm_fe_dump_wav(path, app->machine.sound);
    if (rc != 0) app_error(app, "WAV save failed. Enable audio capture with --dump-wav, or run long enough for the core recorder to collect samples.");
}

static void app_resize_to_scale(bdm_win32_app_t *app, unsigned scale) {
    RECT wr, cr;
    int extra_w, extra_h;
    if (!app || !app->hwnd || scale < 1) return;
    app->opt.scale = scale;
    GetWindowRect(app->hwnd, &wr);
    GetClientRect(app->hwnd, &cr);
    extra_w = (wr.right - wr.left) - (cr.right - cr.left);
    extra_h = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    SetWindowPos(app->hwnd, NULL, 0, 0,
                 (int)(BDM_LCD_WIDTH * scale) + extra_w,
                 (int)(BDM_LCD_HEIGHT * scale) + BDM_UI_STATUS_H + extra_h,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOOWNERZORDER);
    app_update_status(app);
}

static void app_reset_machine(bdm_win32_app_t *app) {
    if (!app || !app->machine_loaded) return;
    app_sync_options(app);
    bdm_status_t rc = bdm_fe_soft_reset(app->machine.core, app->machine.input, &app->touch, &app->opt, 1);
    if (rc != BDM_OK) {
        bdm_core_state_t st;
        char err[256];
        bdm_core_get_state(app->machine.core, &st);
        snprintf(err, sizeof(err), "Reset auto sequence failed: rc=%d pc=%04x op=%04x steps=%" PRIu64, rc, st.pc, st.last_opcode, st.steps);
        app_error(app, err);
        app->paused = 1;
    }
    bdm_video_present_headless(app->machine.video);
    if (app->video_backend) bdm_win32_video_present(app->video_backend, app->machine.video);
    InvalidateRect(app->video_hwnd, NULL, FALSE);
    app_update_status(app);
}

static void app_set_panel_button(bdm_win32_app_t *app, bdm_button_t button, int pressed) {
    if (!app || !app->machine_loaded) return;
    bdm_fe_set_panel_button(app->machine.input, &app->touch, app->machine.core, button, pressed);
}

static void app_update_panel_button_latch(bdm_win32_app_t *app, bdm_button_t button, int down) {
    if (!app || button < 0 || button >= BDM_BUTTON_COUNT) return;
    uint8_t v = down ? 1u : 0u;
    if (app->joy_panel_down[button] == v) return;
    app->joy_panel_down[button] = v;
    app_set_panel_button(app, button, down);
}

static void apply_virtual_key(bdm_win32_app_t *app, UINT vk, int pressed) {
    if (!app || !app->machine_loaded) return;
    switch (vk) {
    case 'A': case 'Z': app_set_panel_button(app, BDM_BUTTON_MENU_A, pressed); break;
    case 'B': case 'X': app_set_panel_button(app, BDM_BUTTON_MENU_B, pressed); break;
    case 'C': app_set_panel_button(app, BDM_BUTTON_MENU_C, pressed); break;
    case 'D': app_set_panel_button(app, BDM_BUTTON_MENU_D, pressed); break;
    case 'E': app_set_panel_button(app, BDM_BUTTON_MENU_E, pressed); break;
    case VK_LEFT: case VK_BACK: app_set_panel_button(app, BDM_BUTTON_PAGE_LEFT, pressed); break;
    case VK_RIGHT: case VK_RETURN: app_set_panel_button(app, BDM_BUTTON_PAGE_RIGHT, pressed); break;
    default: break;
    }
}

static void poll_winmm_gamepads(bdm_win32_app_t *app) {
#if !defined(BDM_WIN64_FRONTEND)
    if (!app || !app->machine_loaded) return;
    JOYINFOEX jx;
    memset(&jx, 0, sizeof(jx));
    jx.dwSize = sizeof(jx);
    jx.dwFlags = JOY_RETURNBUTTONS;
    if (joyGetPosEx(JOYSTICKID1, &jx) != JOYERR_NOERROR) return;
    app_update_panel_button_latch(app, BDM_BUTTON_MENU_A, (jx.dwButtons & JOY_BUTTON1) != 0);
    app_update_panel_button_latch(app, BDM_BUTTON_MENU_B, (jx.dwButtons & JOY_BUTTON2) != 0);
    app_update_panel_button_latch(app, BDM_BUTTON_MENU_C, (jx.dwButtons & JOY_BUTTON3) != 0);
    app_update_panel_button_latch(app, BDM_BUTTON_MENU_D, (jx.dwButtons & JOY_BUTTON4) != 0);
#ifdef JOY_BUTTON5
    app_update_panel_button_latch(app, BDM_BUTTON_MENU_E, (jx.dwButtons & JOY_BUTTON5) != 0);
#endif
#ifdef JOY_BUTTON6
    app_update_panel_button_latch(app, BDM_BUTTON_PAGE_LEFT, (jx.dwButtons & JOY_BUTTON6) != 0);
#endif
#ifdef JOY_BUTTON7
    app_update_panel_button_latch(app, BDM_BUTTON_PAGE_RIGHT, (jx.dwButtons & JOY_BUTTON7) != 0);
#endif
#else
    (void)app;
#endif
}

#if defined(BDM_WIN64_FRONTEND)
static void register_raw_keyboard(HWND hwnd) {
    RAWINPUTDEVICE rid;
    memset(&rid, 0, sizeof(rid));
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_KEYBOARD;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

static void process_raw_input(bdm_win32_app_t *app, HRAWINPUT hri) {
    UINT size = 0;
    if (GetRawInputData(hri, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0 || !size) return;
    BYTE *buf = (BYTE *)malloc(size);
    if (!buf) return;
    if (GetRawInputData(hri, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
        RAWINPUT *ri = (RAWINPUT *)buf;
        if (ri->header.dwType == RIM_TYPEKEYBOARD) {
            UINT msg = ri->data.keyboard.Message;
            int pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            int released = (msg == WM_KEYUP || msg == WM_SYSKEYUP);
            if (pressed || released) apply_virtual_key(app, ri->data.keyboard.VKey, pressed);
        }
    }
    free(buf);
}
#else
static void register_raw_keyboard(HWND hwnd) { (void)hwnd; }
#endif


static LRESULT CALLBACK panel_bg_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int create_main_window(bdm_win32_app_t *app) {
#if defined(BDM_WIN32_WIN31)
    WNDCLASSA wc;
    WNDCLASSA vc;
    WNDCLASSA pc;
#else
    WNDCLASSEXA wc;
    WNDCLASSEXA vc;
    WNDCLASSEXA pc;
#endif
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    RECT r;
    memset(&wc, 0, sizeof(wc));
#if !defined(BDM_WIN32_WIN31)
    wc.cbSize = sizeof(wc);
#endif
    wc.lpfnWndProc = main_wndproc;
    wc.hInstance = app->instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = app_load_icon(app->instance, 0);
#if !defined(BDM_WIN32_WIN31)
    wc.hIconSm = app_load_icon(app->instance, 1);
#endif
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "BDMWin32Frontend";
#if defined(BDM_WIN32_WIN31)
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#else
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#endif

    memset(&vc, 0, sizeof(vc));
#if !defined(BDM_WIN32_WIN31)
    vc.cbSize = sizeof(vc);
#endif
    vc.lpfnWndProc = video_wndproc;
    vc.hInstance = app->instance;
    vc.hCursor = LoadCursor(NULL, IDC_ARROW);
    vc.hIcon = app_load_icon(app->instance, 0);
#if !defined(BDM_WIN32_WIN31)
    vc.hIconSm = app_load_icon(app->instance, 1);
#endif
    vc.style = CS_OWNDC;
    vc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    vc.lpszClassName = "BDMWin32VideoView";
#if defined(BDM_WIN32_WIN31)
    if (!RegisterClassA(&vc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#else
    if (!RegisterClassExA(&vc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#endif

    memset(&pc, 0, sizeof(pc));
#if !defined(BDM_WIN32_WIN31)
    pc.cbSize = sizeof(pc);
#endif
    pc.lpfnWndProc = panel_bg_wndproc;
    pc.hInstance = app->instance;
    pc.hCursor = LoadCursor(NULL, IDC_ARROW);
    pc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    pc.lpszClassName = "BDMWin32PanelBackground";
#if defined(BDM_WIN32_WIN31)
    if (!RegisterClassA(&pc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#else
    if (!RegisterClassExA(&pc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
#endif

    app->main_menu = app_create_menu(app);
    app->accel = app_create_accel();
    r.left = 0; r.top = 0;
    r.right = (LONG)(BDM_LCD_WIDTH * app->opt.scale);
    r.bottom = (LONG)(BDM_LCD_HEIGHT * app->opt.scale + BDM_UI_STATUS_H);
#if defined(BDM_WIN32_WIN31)
    AdjustWindowRect(&r, style, TRUE);
    app->hwnd = CreateWindowA(wc.lpszClassName, "Bandai Design Master",
                              style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              NULL, app->main_menu, app->instance, app);
#else
    AdjustWindowRectEx(&r, style, TRUE, 0);
    app->hwnd = CreateWindowExA(0, wc.lpszClassName, "Bandai Design Master",
                                style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                NULL, app->main_menu, app->instance, app);
#endif
    if (!app->hwnd) return 0;
    SendMessage(app->hwnd, WM_SETICON, ICON_BIG, (LPARAM)app_load_icon(app->instance, 0));
#if !defined(BDM_WIN32_WIN31)
    SendMessage(app->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)app_load_icon(app->instance, 1));
#endif
#if defined(BDM_WIN32_WIN31)
    app->video_hwnd = CreateWindowA(vc.lpszClassName, "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                                    0, 0, 1, 1, app->hwnd, NULL, app->instance, app);
#else
    app->video_hwnd = CreateWindowExA(0, vc.lpszClassName, "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                                      0, 0, 1, 1, app->hwnd, NULL, app->instance, app);
#endif
    if (!app->video_hwnd) return 0;
    for (int i = 0; i < 3; ++i) {
#if defined(BDM_WIN32_WIN31)
        app->panel_bg[i] = CreateWindowA("BDMWin32PanelBackground", "", WS_CHILD | WS_CLIPSIBLINGS,
                                         0, 0, 1, 1, app->hwnd, NULL, app->instance, app);
#else
        app->panel_bg[i] = CreateWindowExA(0, "BDMWin32PanelBackground", "", WS_CHILD | WS_CLIPSIBLINGS,
                                           0, 0, 1, 1, app->hwnd, NULL, app->instance, app);
#endif
    }
    {
        const char *labels[7] = { "A", "B", "C", "D", "E", "<", ">" };
        UINT ids[7] = { IDM_INPUT_PANEL_A, IDM_INPUT_PANEL_B, IDM_INPUT_PANEL_C, IDM_INPUT_PANEL_D, IDM_INPUT_PANEL_E, IDM_INPUT_PANEL_LEFT, IDM_INPUT_PANEL_RIGHT };
        for (int i = 0; i < 7; ++i) {
#if defined(BDM_WIN32_WIN31)
            app->panel_buttons[i] = CreateWindowA("BUTTON", labels[i], WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
                                                 0, 0, 1, 1, app->hwnd, (HMENU)(UINT_PTR)ids[i], app->instance, NULL);
#else
            app->panel_buttons[i] = CreateWindowExA(0, "BUTTON", labels[i], WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
                                                   0, 0, 1, 1, app->hwnd, (HMENU)(UINT_PTR)ids[i], app->instance, NULL);
#endif
            if (app->panel_buttons[i]) SetWindowPos(app->panel_buttons[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
#if defined(BDM_WIN32_WIN31)
    app->status_hwnd = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFTNOWORDWRAP,
                                     0, 0, 1, BDM_UI_STATUS_H, app->hwnd, NULL, app->instance, NULL);
#else
    app->status_hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFTNOWORDWRAP,
                                       0, 0, 1, BDM_UI_STATUS_H, app->hwnd, NULL, app->instance, NULL);
#endif
    register_raw_keyboard(app->hwnd);
    app_layout_children(app);
    ShowWindow(app->hwnd, SW_SHOW);
    UpdateWindow(app->hwnd);
    SetFocus(app->video_hwnd);
    return 1;
}

static void app_handle_recent(bdm_win32_app_t *app, UINT id) {
    int idx = (int)(id - IDM_FILE_RECENT_BASE);
    if (!app || idx < 0 || idx >= app->recent_count) return;
    copy_path(app->cart_path, sizeof(app->cart_path), app->recent[idx].cart);
    copy_path(app->media_path, sizeof(app->media_path), app->recent[idx].media);
    app->load_state_path[0] = 0;
    app_sync_options(app);
    app_start_machine(app);
}

static bdm_button_t app_panel_button_from_command(UINT id) {
    switch (id) {
    case IDM_INPUT_PANEL_A: return BDM_BUTTON_MENU_A;
    case IDM_INPUT_PANEL_B: return BDM_BUTTON_MENU_B;
    case IDM_INPUT_PANEL_C: return BDM_BUTTON_MENU_C;
    case IDM_INPUT_PANEL_D: return BDM_BUTTON_MENU_D;
    case IDM_INPUT_PANEL_E: return BDM_BUTTON_MENU_E;
    case IDM_INPUT_PANEL_LEFT: return BDM_BUTTON_PAGE_LEFT;
    case IDM_INPUT_PANEL_RIGHT: return BDM_BUTTON_PAGE_RIGHT;
    default: return BDM_BUTTON_COUNT;
    }
}

static void app_click_panel_button(bdm_win32_app_t *app, bdm_button_t button) {
    if (!app || button <= BDM_BUTTON_PEN || button >= BDM_BUTTON_COUNT) return;
    app_set_panel_button(app, button, 1);
    app->click_panel_latch[button] = 1u;
    app->click_panel_latch_frames[button] = 5u;
    if (app->video_hwnd) SetFocus(app->video_hwnd);
}

static void app_tick_panel_button_latches(bdm_win32_app_t *app) {
    if (!app) return;
    for (unsigned i = 0; i < (unsigned)BDM_BUTTON_COUNT; ++i) {
        if (!app->click_panel_latch[i]) continue;
        if (app->click_panel_latch_frames[i] > 0u) --app->click_panel_latch_frames[i];
        if (app->click_panel_latch_frames[i] == 0u) {
            app->click_panel_latch[i] = 0u;
            app_set_panel_button(app, (bdm_button_t)i, 0);
        }
    }
}

static void app_handle_command(bdm_win32_app_t *app, UINT id) {
    if (!app) return;
    if (id >= IDM_INPUT_PANEL_BASE && id <= IDM_INPUT_PANEL_RIGHT) {
        app_click_panel_button(app, app_panel_button_from_command(id));
        return;
    }
    if (id >= IDM_FILE_RECENT_BASE && id < IDM_FILE_RECENT_BASE + BDM_UI_RECENT_MAX) { app_handle_recent(app, id); return; }
    if (id > IDM_VIDEO_SCALE_BASE && id <= IDM_VIDEO_SCALE_BASE + 6) { app_resize_to_scale(app, id - IDM_VIDEO_SCALE_BASE); return; }
    switch (id) {
    case IDM_FILE_OPEN_CART: app_open_cart(app); break;
    case IDM_FILE_OPEN_PAIR: app_open_pair(app); break;
    case IDM_FILE_OPEN_MEDIA: app_open_media(app); break;
    case IDM_FILE_OPEN_BIOS: app_open_bios(app); break;
    case IDM_FILE_RELOAD: app_reload_machine(app); break;
    case IDM_FILE_LOAD_STATE: app_load_state_dialog(app); break;
    case IDM_FILE_SAVE_STATE: app_save_state_dialog(app); break;
    case IDM_FILE_QUICK_SAVE: app_quick_save(app); break;
    case IDM_FILE_QUICK_LOAD: app_quick_load(app); break;
    case IDM_FILE_SAVE_FRAME: app_save_frame_dialog(app); break;
    case IDM_FILE_SAVE_WAV: app_save_wav_dialog(app); break;
    case IDM_FILE_EXIT: app->quit_requested = 1; DestroyWindow(app->hwnd); break;
    case IDM_EMU_PAUSE: app->paused = !app->paused; app_update_status(app); break;
    case IDM_EMU_RESET: app_reset_machine(app); break;
    case IDM_EMU_AUTO_CAL: app->opt.auto_calibrate = !app->opt.auto_calibrate; app_update_status(app); break;
    case IDM_EMU_AUTO_TITLE: app->opt.auto_title = !app->opt.auto_title; app_update_status(app); break;
    case IDM_EMU_AUTO_MENU: app->opt.auto_menu = !app->opt.auto_menu; app_update_status(app); break;
    case IDM_EMU_AUTO_MODE1: app->opt.auto_mode1 = !app->opt.auto_mode1; if (app->opt.auto_mode1) app->opt.auto_menu = 1; app_update_status(app); break;
    case IDM_VIDEO_GDI: copy_path(app->video_backend_name, sizeof(app->video_backend_name), "gdi"); app_sync_options(app); app_recreate_video(app); break;
    case IDM_VIDEO_D3D11:
#if defined(BDM_WIN64_FRONTEND)
        copy_path(app->video_backend_name, sizeof(app->video_backend_name), "d3d11"); app_sync_options(app); app_recreate_video(app);
#endif
        break;
    case IDM_VIDEO_INTEGER: app->opt.integer_scaling = !app->opt.integer_scaling; if (app->video_backend) bdm_win32_video_set_integer_scaling(app->video_backend, app->opt.integer_scaling); app_update_status(app); break;
    case IDM_VIDEO_ASPECT: app->opt.integer_scaling = 0; if (app->video_backend) bdm_win32_video_set_integer_scaling(app->video_backend, 0); app_update_status(app); break;
    case IDM_VIDEO_FULLSCREEN: toggle_fullscreen(app); break;
    case IDM_AUDIO_ENABLE: app->opt.enable_audio = !app->opt.enable_audio; if (!app->opt.enable_audio) copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), "none"); else if (str_eq_i(app->audio_backend_name, "none")) copy_path(app->audio_backend_name, sizeof(app->audio_backend_name),
#if defined(BDM_WIN64_FRONTEND)
        "wasapi"
#else
        "waveout"
#endif
        ); app_sync_options(app); app_recreate_audio(app); break;
    case IDM_AUDIO_WAVEOUT: app->opt.enable_audio = 1; copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), "waveout"); app_sync_options(app); app_recreate_audio(app); break;
    case IDM_AUDIO_WASAPI:
#if defined(BDM_WIN64_FRONTEND)
        app->opt.enable_audio = 1; copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), "wasapi"); app_sync_options(app); app_recreate_audio(app);
#endif
        break;
    case IDM_AUDIO_NONE: app->opt.enable_audio = 0; copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), "none"); app_sync_options(app); app_recreate_audio(app); break;
    case IDM_INPUT_RESET_OFFSET: app->opt.touch_offset_x = 0; app->opt.touch_offset_y = 0; app_update_status(app); break;
    case IDM_INPUT_OFFSET_X_DEC: if (app->opt.touch_offset_x > -8) --app->opt.touch_offset_x; app_update_status(app); break;
    case IDM_INPUT_OFFSET_X_INC: if (app->opt.touch_offset_x < 8) ++app->opt.touch_offset_x; app_update_status(app); break;
    case IDM_INPUT_OFFSET_Y_DEC: if (app->opt.touch_offset_y > -8) --app->opt.touch_offset_y; app_update_status(app); break;
    case IDM_INPUT_OFFSET_Y_INC: if (app->opt.touch_offset_y < 8) ++app->opt.touch_offset_y; app_update_status(app); break;
    case IDM_INPUT_HOLD_DEC: if (app->opt.touch_hold_ms >= 5) app->opt.touch_hold_ms -= 5; app->touch.min_hold_steps = app_touch_hold_steps(app, app->opt.touch_hold_ms); app_update_status(app); break;
    case IDM_INPUT_HOLD_INC: if (app->opt.touch_hold_ms <= 4995) app->opt.touch_hold_ms += 5; app->touch.min_hold_steps = app_touch_hold_steps(app, app->opt.touch_hold_ms); app_update_status(app); break;
    case IDM_INPUT_DEBUG: app->opt.touch_debug = !app->opt.touch_debug; app->touch.debug = app->opt.touch_debug; app_update_status(app); break;
    case IDM_INPUT_CROSSHAIR: app->touch_crosshair_cursor = !app->touch_crosshair_cursor; SetCursor(LoadCursor(NULL, app->touch_crosshair_cursor ? IDC_CROSS : IDC_ARROW)); app_update_status(app); break;
    case IDM_INPUT_VISIBLE_PANEL:
        app->visible_panel_buttons = !app->visible_panel_buttons;
        app_layout_children(app);
        app_redraw_shell(app);
        app_update_status(app);
        break;
    case IDM_HELP_INPUT:
        app_message(app->hwnd, MB_ICONINFORMATION, "Input Mapping",
                    "Pen: mouse/touch on the LCD\n"
                    "Menu A-E: A/B/C/D/E keys (Z/X also alias A/B)\n"
                    "Page left/right: Left/Right arrows, Backspace/Enter\n"
                    "Visible controls: Input > Show hardware panel buttons\n"
                    "Gamepad: face buttons map to panel A-E and shoulder/extra buttons to page L/R\n"
                    "Quick save/load: F5/F8\nFullscreen: F11\nReset touch offset: Ctrl+0");
        break;
    case IDM_HELP_ABOUT:
        app_message(app->hwnd, MB_ICONINFORMATION, "About Bandai Design Master Emulator",
#if defined(BDM_WIN64_FRONTEND)
                    "Bandai Design Master Emulator\nNative Win64 UI backend\nVideo: D3D11 shader or GDI\nAudio: WASAPI or waveOut\nInput: raw keyboard, mouse/touch, SDL3 gamepad"
#else
                    "Bandai Design Master Emulator\nNative Win32 UI backend\nVideo: GDI\nAudio: waveOut\nInput: window keyboard, mouse/touch, WinMM gamepad"
#endif
                    );
        break;
    default: break;
    }
}

static LRESULT CALLBACK main_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bdm_win32_app_t *app = (bdm_win32_app_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        /* The parent owns the exposed frame around the LCD child.  Keep it
         * deterministically black so toggling the optional hardware buttons
         * never leaves stale game pixels behind the controls. */
        FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (app) app_layout_children(app);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = 320;
        mmi->ptMinTrackSize.y = 260;
        return 0;
    }
    case WM_COMMAND:
        if (app) app_handle_command(app, LOWORD(wp));
        return 0;
#if defined(BDM_WIN64_FRONTEND)
    case WM_INPUT:
        if (app) process_raw_input(app, (HRAWINPUT)lp);
        return 0;
#endif
    case WM_CLOSE:
        if (app) app->quit_requested = 1;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (app) app->quit_requested = 1;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK video_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bdm_win32_app_t *app = (bdm_win32_app_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lp;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    case WM_SETFOCUS:
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == hwnd) {
            SetCursor(LoadCursor(NULL, (app && app->touch_crosshair_cursor) ? IDC_CROSS : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (app && app->video_backend) bdm_win32_video_resize(app->video_backend, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (app && !(lp & (1u << 30))) apply_virtual_key(app, (UINT)wp, 1);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (app) apply_virtual_key(app, (UINT)wp, 0);
        return 0;
    case WM_LBUTTONDOWN:
        if (app && app->machine_loaded) {
            int32_t px = 0, py = 0;
            SetFocus(hwnd);
            SetCapture(hwnd);
            client_to_pen_fp(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &px, &py);
            app_prepare_touch_down(app);
            bdm_fe_touch_apply_down_fp(app->machine.input, &app->touch, app->machine.core, px, py);
        }
        return 0;
    case WM_LBUTTONUP:
        if (app && app->machine_loaded) {
            int32_t px = 0, py = 0;
            ReleaseCapture();
            client_to_pen_fp(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &px, &py);
            bdm_fe_touch_request_up_fp(app->machine.input, &app->touch, app->machine.core, px, py);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (app && app->machine_loaded && (app->touch.physical_down || app->touch.emulated_down)) {
            int32_t px = 0, py = 0;
            client_to_pen_fp(app, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &px, &py);
            bdm_fe_touch_update_motion_fp(app->machine.input, &app->touch, px, py);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        (void)dc;
        if (app && app->machine_loaded && app->video_backend) {
            bdm_video_present_headless(app->machine.video);
            (void)bdm_win32_video_present(app->video_backend, app->machine.video);
        } else {
            HDC hdc = GetDC(hwnd);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            SetTextColor(hdc, RGB(180, 180, 180));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, "Open a Bandai Design Master ROM from File > Open Program ROM...", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            ReleaseDC(hwnd, hdc);
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int app_parse_startup(bdm_win32_app_t *app, int argc, char **argv) {
    bdm_fe_options_t parsed;
    if (!app) return 0;
    bdm_fe_options_init(&parsed);
#if defined(BDM_WIN64_FRONTEND)
    parsed.video_backend = "d3d11";
    parsed.audio_backend = "wasapi";
#else
    parsed.video_backend = "gdi";
    parsed.audio_backend = "waveout";
#endif
    app_capture_option_paths(app, &parsed);
    if (argc > 1) {
        if (bdm_fe_parse_args(argc, argv, &parsed, 1)) {
            app_capture_option_paths(app, &parsed);
            return 1;
        }
        app_message(NULL, MB_ICONWARNING, "Bandai Design Master",
                    "The command line was not valid for the emulator core. Starting the Windows UI without an opened ROM.");
    }
    return 1;
}

static int app_init(bdm_win32_app_t *app, int argc, char **argv) {
    memset(app, 0, sizeof(*app));
    app->instance = GetModuleHandle(NULL);
    if (timeBeginPeriod(1) == TIMERR_NOERROR) app->timer_period_set = 1;
    if (!app_parse_startup(app, argc, argv)) return 0;
#if !defined(BDM_WIN64_FRONTEND)
    copy_path(app->video_backend_name, sizeof(app->video_backend_name), "gdi");
    if (!str_eq_i(app->audio_backend_name, "waveout") && !str_eq_i(app->audio_backend_name, "none")) copy_path(app->audio_backend_name, sizeof(app->audio_backend_name), "waveout");
#endif
    app_set_recent_store_path(app);
    app_recent_load(app);
    if (!create_main_window(app)) {
        app_error(app, "Window creation failed.");
        return 0;
    }
    app->video_backend = bdm_win32_video_create(app->video_hwnd, app->opt.scale, app->opt.integer_scaling,
                                                app->video_backend_name[0] ? app->video_backend_name : "gdi");
    app->sdl_input = bdm_win32_sdl_input_create();
    app_sync_options(app);
    app_update_status(app);
    if (app->opt.fullscreen) toggle_fullscreen(app);
    if (app->cart_path[0] || app->bios_path[0]) app_start_machine(app);
    return 1;
}

static void app_shutdown(bdm_win32_app_t *app) {
    if (!app) return;
    if (app->opt.save_state_path && app->machine_loaded) {
        if (bdm_fe_save_state_file(app->opt.save_state_path, app->machine.core) == 0) printf("wrote %s\n", app->opt.save_state_path);
        else fprintf(stderr, "state save failed: %s\n", app->opt.save_state_path);
    }
    if (app->opt.dump_wav_path && app->machine_loaded && app->machine.sound) {
        int wav_rc = -1;
        size_t cap_frames = 0;
        const int16_t *cap = app->audio_backend ? bdm_win32_audio_capture(app->audio_backend, &cap_frames) : NULL;
        if (cap_frames) wav_rc = bdm_fe_dump_wav_samples(app->opt.dump_wav_path, cap, cap_frames,
                                                         app->audio_backend ? bdm_win32_audio_sample_rate(app->audio_backend) : app->opt.sample_rate);
        else wav_rc = bdm_fe_dump_wav(app->opt.dump_wav_path, app->machine.sound);
        if (wav_rc != 0) fprintf(stderr, "WAV dump failed: %s\n", app->opt.dump_wav_path);
        else printf("wrote %s\n", app->opt.dump_wav_path);
    }
    if (app->machine_loaded && bdm_fe_save_sram_if_requested(app->opt.save_sram_path, app->machine.core) == 0 && app->opt.save_sram_path) printf("wrote %s\n", app->opt.save_sram_path);
    app_stop_machine(app);
    bdm_win32_sdl_input_destroy(app->sdl_input);
    bdm_win32_video_destroy(app->video_backend);
    if (app->accel) DestroyAcceleratorTable(app->accel);
    if (app->main_menu) DestroyMenu(app->main_menu);
    if (app->timer_period_set) {
        timeEndPeriod(1);
        app->timer_period_set = 0;
    }
}

static int app_run(bdm_win32_app_t *app) {
    uint64_t step_remainder = 0;
    uint64_t pacer_base_ns = qpc_now_ns();
    uint64_t pacer_frame = 0;
    int pacer_reset = 1;
    app->fps_window_ms = ticks_ms();
    while (!app->quit_requested) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!app->accel || !TranslateAccelerator(app->hwnd, app->accel, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (app->machine_loaded && !app->paused) {
            if (pacer_reset) {
                pacer_base_ns = qpc_now_ns();
                pacer_frame = 0;
                pacer_reset = 0;
            }
#if defined(BDM_WIN64_FRONTEND)
            bdm_win32_sdl_input_poll(app->sdl_input, app->machine.input, app->machine.core, &app->touch, &app->quit_requested);
#else
            poll_winmm_gamepads(app);
#endif
            step_remainder += app->opt.steps_per_second;
            uint64_t frame_steps = step_remainder / app->opt.fps;
            step_remainder %= app->opt.fps;
            bdm_status_t rc = bdm_fe_run_checked(app->machine.core, frame_steps);
            app_tick_panel_button_latches(app);
            bdm_fe_touch_tick_release(app->machine.input, &app->touch, app->machine.core);
            if (rc != BDM_OK) {
                bdm_core_state_t st;
                char err[256];
                bdm_core_get_state(app->machine.core, &st);
                snprintf(err, sizeof(err), "Emulation stopped: rc=%d pc=%04x last_op=%04x steps=%" PRIu64, rc, st.pc, st.last_opcode, st.steps);
                app_error(app, err);
                app->paused = 1;
                pacer_reset = 1;
            }
            if (app->audio_backend && bdm_win32_audio_pump(app->audio_backend, app->opt.fps) != 0) fprintf(stderr, "audio pump failed\n");
            bdm_video_present_headless(app->machine.video);
            if (bdm_win32_video_present(app->video_backend, app->machine.video) != 0) fprintf(stderr, "video present failed\n");
            ++app->frame_counter;
            ++app->fps_window_frames;

            ++pacer_frame;
            uint64_t target_ns = pacer_base_ns + (pacer_frame * 1000000000ull) / (uint64_t)app->opt.fps;
            uint64_t now_ns = qpc_now_ns();
            if (target_ns > now_ns) {
                sleep_until_ns(target_ns);
            } else if (now_ns - target_ns > (4000000000ull / (uint64_t)app->opt.fps)) {
                pacer_base_ns = now_ns;
                pacer_frame = 0;
            }
        } else if (!app->machine_loaded) {
            pacer_reset = 1;
            WaitMessage();
        } else {
            pacer_reset = 1;
            Sleep(10);
        }
        uint64_t now = ticks_ms();
        if (now - app->fps_window_ms >= 1000u) {
            app->measured_fps = (unsigned)((app->fps_window_frames * 1000u) / (now - app->fps_window_ms));
            app->fps_window_frames = 0;
            app->fps_window_ms = now;
            app_update_status(app);
        }
    }
    return 0;
}

static int bdm_win32_main(int argc, char **argv) {
    bdm_win32_app_t app;
    if (!app_init(&app, argc, argv)) return 2;
    int rc = app_run(&app);
    app_shutdown(&app);
    return rc;
}

int main(int argc, char **argv) { return bdm_win32_main(argc, argv); }

#if !defined(BDM_WIN64_FRONTEND)
static char *dup_arg_range(const char *start, size_t len) {
    char *out = (char *)calloc(len + 1u, 1u);
    if (!out) return NULL;
    if (len) memcpy(out, start, len);
    out[len] = 0;
    return out;
}

static int argv_append(char ***argv_io, int *argc_io, int *cap_io, const char *start, size_t len) {
    if (!argv_io || !argc_io || !cap_io || !start) return 0;
    if (*argc_io + 1 >= *cap_io) {
        int new_cap = *cap_io ? (*cap_io * 2) : 8;
        char **new_argv = (char **)realloc(*argv_io, (size_t)new_cap * sizeof(char *));
        if (!new_argv) return 0;
        memset(new_argv + *cap_io, 0, (size_t)(new_cap - *cap_io) * sizeof(char *));
        *argv_io = new_argv;
        *cap_io = new_cap;
    }
    (*argv_io)[*argc_io] = dup_arg_range(start, len);
    if (!(*argv_io)[*argc_io]) return 0;
    ++(*argc_io);
    (*argv_io)[*argc_io] = NULL;
    return 1;
}

static int append_literal_arg(char ***argv_io, int *argc_io, int *cap_io, const char *text) {
    return argv_append(argv_io, argc_io, cap_io, text ? text : "", text ? strlen(text) : 0u);
}

static int parse_cmdline_a(LPSTR cmdline, char ***argv_out) {
    char **argv = NULL;
    int argc = 0;
    int cap = 0;
    const char *p = cmdline ? cmdline : "";
    if (!append_literal_arg(&argv, &argc, &cap, "bdm-win32.exe")) goto fail;
    while (*p) {
        char token[BDM_UI_PATH_MAX];
        size_t n = 0;
        int in_quotes = 0;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        while (*p) {
            char c = *p++;
            if (c == '"') {
                in_quotes = !in_quotes;
                continue;
            }
            if (!in_quotes && (c == ' ' || c == '\t')) break;
            if (n + 1u < sizeof(token)) token[n++] = c;
        }
        token[n] = 0;
        if (!argv_append(&argv, &argc, &cap, token, n)) goto fail;
    }
    *argv_out = argv;
    return argc;
fail:
    if (argv) {
        for (int i = 0; i < argc; ++i) free(argv[i]);
        free(argv);
    }
    *argv_out = NULL;
    return 0;
}
#endif

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int show) {
    (void)hInst; (void)hPrev; (void)show;
#if !defined(BDM_WIN64_FRONTEND)
    char **argv = NULL;
    int argc = parse_cmdline_a(cmdLine, &argv);
    int rc = bdm_win32_main(argc, argv);
    if (argv) {
        for (int i = 0; i < argc; ++i) free(argv[i]);
        free(argv);
    }
    return rc;
#else
    (void)cmdLine;
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc <= 0) return bdm_win32_main(0, NULL);
    char **argv = (char **)calloc((size_t)argc + 1u, sizeof(char *));
    if (!argv) { LocalFree(wargv); return 1; }
    for (int i = 0; i < argc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char *)calloc((size_t)len ? (size_t)len : 1u, 1u);
        if (argv[i]) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], len, NULL, NULL);
    }
    int rc = bdm_win32_main(argc, argv);
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv);
    LocalFree(wargv);
    return rc;
#endif
}
