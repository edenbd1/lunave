// Unlink native signer.
// The Unlink spending key is derived from the device seed and never leaves the
// Secure Element. On GET_PUBLIC_KEY we derive it, sign a fixed message entirely
// on-device, and return Ax|Ay|R8x|R8y|S (160 bytes). The signature verifies
// against the returned public key under @zk-kit/eddsa-poseidon.
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "os.h"
#include "io.h"
#include "buffer.h"
#include "sw.h"
#include "get_public_key.h"
#include "../unlink_crypto.h"

// m/44'/1'/0'/0/0 — Unlink spending-key path (testnet coin type, allowed by app load params)
static const uint32_t UNLINK_PATH[5] = {0x8000002C, 0x80000001, 0x80000000, 0, 0};

int handler_get_public_key(buffer_t *cdata, bool display) {
    (void) cdata;
    (void) display;

    // Derive 32 bytes of key material from the device seed (never exported).
    uint8_t key[32] = {0};
    os_perso_derive_node_bip32(CX_CURVE_256K1, UNLINK_PATH, 5, key, NULL);

    uint8_t msg[32] = {0};
    msg[31] = 42;  // demo message_hash = field element 42 (big-endian)

    uint8_t Ax[32], Ay[32], R8x[32], R8y[32], S[32];
    unlink_sign(key, sizeof(key), msg, Ax, Ay, R8x, R8y, S);
    explicit_bzero(key, sizeof(key));

    uint8_t resp[160];
    memcpy(resp + 0, Ax, 32);
    memcpy(resp + 32, Ay, 32);
    memcpy(resp + 64, R8x, 32);
    memcpy(resp + 96, R8y, 32);
    memcpy(resp + 128, S, 32);

    return io_send_response_pointer(resp, sizeof(resp), SWO_SUCCESS);
}
