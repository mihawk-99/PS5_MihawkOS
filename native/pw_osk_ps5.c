/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_osk_ps5.h"
#include <stdio.h>
#include <string.h>

/* The block sceImeDialogInit reads: 96 bytes. A mismatch would be read as garbage pointers, so it is checked here. */
typedef struct {
    int32_t user_id;
    int32_t type;                   /* 0: every language */
    uint64_t supported_languages;
    int32_t enter_label;
    int32_t input_method;
    void *filter;
    uint32_t option;
    uint32_t max_text_length;
    uint16_t *input_text_buffer;
    float pos_x, pos_y;
    int32_t horizontal_alignment, vertical_alignment;   /* 1: centred */
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} ImeParam;
_Static_assert(sizeof(ImeParam) == 96, "unexpected IME dialog parameter layout");

enum { ENTER_LABEL_DEFAULT = 2 };

size_t pw_osk_ps5_utf16(const char *utf8, uint16_t *out, size_t capacity)
{
    const unsigned char *text = (const unsigned char *)utf8;
    size_t written = 0;
    if (!out || !capacity) return 0;
    while (text && *text && written + 1 < capacity) {
        uint32_t code = *text;
        unsigned length = 1;
        if ((code & 0xe0u) == 0xc0u) { code &= 0x1fu; length = 2; }
        else if ((code & 0xf0u) == 0xe0u) { code &= 0x0fu; length = 3; }
        else if ((code & 0xf8u) == 0xf0u) { code &= 0x07u; length = 4; }
        else if (code & 0x80u) { code = 0xfffdu; length = 1; }
        for (unsigned i = 1; i < length; i++) {
            if ((text[i] & 0xc0u) != 0x80u) { code = 0xfffdu; length = 1; break; }
            code = (code << 6) | (text[i] & 0x3fu);
        }
        text += length;
        if (code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu)) code = 0xfffdu;
        if (code >= 0x10000u) {
            if (written + 2 >= capacity) break;
            code -= 0x10000u;
            out[written++] = (uint16_t)(0xd800u | (code >> 10));
            out[written++] = (uint16_t)(0xdc00u | (code & 0x3ffu));
        } else {
            out[written++] = (uint16_t)code;
        }
    }
    out[written] = 0;
    return written;
}

size_t pw_osk_ps5_utf8(const uint16_t *utf16, char *out, size_t capacity)
{
    size_t written = 0;
    if (!out || !capacity) return 0;
    for (size_t i = 0; utf16 && utf16[i]; i++) {
        uint32_t code = utf16[i];
        if (code >= 0xd800u && code <= 0xdbffu && utf16[i + 1] >= 0xdc00u && utf16[i + 1] <= 0xdfffu)
            code = 0x10000u + ((code - 0xd800u) << 10) + (utf16[++i] - 0xdc00u);
        else if (code >= 0xd800u && code <= 0xdfffu)
            code = 0xfffdu;
        const unsigned length = code < 0x80u ? 1u : code < 0x800u ? 2u : code < 0x10000u ? 3u : 4u;
        if (written + length >= capacity) break;
        if (length == 1) {
            out[written++] = (char)code;
        } else {
            /* the lead byte: 110xxxxx, 1110xxxx or 11110xxx (0xf00 >> length keeps the 1s of its low byte) */
            out[written] = (char)((0xf00u >> length) | (code >> (6 * (length - 1))));
            for (unsigned b = 1; b < length; b++)
                out[written + b] = (char)(0x80u | ((code >> (6 * (length - 1 - b))) & 0x3fu));
            written += length;
        }
    }
    out[written] = '\0';
    return written;
}

static void say(const PwOskPs5 *osk, const char *line)
{
    if (osk->ops.log) osk->ops.log(line);
}

