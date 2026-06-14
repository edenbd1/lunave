# Ledger — Developer Experience Feedback (Lunave, ETHGlobal NY 2026)

**Project:** Lunave — a native BOLOS app on the Apex P that signs Unlink's
EdDSA-Poseidon (BabyJubJub) entirely inside the Secure Element, plus the OpenPGP
app used to custody the agent's strategy.

**Who:** a hackathon team building a *native* SE app (not a plugin, not a
clear-signing descriptor) — so we hit the full toolchain end to end.

---

## TL;DR

The developer ergonomics are genuinely excellent — **the logging, Speculos, NBGL,
the OpenPGP app, and clear-signing were a pleasure.** Iterating against the
emulator is fast and honest.

The single biggest friction was **the gap between "it runs in Speculos" and "it
runs on a real device."** Two things made that gap painful:

1. **Building & loading a custom app onto real hardware** — the build "recipes"
   (manifest + Makefile + SDK target + Docker image) and the sideload/install
   flow were hard to get right, and the failure messages were not obvious.
2. **Behavioral differences between the emulator and the chip** — code that
   passed 100% on Speculos broke on the Secure Element, mostly around the `cx_bn`
   bignum coprocessor. These cost us the most debugging time.

Everything below is meant as constructive detail. Net: we shipped, and we'd build
on Ledger again.

---

## 1. What was great (please don't change this)

- **Logging.** `PRINTF` over the Speculos console is immediate and readable. Being
  able to print intermediate `cx_bn` values mid-computation is how we debugged the
  crypto at all. This is top-tier.
- **Speculos.** Fast boot, the on-screen UI, scriptable APDU, no device needed for
  90% of the loop. Excellent for the inner dev cycle.
- **NBGL.** The high-level UI components (review screens, address display, status)
  are clean and the "use cases" API saved us a lot of UI plumbing.
- **The OpenPGP app.** We used it to encrypt/decrypt the agent's strategy (custody
  of the *rules*). `gpg` ↔ `scdaemon` ↔ device "just worked" once set up. Lovely
  that a stock app gave us a second custody primitive for free.
- **APDU model & docs.** The CLA/INS/P1/P2 conventions, the status words, and the
  transport docs are clear. Building our own protocol on top was straightforward.
- **hw-app-eth & clear-signing** were great where we used them.

---

## 2. The core pain — Speculos vs. installing on a real device

This is the feedback that matters most. Working in Speculos feels like one world;
getting the same app onto an Apex P felt like a *different* project.

### 2a. The build "recipes" (manifest / Makefile / SDK / Docker)

Getting a custom native app to *build for the device* (not just the emulator)
involved several moving parts that each had to be exactly right, with little
feedback when they weren't:

- **App manifest** (`ledger_app.toml` / app metadata): the relationship between the
  manifest, the Makefile variables, and what the loader expects was not obvious.
  Small mismatches (app name, flags, icon format/size, API level) failed late.
- **Makefile variables**: `BOLOS_SDK`, the per-device target, `APPNAME`,
  `APPVERSION`, the `APP_LOAD_PARAMS` (flags, derivation paths, curves) — the set
  that must be declared, and *why*, was scattered. We pieced it together from
  multiple sources rather than one canonical reference.
- **SDK target per device**: selecting the **Apex P** target (vs Stax/Flex/Nano)
  and getting the matching SDK / API level right was trial and error. A clear
  "which SDK + which API level for which device, and how they map to Speculos"
  matrix would have saved hours.
- **Docker build image** (`ledger-app-builder`): which tag matches which SDK/device
  generation wasn't obvious, and a mismatch produced confusing build or load
  errors rather than "wrong image for this target."
- **Declared permissions / derivation paths / curves**: the app must declare the
  curves and BIP32 paths it will use. Getting these declarations to match what the
  code actually calls (we use `CX_CURVE_256K1` to derive, then BabyJubJub math in
  software) was non-obvious, and a mismatch only surfaced at load/run time.

**Ask:** one **end-to-end "new native app for Apex P" walkthrough** — manifest +
Makefile + SDK/API level + Docker tag + build + load — as a single canonical page,
with the exact failure messages each misconfig produces.

### 2b. Sideloading / installing the custom app

Once it builds, getting it onto the device was its own learning curve:

- **Developer mode / loader**: entering the right state, and the relationship
  between Ledger Live, "My Ledger," developer mode, and a raw `loadApp` was unclear
  for a custom (unsigned, not-in-catalog) app.
- **Custom CA / "app not genuine"**: a self-built app isn't signed by Ledger, so we
  had to understand the custom-CA / secure-channel path and accept the "not
  genuine" warnings. The docs exist but are spread out; a single "here is how you
  load YOUR app that Ledger has not signed, and what warnings are expected" page
  would help.
- **`ledgerblue` / Python loader**: works, but error reporting on a failed load
  (wrong target, wrong flags, size, API level) is cryptic. We frequently couldn't
  tell *which* of the build recipes was wrong from the load error alone.
- **Install ≠ emulator**: things like flash/RAM budget, app size, and icon
  constraints only bite on the real install, not in Speculos.

### 2c. Behavioral differences: emulator passes, device fails

This was the most expensive category — **green in Speculos, red on the chip.**
Almost all of it was the `cx_bn` bignum coprocessor on the Secure Element:

