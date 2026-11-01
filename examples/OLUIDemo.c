/* OLUIDemo: a window described in HTML and CSS, in the GadTools theme,
 * with its events in C. Every widget GFOpenLayout UI has: a field, buttons,
 * a tree, tabs, a checkbox, radio buttons, a progress bar; and 0.3's: a
 * drop-down with groups, a password and a number field, a textarea, a
 * fieldset with its legend, labels that work their controls, a box that
 * scrolls, and tooltips.
 *
 *     OLUIDemo [FILE=markup.html] [CSS=style.css] [THEME=x.theme]
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"

static const char version[] = "$VER: OLUIDemo 0.2 (1.11.2026) AmiCradle Team";

static const char markup[] =
    "<row>"
    "  <label style='width:56px'>Find</label>"
    "  <input id=find placeholder='A folder name' style='flex:1'>"
    "  <button id=go>Find</button>"
    "</row>"
    "<row style='flex:1'>"
    "  <tree id=folders style='width:170px;height:auto'>"
    "    <item value=INBOX open>Inbox<item>Work</item><item>Family</item></item>"
    "    <item value=SENT>Sent</item>"
    "    <item value=ARCHIVE>Archive<item>2025</item><item>2026</item></item>"
    "  </tree>"
    "  <tabs id=tabs style='flex:1'>"
    "    <tab title=View>"
    "      <checkbox id=preview checked>Show the preview</checkbox>"
    "      <radio name=sort id=bydate value=date checked>Newest first</radio>"
    "      <radio name=sort id=byname value=sender>By sender</radio>"
    "      <label id=status>Nothing chosen yet.</label>"
    "    </tab>"
    "    <tab title=Form>"
    "      <fieldset><legend>Account</legend>"
    "        <row><label for=user style='width:64px'>Name</label><input id=user style='flex:1' title='Your name'></row>"
    "        <row><label for=pass style='width:64px'>Password</label>"
    "          <input type=password id=pass maxlength=32 style='flex:1'></row>"
    "        <row><label for=size style='width:64px'>Size</label>"
    "          <select id=size title='The text size'><option>Small<option selected>Normal<option>Large"
    "            <optgroup label=Bigger><option>Poster<option>Banner</optgroup></select>"
    "          <label for=copies style='width:56px;padding-left:8px'>Copies</label>"
    "          <spinner id=copies min=1 max=99 value=1></row>"
    "      </fieldset>"
    "      <textarea id=notes rows=4 placeholder='Notes: lines wrap at words' style='flex:1'></textarea>"
    "    </tab>"
    "    <tab title=Long>"
    "      <div style='height:120px;overflow:auto;border:1px solid #909090;padding:4px'>"
    "        <p>A box with a height and overflow:auto scrolls what it holds: the wheel, or Tab to a button in it.</p>"
    "        <p>GFOpenLayout lays out what it holds in full, and olui draws it moved by the scroll and cut to the box.</p>"
    "        <p>Each paragraph here is one more line to scroll through.</p>"
    "        <button id=deep title='A button in a box that scrolls'>A button down here</button>"
    "      </div>"
    "    </tab>"
    "    <tab title=About>"
    "      <h2>GFOpenLayout UI</h2>"
    "      <p>This window is HTML and CSS, laid out by GFOpenLayout and drawn in the theme.</p>"
    "    </tab>"
    "  </tabs>"
    "</row>"
    "<progress id=bar max=10></progress>"
    "<row style='justify-content:end'>"
    "  <button id=step title='Fills the bar a tenth more'>Step</button><button id=quit title='Closes the window'>Quit</button>"
    "</row>";

static int done;

static void on_quit(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ui; (void)ev; (void)userdata;
    done = 1;
}

static void on_step(olui *ui, const olui_event *ev, void *userdata)
{
    static int at;
    (void)ev; (void)userdata;
    at = (at + 1) % 11;
    olui_set_progress(ui, olui_find(ui, "bar"), at);
}

/* Whatever happens, in the status line */
static void on_any(olui *ui, const olui_event *ev, void *userdata)
{
    static const char *const names[] = { "", "click", "change", "submit", "select", "activate", "expand",
                                         "collapse", "focus", "cut", "copy", "paste" };
    char line[160];
    (void)userdata;
    if (ev->type == OLUI_FOCUS) return;
    snprintf(line, sizeof(line), "%s on %s%s%s", ev->type < OLUI_EVENT_COUNT ? names[ev->type] : "?",
             ev->id[0] ? ev->id : olui_tag(ev->el), ev->value ? ": " : "", ev->value ? ev->value : "");
    olui_set_text(ui, olui_find(ui, "status"), line);
}