int pw_osk_ps5_open(PwOskPs5 *osk, const PwOskPs5Ops *ops)
{
    char line[96];
    if (!osk || !ops || !ops->common_dialog_init || !ops->load_module || !ops->foreground_user || !ops->dialog_init ||
        !ops->dialog_status || !ops->dialog_result || !ops->dialog_term)
        return -22;
    memset(osk, 0, sizeof(*osk));
    osk->ops = *ops;
    /* The dialog family's start-up is tried and logged, and not required: a firmware that has it started already, or
     * needs no call, still shows the dialog; what is required is the module and its calls. */
    const int common = ops->common_dialog_init();
    const int module = ops->load_module();
    osk->ready = module >= 0;
    snprintf(line, sizeof(line), "keyboard common=%#x module=%#x ready=%d", (unsigned)common, (unsigned)module, osk->ready);
    say(osk, line);
    return osk->ready ? 0 : -19;
}

int pw_osk_ps5_show(PwOskPs5 *osk, const char *title)
{
    char line[96];
    int32_t user = -1;
    if (!osk || !osk->ready || osk->showing) return -16;
    if (osk->ops.foreground_user(&user) < 0) return -19;
    memset(osk->text, 0, sizeof(osk->text));
    pw_osk_ps5_utf16(title ? title : "", osk->title, PW_OSK_PS5_TITLE);
    ImeParam param;
    memset(&param, 0, sizeof(param));
    param.user_id = user;
    param.enter_label = ENTER_LABEL_DEFAULT;
    param.max_text_length = PW_OSK_PS5_TEXT - 1;
    param.input_text_buffer = osk->text;
    param.horizontal_alignment = param.vertical_alignment = 1;
    param.title = osk->title;
    const int rc = osk->ops.dialog_init(&param);
    osk->showing = rc == 0;
    osk->polls = 0;
    if (osk->showing) osk->shown++;
    snprintf(line, sizeof(line), "keyboard shown=%d init=%#x", osk->showing, (unsigned)rc);
    say(osk, line);
    return osk->showing ? 0 : -5;
}

static int finish(PwOskPs5 *osk, int answer)
{
    osk->showing = 0;
    (void)osk->ops.dialog_term();
    return answer;
}

void pw_osk_ps5_close(PwOskPs5 *osk)
{
    if (!osk || !osk->showing) return;
    if (osk->ops.dialog_abort) (void)osk->ops.dialog_abort();
    say(osk, "keyboard ended by=title");
    (void)finish(osk, 2);
}

int pw_osk_ps5_poll(PwOskPs5 *osk, char *text, size_t capacity)
{
    int32_t outcome = -1;
    if (!osk || !text || !capacity) return -22;
    text[0] = '\0';
    if (!osk->showing) return -1;
    const int status = osk->ops.dialog_status();
    osk->polls++;
    if (status == PW_OSK_PS5_RUNNING) return 0;
    if (status != PW_OSK_PS5_FINISHED) {
        /* none: early on it may not be drawn yet; past the grace it is gone and nothing was typed */
        if (osk->polls < PW_OSK_PS5_GRACE_POLLS) return 0;
        say(osk, "keyboard ended status=none");
        return finish(osk, 2);
    }
    if (osk->ops.dialog_result(&outcome) >= 0 && outcome == PW_OSK_PS5_OK) {
        pw_osk_ps5_utf8(osk->text, text, capacity);
        osk->accepted++;
        (void)osk->ops.dialog_term();
        osk->showing = 0;
        return text[0] ? 1 : 2;
    }
    osk->cancelled++;
    return finish(osk, 2);
}

#ifdef __PROSPERO__
/* The system's libraries by name, as native/pw_usb_input_ps5.c does for the keyboard and mouse: the SDK has no link stub
 * for libSceCommonDialog, and a link stub for libSceImeDialog would make the loader ask for it by path at the title's start
 * (a title may not load these by path: 0x80020063). The IME dialog module is loaded by its sysmodule id, 0x0096; its handle
 * is the system's, found by that id or its internal form, and every call is looked up on it. sceCommonDialogInitialize
 * is looked up on that module first, then in libSceCommonDialog as a module of its own. Anything not found is
 * "unavailable": pw_osk_ps5_open logs the state once and the title leaves the pad alone. */
