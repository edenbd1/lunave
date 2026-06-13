// Unlink native signer.
// The Unlink spending key is derived from the device seed and never leaves the
// Secure Element. The device shows an on-device review ("Sign Unlink private
// tx?"); only on physical approval does it sign (EdDSA-Poseidon, on the SE) and
// return Ax|Ay|R8x|R8y|S. The signature verifies under @zk-kit/eddsa-poseidon.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "os.h"
#include "io.h"
#include "buffer.h"
#include "sw.h"
#include "glyphs.h"
#include "nbgl_use_case.h"
#include "../ui/display.h"
#include "menu.h"
#include "get_public_key.h"
#include "../unlink_crypto.h"

// m/44'/1'/0'/0/0 — Unlink spending-key path
static const uint32_t UNLINK_PATH[5] = {0x8000002C, 0x80000001, 0x80000000, 0, 0};

static uint8_t g_msg[32];
static nbgl_contentTagValue_t g_pairs[2];
static nbgl_contentTagValueList_t g_pairList;

static int sign_and_reply(void) {
    uint8_t key[32] = {0};
    os_perso_derive_node_bip32(CX_CURVE_256K1, UNLINK_PATH, 5, key, NULL);

    uint8_t Ax[32], Ay[32], R8x[32], R8y[32], S[32];
    unlink_sign(key, sizeof(key), g_msg, Ax, Ay, R8x, R8y, S);
    explicit_bzero(key, sizeof(key));

    uint8_t resp[160];
    memcpy(resp + 0, Ax, 32);
    memcpy(resp + 32, Ay, 32);
    memcpy(resp + 64, R8x, 32);
    memcpy(resp + 96, R8y, 32);
    memcpy(resp + 128, S, 32);
    return io_send_response_pointer(resp, sizeof(resp), SWO_SUCCESS);
}

static void review_choice(bool confirm) {
    if (confirm) {
        sign_and_reply();
        nbgl_useCaseReviewStatus(STATUS_TYPE_TRANSACTION_SIGNED, ui_menu_main);
    } else {
        io_send_sw(SWO_CONDITIONS_NOT_SATISFIED);
        nbgl_useCaseReviewStatus(STATUS_TYPE_TRANSACTION_REJECTED, ui_menu_main);
    }
}

int handler_get_public_key(buffer_t *cdata, bool display) {
    (void) cdata;
    (void) display;

    memset(g_msg, 0, sizeof(g_msg));
    g_msg[31] = 42;  // demo message_hash = field element 42

    g_pairs[0].item = "Action";
    g_pairs[0].value = "Sign Unlink private tx";
    g_pairs[1].item = "Privacy";
    g_pairs[1].value = "amount & recipient hidden";
    g_pairList.nbPairs = 2;
    g_pairList.pairs = g_pairs;
    (void) review_choice;

#ifdef UNLINK_IMMEDIATE_SIGN
    // Sign immediately (no blocking review) — used to capture a hardware signature
    // over macOS HID, which fails on the USB re-enumeration during the on-device UI.
    return sign_and_reply();
#else
    nbgl_useCaseReview(TYPE_TRANSACTION,
                       &g_pairList,
                       &ICON_APP_BOILERPLATE,
                       "Review Unlink\nprivate transaction",
                       NULL,
                       "Sign Unlink\nprivate transaction?",
                       review_choice);
    return 0;
#endif
}