/* The clipboard, as the program keeps it: a program for others would use
 * clipboard.device (iffparse.library's FTXT) here */
static char *clip;

static void on_clip(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    if (ev->type == OLUI_CUT || ev->type == OLUI_COPY) {
        size_t n = strlen(ev->value);
        char *s = (char *)malloc(n + 1);
        if (!s) return;
        memcpy(s, ev->value, n + 1);
        free(clip);
        clip = s;
    } else if (ev->type == OLUI_PASTE && clip) {
        olui_insert_text(ui, ev->el, clip);
    }
}

static void on_find(olui *ui, const olui_event *ev, void *userdata)
{
    olui_el *tree = olui_find(ui, "folders");
    (void)ev; (void)userdata;
    olui_list_add(ui, tree, NULL, olui_get_text(olui_find(ui, "find")), NULL);
}

static char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if ((s = (char *)malloc((size_t)n + 1)) && fread(s, 1, (size_t)n, f) == (size_t)n) s[n] = 0;
    else { free(s); s = NULL; }
    fclose(f);
    return s;
}

int main(void)
{
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"FILE/K,CSS/K,THEME/K", args, NULL);
    char *file_markup = NULL, *file_css = NULL, err[128];
    struct Window *win;
    olui *ui;
    olui_amiga *amiga;
    int rc = RETURN_FAIL;
    (void)version;

    if (rda) {
        if (args[0]) file_markup = load((const char *)args[0]);
        if (args[1]) file_css = load((const char *)args[1]);
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"GFOpenLayout UI", WA_Width, 560, WA_Height, 400,
                         WA_MinWidth, 300, WA_MinHeight, 220, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE,
                         WA_IDCMP, olui_amiga_idcmp(), TAG_DONE);
    if (!win) {
        PutStr((STRPTR)"OLUIDemo: no window\n");
        goto out;
    }
    ui = olui_new();
    amiga = ui ? olui_amiga_new(ui, win) : NULL;
    if (!amiga) {
        PutStr((STRPTR)"OLUIDemo: out of memory\n");
        goto free;
    }
    if (!olui_amiga_theme(amiga, args[2] ? (const char *)args[2] : NULL))
        PutStr((STRPTR)"OLUIDemo: no theme found, the built-in look\n");
    if (!olui_load(ui, file_markup ? file_markup : markup, file_css, err, sizeof(err))) {
        Printf((STRPTR)"OLUIDemo: %s\n", (ULONG)err);
        goto free;
    }
    olui_on(ui, NULL, 0, on_any, NULL);
    olui_on(ui, "quit", OLUI_CLICK, on_quit, NULL);
    olui_on(ui, "step", OLUI_CLICK, on_step, NULL);
    olui_on(ui, "go", OLUI_CLICK, on_find, NULL);
    olui_on(ui, "find", OLUI_SUBMIT, on_find, NULL);
    olui_on(ui, NULL, OLUI_CUT, on_clip, NULL);
    olui_on(ui, NULL, OLUI_COPY, on_clip, NULL);
    olui_on(ui, NULL, OLUI_PASTE, on_clip, NULL);
    if (!olui_amiga_layout(amiga)) {
        PutStr((STRPTR)"OLUIDemo: out of memory\n");
        goto free;
    }
    while (!done) {
        struct IntuiMessage *m;
        ULONG sig = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            int r = olui_amiga_event(amiga, m);
            ReplyMsg(&m->ExecMessage);
            if (r == OLUI_AMIGA_CLOSE) done = 1;
        }
        olui_amiga_draw(amiga);
    }
    rc = RETURN_OK;
free:
    olui_amiga_free(amiga);
    olui_free(ui);
    CloseWindow(win);
out:
    free(file_markup);
    free(file_css);
    free(clip);
    if (rda) FreeArgs(rda);
    return rc;
}