- **Results not fully reduced.** `cx_bn_mod_add` / `cx_bn_mod_mul` can leave the
  result in `[m, 2m)` instead of `[0, m)`. On Speculos our values were fine; on the
  device, downstream operations that assumed a fully-reduced operand silently
  produced wrong points. Fix: an explicit `cx_bn_reduce` after the affected ops.
- **Operands must be `< modulus`.** Some ops require strictly-reduced inputs; a
  value in `[m, 2m)` (see above) fed into the next op compounded the error.
- **In-place square is broken.** `cx_bn_mod_mul(r, a, a)` with `r == a == b` does
  not behave. We had to route squarings through a temporary (`pow5` uses a scratch
  register to avoid `r == a == b`).
- **`r == b` aliasing is broken.** Reusing the output register as the second
  operand misbehaves. We rewrote our add/double formulas to never alias `r` with
  `b`, using explicit temporaries — which also forced a fixed register file.
- **`cx_bn_init` needs a RAM source.** It would not initialize from a flash
  constant on the SE; we `memcpy` every constant (Poseidon round constants, MDS
  matrix) flash→RAM before `cx_bn_init`. On Speculos this distinction didn't exist.
- **Watchdog / heartbeat.** Long computations (our scalar mul is ~26 s) needed
  `io_seproxyhal_io_heartbeat()` inside the loop on the device; the emulator was
  more forgiving about long uninterrupted work.
- **BN pool pressure.** The SE bignum pool is small and unforgiving; we had to
  pre-allocate a **fixed register file** (6 persistent + 17 scratch) so it never
  grows mid-computation. Speculos tolerated a looser allocation style.

None of these are documented near `cx_bn`'s API. Each was found by bisecting
host-reference vs device output. We ended up validating the SE signer **1:1
against a GMP host port** (9/9 test vectors) precisely *because* we couldn't trust
"it passed in Speculos."

**Ask:** a **"cx_bn on the Secure Element — pitfalls" page**: non-reduced results,
required-reduced operands, broken in-place square, broken `r == b` aliasing,
RAM-source requirement, and pool sizing. Even a short "known differences from the
emulator" list would have saved us days.

### 2d. Smaller real-device-only papercuts

- **USB re-enumeration on app switch.** Switching from the OpenPGP app to our app
  (we need both: OpenPGP decrypts the strategy, then our app signs) causes the
  device to re-enumerate its HID interface and be briefly absent. Our host APDU
  layer had to add a retry loop (~8 s) to survive the switch. A documented "expect
  re-enumeration when the active app changes" note would have helped.
- **`scdaemon` holds the device.** After an OpenPGP decrypt, `scdaemon` keeps the
  card, blocking our app's APDU transport. We had to `gpgconf --kill scdaemon` to
  release it. Non-obvious interaction between two Ledger apps on one device.
- **HID interface selection on macOS.** Picking the right usage page (`0xffa0`) and
  surviving an empty/locked enumeration without crashing took some hardening.

---

## 3. Speculos vs. device — quick reference of what differed

| Area | Speculos (emulator) | Apex P (Secure Element) |
|---|---|---|
| `cx_bn_mod_add/mul` reduction | results looked reduced | can land in `[m, 2m)` → need `cx_bn_reduce` |
| In-place square `mul(r,a,a)` | fine | broken — needs a temp |
| Output/operand aliasing `r==b` | fine | broken — needs a temp |
| `cx_bn_init` from flash constant | worked | needs a RAM copy |
| Long compute (no heartbeat) | tolerated | needs `io_heartbeat()` |
| BN pool growth | loose | small, needs a fixed register file |
| App-switch USB behavior | n/a | re-enumerates, briefly absent |
| Build/load correctness | mostly irrelevant | manifest/Makefile/SDK/Docker must all match |

---

## 4. Concrete suggestions (priority order)

1. **A `cx_bn`-on-SE pitfalls page** (non-reduction, required-reduced operands,
   broken in-place square, broken `r==b`, RAM-source `cx_bn_init`, pool sizing).
   This is the single highest-leverage doc you could add for native-crypto apps.
2. **A native SE crypto reference** — a worked example of a non-secp256k1 scheme
   (a Poseidon hash and/or an Edwards-curve scalar mul) using `cx_bn`, so teams
   doing ZK-friendly crypto (BabyJubJub, Poseidon, Pedersen) have a starting point.
3. **One canonical "Speculos → real Apex P" guide**: manifest + Makefile + SDK/API
   level + Docker tag + build + sideload of an *unsigned* app, with the expected
   warnings and the exact error each misconfig produces.
4. **Better load-time diagnostics** in `ledgerblue` / the loader: when a load fails,
   say *which* parameter (target, flags, size, API level, curve declaration) is
   wrong, not just that it failed.
5. **Document the app-switch re-enumeration and `scdaemon` interaction** for hosts
   that drive two Ledger apps in one flow.

---

## 5. Closing

To be clear: the *logging is top, Speculos is top, NBGL is top, and the OpenPGP +
clear-signing apps are top.* The friction was almost entirely in the **"leave the
emulator and put a custom native app on a real device"** step — both the build
recipes and the behavioral differences of the Secure Element's `cx_bn`. Once we
mapped those quirks, the chip did exactly what we needed and our signatures matched
our reference 1:1.

If even the `cx_bn`-on-SE pitfalls and a single "Speculos → real device" guide
existed, a hackathon team could go from zero to a custom native signer in a day
instead of a weekend. We'd happily build on Ledger again.
