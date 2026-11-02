/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The system's IME dialog (libSceImeDialog), shown over the running title. The calls are an operations table, so the
 * sequence (bring the dialog family up, load the module, init, poll, read the line, term) runs on the host against a
 * fake; pw_osk_ps5_system_ops() is the console's. Statuses and the parameter block are the ones the console's IME dialog
 * answers with (checked on a PS5 by the owner's other titles: ps5-radio-browser's search box). */
enum { PW_OSK_PS5_NONE = 0, PW_OSK_PS5_RUNNING = 1, PW_OSK_PS5_FINISHED = 2 };
enum { PW_OSK_PS5_OK = 0 };    /* the outcome of a finished dialog: 0 accepted, anything else cancelled */
enum { PW_OSK_PS5_TEXT = 128, PW_OSK_PS5_TITLE = 32, PW_OSK_PS5_GRACE_POLLS = 60 };

typedef struct PwOskPs5Ops {
    int (*common_dialog_init)(void);                /* sceCommonDialogInitialize; "already initialised" counts as done */
    int (*load_module)(void);                       /* sceSysmoduleLoadModule(IME dialog) */
    int (*foreground_user)(int32_t *user_id);       /* sceUserServiceGetForegroundUser */
    int (*dialog_init)(const void *param);          /* sceImeDialogInit(param, NULL) */
    int (*dialog_status)(void);                     /* sceImeDialogGetStatus */
    int (*dialog_result)(int32_t *outcome);         /* sceImeDialogGetResult */
    int (*dialog_term)(void);                       /* sceImeDialogTerm */
    int (*dialog_abort)(void);                      /* sceImeDialogAbort */
    void (*log)(const char *line);
} PwOskPs5Ops;

typedef struct PwOskPs5 {
    PwOskPs5Ops ops;
    uint16_t text[PW_OSK_PS5_TEXT], title[PW_OSK_PS5_TITLE];
    uint8_t ready, showing;
    unsigned polls, shown, accepted, cancelled;
} PwOskPs5;

/* 0, or a negative errno-like value: the module would not load, or the dialog would not start. */
int pw_osk_ps5_open(PwOskPs5 *osk, const PwOskPs5Ops *ops);
int pw_osk_ps5_show(PwOskPs5 *osk, const char *title);
/* Polled every frame while shown: 0 while the dialog is up; 1 when it ended with a line (text holds it, UTF-8); 2 when it
 * ended with nothing (cancelled, or gone before it drew); -1 when it is not shown. */
int pw_osk_ps5_poll(PwOskPs5 *osk, char *text, size_t capacity);
void pw_osk_ps5_close(PwOskPs5 *osk);
size_t pw_osk_ps5_utf16(const char *utf8, uint16_t *out, size_t capacity);
size_t pw_osk_ps5_utf8(const uint16_t *utf16, char *out, size_t capacity);
/* The console's operations (only where the system's libraries are linked: __PROSPERO__). */
const PwOskPs5Ops *pw_osk_ps5_system_ops(void);
/* How the console's operations found the IME dialog, step by step (empty before the first open, and on the host). */
const char *pw_osk_ps5_resolve_trail(void);

/* Finding the IME dialog's calls, run on the host against fakes. A standard PS5 on 13.60 found none of them where the Pro on
 * 10.01 did (2026-11-01: "keyboard common=0xfffffffe module=0xfffffffe"), so each way is tried in turn and written to the
 * trail: the module by its sysmodule id (refused on 13.60: 0x80020063), then by its internal id, its handle by that id and
 * by the internal id, then libSceImeDialog.sprx by path
 * (/system/common/lib, then the bare name) when the handle is missing or lacks a call. The first missing symbol is named. */
typedef struct PwOskResolveOps {
    int (*load_sysmodule)(uint16_t id);                          /* sceSysmoduleLoadModule */
    int (*load_internal)(uint32_t id);                           /* sceSysmoduleLoadModuleInternal; may be 0 */
    int (*sysmodule_handle)(uint32_t id, int32_t *handle);       /* sceSysmoduleGetModuleHandleInternal */
    int32_t (*load_path)(const char *path, int *result);         /* sceKernelLoadStartModule */
    int (*dlsym)(int32_t handle, const char *symbol, void **address);   /* sceKernelDlsym: 0 when found */
} PwOskResolveOps;
typedef struct PwOskResolved {
    int32_t handle;
    void *init, *status, *result, *term, *abort;
} PwOskResolved;
enum { PW_OSK_IME_DIALOG_MODULE = 0x0096 };
/* 1 when init, status, result and term were all found on one handle (abort is optional); trail says how either way. */
int pw_osk_ps5_resolve(const PwOskResolveOps *ops, PwOskResolved *out, char *trail, size_t capacity);

#ifdef __cplusplus
}
#endif
