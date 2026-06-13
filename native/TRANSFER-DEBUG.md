# Device-custodied transfer — open finding

## What works (proven on hardware, base-sepolia)
- The native Ledger app signs Unlink EdDSA-Poseidon **byte-exact** vs the SDK
  (`signMessage(sk, 42)` == vector 0 `S`; `verifySignature` returns `true` for
  device signatures).
- A full account is reconstructed host-side from the device exports
  (spending **public** key + viewing key) — `address`/`nullifyingKey` match
  `account.fromSeed` exactly. The spending private key never leaves the SE.
- The device account **registers** on the live backend.
- A real on-chain **deposit** (`depositWithApproval`) funds it — balance observed
  at 2 USDC, deposits `processed`.
- During `client.transfer` the device signs the prepared request and the
  signature **verifies locally and via the SDK's own `verifySignature`**.

## What fails
`client.transfer` from the device account ends `failed` **engine-side**, right
after a valid signature, with **no status progression** (`onStatus` never fires)
and **no error reason** exposed by `getTransactions`.

## Why it is NOT the device
A control run with a **pure SDK software account** (`account.fromSeed`), same
environment, same funding wallet, same recipient pattern:
- transfer status → **`processed`** (completed), balance 1 USDC → 0.5 USDC.

So the backend/relayer path works. The device signature is valid by every
available measure. The only variable is that the account's spending key is
device-custodied (signature-only, no host-side private key).

## Hypotheses left to check (need Unlink relayer logs)
1. The relayer/prover rejects the proof for a reason not surfaced to the client
   (e.g. an input-witness it expects beyond the EdDSA signature).
2. The device account's note set is in a bad state from earlier failed attempts
   (though deposits show `processed` and balance is non-zero).
3. A subtle mismatch in how the registered `spendingPublicKey` / `nullifyingKey`
   feed the transfer circuit vs a seed-derived account.

## Failed device-account transfer tx IDs (for relayer-log lookup)
- `e833c8eb-3dfb-4ee9-a6e5-750885939b47`
- `839f35a8-b887-415a-a343-9693e43bd878`

Device account address:
`unlink1qqjst0404jv4t887l02dtgyqj929lm3kuws44uvyapa5s4hsjzudlzg2czep8n6jdsu7x5ep4qgsr0xcmt5ulvm7femt5kk87mze3aar3xx90d`

Reproduce: `node --env-file=.env native/host/device-wallet-demo.mjs` (keep the
device unlocked and the Unlink app open to avoid the 0x5515 auto-lock).