int sceSysmoduleLoadModule(uint16_t id);
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *handle);
int sceKernelDlsym(int32_t handle, const char *symbol, void **address);
int32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, const void *option,
                                 int *result);
int sceUserServiceGetInitialUser(int32_t *user_id);

typedef struct { int32_t outcome; int8_t reserved[12]; } ImeResult;
_Static_assert(sizeof(ImeResult) == 16, "unexpected IME dialog result layout");

enum { IME_DIALOG_MODULE = 0x0096 };
static struct {
    int tried, found;
    int32_t handle;
    int (*common)(void);
    int (*init)(const void *, const void *);
    int (*status)(void);
    int (*result)(void *);
    int (*term)(void);
    int (*abort)(void);
} ime = {.handle = -1};

static int32_t sysmodule_handle(uint16_t id)
{
    int32_t handle = -1;
    if (!sceSysmoduleGetModuleHandleInternal(id, &handle) && handle >= 0) return handle;
    handle = -1;
    if (!sceSysmoduleGetModuleHandleInternal(0x80000000u | id, &handle) && handle >= 0) return handle;
    return -1;
}

/* Once: load the module and look every call up. 1 when all were found. */
static int ime_resolve(void)
{
    static const char *const paths[] = {"libSceCommonDialog.sprx", "/system/common/lib/libSceCommonDialog.sprx"};
    if (ime.tried) return ime.found;
    ime.tried = 1;
    (void)sceSysmoduleLoadModule(IME_DIALOG_MODULE);
    ime.handle = sysmodule_handle(IME_DIALOG_MODULE);
    if (ime.handle < 0) return 0;
    if (sceKernelDlsym(ime.handle, "sceImeDialogInit", (void **)&ime.init) || !ime.init ||
        sceKernelDlsym(ime.handle, "sceImeDialogGetStatus", (void **)&ime.status) || !ime.status ||
        sceKernelDlsym(ime.handle, "sceImeDialogGetResult", (void **)&ime.result) || !ime.result ||
        sceKernelDlsym(ime.handle, "sceImeDialogTerm", (void **)&ime.term) || !ime.term)
        return 0;
    if (sceKernelDlsym(ime.handle, "sceImeDialogAbort", (void **)&ime.abort)) ime.abort = 0;   /* optional */
    if (sceKernelDlsym(ime.handle, "sceCommonDialogInitialize", (void **)&ime.common)) ime.common = 0;
    for (size_t i = 0; !ime.common && i < sizeof(paths) / sizeof(paths[0]); i++) {
        int result = 0;
        const int32_t module = sceKernelLoadStartModule(paths[i], 0, 0, 0, 0, &result);
        if (module >= 0 && sceKernelDlsym(module, "sceCommonDialogInitialize", (void **)&ime.common)) ime.common = 0;
    }
    ime.found = 1;   /* the common dialog's start-up is optional (pw_osk_ps5_open) */
    return 1;
}

static int system_common_init(void) { return ime_resolve() && ime.common ? ime.common() : -2; }
static int system_load(void) { return ime_resolve() ? 0 : -2; }
static int system_user(int32_t *user) { return sceUserServiceGetInitialUser(user); }
static int system_init(const void *param) { return ime.init(param, 0); }
static int system_status(void) { return ime.status(); }
static int system_term(void) { return ime.term(); }
static int system_abort(void) { return ime.abort ? ime.abort() : 0; }
static int system_result(int32_t *outcome)
{
    ImeResult result;
    memset(&result, 0, sizeof(result));
    const int rc = ime.result(&result);
    *outcome = result.outcome;
    return rc;
}

const PwOskPs5Ops *pw_osk_ps5_system_ops(void)
{
    static const PwOskPs5Ops ops = {
        system_common_init, system_load, system_user, system_init, system_status, system_result, system_term, system_abort, 0,
    };
    return &ops;
}
#else
const PwOskPs5Ops *pw_osk_ps5_system_ops(void) { return 0; }
#endif
