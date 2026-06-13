# Debugging the on-device Unlink signer (Apex P) — journey & findings

Goal: the BOLOS app must compute an Unlink EdDSA-Poseidon signature **on the
Secure Element** that verifies under `@zk-kit/eddsa-poseidon`. It is byte-exact
correct on Speculos but was wrong on the physical Apex P. This log records why.

## What's proven correct on the real hardware
- The app runs on the Apex P (`GET_APP_NAME` returns "Unlink" live).
- **A (public key) and R8 are byte-EXACT correct on the device** — i.e. the
  scalar multiplications, projective point addition, and modular inversion all
  work. Verified by exporting the seed-derived key and recomputing on the host.
- Only **S** was wrong → isolated to the Poseidon challenge `hm`, since A and R8
  don't depend on Poseidon.

## Why the SE differs from Speculos: in-place cx_bn aliasing
The same binary is correct on Speculos but wrong on the SE. Root cause: some
`cx_bn_*` calls behave differently on the real SE when the **output aliases the
inputs**, which Speculos tolerates.

- ✅ `cx_bn_mod_mul(r,a,b)` with `r==a` (different b): works (used all over the
  point arithmetic, A/R8 correct).
- ✅ `cx_bn_mod_mul(r,a,a)` with `r` distinct (squaring into a new reg): works.
- ❌ `cx_bn_mod_mul(r,a,a)` with **`r==a==a` (all three the same register)**:
  **broken on the SE.** This is exactly `pow5`'s in-place square `fmul(o,o,o)`.
  Fixing it (square into a second register) changed the device output — the
  first confirmed device-specific bug.
- `cx_bn_mod_add(r,a,b)` with `r==a`: works (verified — the defensive rewrites
  were no-ops).

## Why this matters for a generic SE crypto port
The SE's hardware modular engine is fast but has stricter aliasing rules than
the software/emulated path. Any in-place `r==a==b` (notably in-place squaring)
must use a separate output register. The fix is mechanical once located.

## Status
pow5 fixed → Poseidon output moved (progress) but still not equal to the host →
at least one more subtle divergence. Currently pinpointing it by exporting the
full Poseidon state after a single round and diffing element-by-element against
the host reference.
