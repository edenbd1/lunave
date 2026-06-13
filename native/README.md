# Unlink on Ledger — native signer (Aleo-style)

Goal: a native Ledger app (BOLOS) that holds the Unlink **spending key in the
Secure Element** and signs Unlink transactions on-device — like `app-aleo` does
for Aleo. The host app then talks to the device over APDU instead of using a
software signer, so the key never leaves the hardware.

## Status

✅ **Step 1 (the crux) — the C signer reproduces the SDK byte-exact.**
The Unlink signer is `@zk-kit/eddsa-poseidon` over BabyJubJub. Ported to C
(Blake2b + BabyJubJub + Poseidon t=6) and validated against 9 ground-truth
vectors from the SDK:

```
make test
# 9/9 vectors PASS  (pubkey + R8 + S all byte-exact)
```

The constants are extracted straight from the SDK's `poseidon-lite`
(`tools/dump-constants.cjs`) — no hand-copying, so the match is guaranteed.

### The algorithm (host-validated)
```
priv = ASCII bytes of decimal(spendingScalar)     # how the SDK feeds the key
h    = Blake2b(priv)                               # 64 bytes
s    = LE( pruneBuffer(h[0:32]) )                  # multiple of 8, bit254 set
A    = (s>>3)·Base8                                # public key
r    = LE( Blake2b(h[32:64] ‖ msgLE32) ) mod subOrder
R8   = r·Base8
hm   = Poseidon5(R8x,R8y,Ax,Ay,msg)               # t=6, RF=8, RP=60
S    = (r + hm·s) mod subOrder
sig  = {R8, S}
```

## Next steps (BOLOS app)
2. Fork `LedgerHQ/app-boilerplate`, swap GMP for the SE bignum (`cx_math_*`).
3. APDU `GET_PUBLIC_KEY` (→ unlink address) and `SIGN` (→ {R8,S}), on-device UI.
4. Run on **Speculos** (emulator) validated against the same vectors.
5. Sideload to the device (`ledgerctl install`, dev mode, no certification).
6. Wire the host app to sign via the device instead of the software signer.

See `PORT-C.md` for the full primitive list and the SE bignum mapping.
