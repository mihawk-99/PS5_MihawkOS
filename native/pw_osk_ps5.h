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

#ifdef __cplusplus
}
#endif
