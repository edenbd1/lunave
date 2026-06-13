// REVIEW_INTENT — show the human-readable transfer (amount + recipient) on the
// device and wait for a physical approval, BEFORE the transaction is prepared.
// This keeps the on-device tap OUTSIDE the engine's prepare->submit window, so
// the (slower) signature that follows never races the server timeout.
// cdata = "<amount>\0<recipient>"  (two NUL-separated strings).
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "io.h"
#include "buffer.h"
#include "sw.h"
#include "glyphs.h"
#include "nbgl_use_case.h"
#include "menu.h"
#include "../ui/display.h"
#include "review_intent.h"

static char g_amount[40];
static char g_recipient[96];
static nbgl_contentTagValue_t g_pairs[2];
static nbgl_contentTagValueList_t g_pairList;

static void review_cb(bool confirm) {
    if (confirm) {
        io_send_sw(SWO_SUCCESS);
        nbgl_useCaseReviewStatus(STATUS_TYPE_TRANSACTION_SIGNED, ui_menu_main);
    } else {
        io_send_sw(SWO_CONDITIONS_NOT_SATISFIED);
        nbgl_useCaseReviewStatus(STATUS_TYPE_TRANSACTION_REJECTED, ui_menu_main);
    }
}

int handler_review_intent(buffer_t *cdata) {
    if (!cdata || cdata->size < 2) {
        return io_send_sw(SWO_WRONG_DATA_LENGTH);
    }
    // split cdata on the NUL separator: amount | recipient
    const uint8_t *p = cdata->ptr;
    size_t n = cdata->size, sep = 0;
    while (sep < n && p[sep] != 0) sep++;
    size_t alen = sep;
    size_t rlen = (sep < n) ? (n - sep - 1) : 0;
    if (alen >= sizeof(g_amount)) alen = sizeof(g_amount) - 1;
    if (rlen >= sizeof(g_recipient)) rlen = sizeof(g_recipient) - 1;
    memcpy(g_amount, p, alen); g_amount[alen] = 0;
    memcpy(g_recipient, p + sep + 1, rlen); g_recipient[rlen] = 0;

    g_pairs[0].item = "Amount";
    g_pairs[0].value = g_amount;
    g_pairs[1].item = "To";
    g_pairs[1].value = g_recipient;
    g_pairList.nbPairs = 2;
    g_pairList.pairs = g_pairs;

    nbgl_useCaseReview(TYPE_TRANSACTION,
                       &g_pairList,
                       &ICON_APP_BOILERPLATE,
                       "Review Unlink\nprivate transfer",
                       NULL,
                       "Send this private\ntransfer?",
                       review_cb);
    return 0;
}
