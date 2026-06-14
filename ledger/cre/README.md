# Chainlink CRE + Confidential AI Attester — private yield allocation

This is the Chainlink half of Lunave: a private DeFi yield agent custodied by a
Ledger. It targets two ETHGlobal NYC 2026 Chainlink prizes with **one pipeline**:

- **Best usage of Chainlink Confidential AI Attester** — the allocation inference
  runs inside a TEE on the user's PRIVATE financial profile.
- **Best workflow with CRE** — a CRE workflow turns that attested inference into a
  DON-signed on-chain report that gates the Execution Account.

```
Private profile (goals + capital + Unlink-pool balance)        sensitive
        │
        ▼   POST /v1/inference   (Authorization: Bearer <key from Chainlink desk>)
Chainlink Confidential AI Attester  ──  LLM runs INSIDE A TEE (AWS Nitro)
        │   output = allocation JSON  +  response_digest (SHA-256 provenance)
        ▼   cre_callback → CRE HTTP-trigger
CRE workflow  (yield-allocation-workflow/main.ts)
        │   transcriptHash = response_digest
        │   ABI-encode (user, vaults[], bps[], blendedApyBps, approved, transcriptHash, inferenceId)
        │   runtime.report(...)   ← DON-signed
        ▼   writeReport → KeystoneForwarder
AllocationGate.onReport   (contracts/AllocationGate.sol, Base Sepolia 84532)
        │   stores the DON-attested allocation, onlyForwarder
        ▼
Execution Account  only deploys / rebalances WITHIN the attested vaults + weights
```

Everything is on **Base Sepolia** (`ethereum-testnet-sepolia-base-1`, chainId 84532)
— the same chain as the Unlink vaults and the Execution Account.

## Deployed (Base Sepolia, chainId 84532)

| What | Address / tx |
| --- | --- |
| **AllocationGate** | [`0xaf73bc5f7e53f58502443af04756e175278ffcf1`](https://sepolia.basescan.org/address/0xaf73bc5f7e53f58502443af04756e175278ffcf1) |
| deploy tx | [`0xb355ef03…`](https://sepolia.basescan.org/tx/0xb355ef0302445df569351a4e311394d1f936c3ba4192da87c11b22d661e5abf6) |
| onReport tx (attested allocation) | [`0x80a6eb38…`](https://sepolia.basescan.org/tx/0x80a6eb38265b62144c869937ee5383603c85566a96386b387c7c771747b7d69d) |

`scripts/deploy-gate.mjs` deploys the gate and tests it end-to-end: it encodes a
report EXACTLY as the CRE workflow does, from the real local-attester inference
output, delivers it via `onReport`, then reads `approvedAllocation` /
`isApproved` / `getAllocationById` back and asserts they equal the attested
inference (vaults, weights, blended APY, transcriptHash, inference id). All checks
pass on-chain. (Deployed with the funding wallet as `forwarder` so the script can
stand in for the KeystoneForwarder; the live CRE path uses CRE's forwarder.)

```bash
node --env-file=.env ledger/cre/scripts/deploy-gate.mjs
```

## Layout

```
ledger/cre/
├── project.yaml                       CRE project (Base Sepolia RPC)
├── secrets.yaml                       maps INFERENCE_API_KEY → INFERENCE_API_KEY_VAR
├── .env.example                       CRE_ETH_PRIVATE_KEY, INFERENCE_API_KEY_VAR
├── foundry.toml
├── contracts/AllocationGate.sol       the on-chain consumer (onReport / onlyForwarder)
├── simulation/allocation-callback.json a canned Attester callback (for offline simulation)
├── simulation/inference-prompt.txt    the exact prompt the Attester is given
└── yield-allocation-workflow/
    ├── main.ts                        the CRE workflow (HTTP trigger → report → writeReport)
    ├── config.staging.json            consumerAddress, chainSelectorName, userAddress
    ├── workflow.yaml  package.json  tsconfig.json
```

The host-side client that calls the Attester directly (submit + poll) lives at
`ledger/host/confidential-ai.mjs`, wired into the web app's `/api/strategy/propose`.

## Running without a sandbox key — the local attester

The Confidential AI Attester key is distributed out of band to hackathon
participants. To let the whole pipeline run with no key, `ledger/host/local-attester.mjs`
is a local stand-in that exposes the **identical `/v1/inference` contract** (same
request body, same response shape with `output` + SHA-256 `request_digest` /
`response_digest`), backed by Mistral instead of a TEE. It is mounted on the web
app, so:

- the web app's `/api/strategy/propose` submits the private profile, polls, and
  gets the allocation + provenance digests — same code path as the real sandbox;
- its output feeds the CRE workflow unchanged. `simulation/local-attester-callback.json`
  is a real capture, verified end-to-end:

  ```bash
  cre workflow simulate yield-allocation-workflow --target staging-settings \
    --non-interactive --trigger-index 0 --http-payload ./simulation/local-attester-callback.json
  ```

This is **honestly labelled**: it is not a TEE, the inference runs on Mistral on
this host, and the digests are real SHA-256 over our own canonicalisation. To use
the genuine TEE attestation, set `CONFIDENTIAL_AI_API_KEY` (from the Chainlink
desk) — the client switches `baseUrl` to the real sandbox and everything else is
unchanged. `LOCAL_ATTESTER=0` disables the stand-in.

## Prerequisites

- CRE CLI: `curl -sSL https://app.chain.link/cre/install.sh | bash` (installs `~/.cre/bin/cre`)
- Bun ≥ 1.2.21, Foundry (`forge`)
- `cd yield-allocation-workflow && bun install`
- Auth (one time): `cre login` (browser) or `export CRE_API_KEY=<key from app.chain.link>`
- For the live inference: `INFERENCE_API_KEY_VAR` from the Chainlink desk

## 1. Simulate the workflow (offline — no key, no wallet)

This is what the "Best workflow with CRE" prize needs (simulation is enough; the
Chainlink team deploys it for you). Run from this directory:

```bash
cre workflow simulate yield-allocation-workflow \
  --non-interactive \
  --trigger-index 0 \
  --http-payload ./simulation/allocation-callback.json
```

It compiles the workflow to WASM, feeds it the canned Attester callback, parses
the allocation, computes the transcriptHash, ABI-encodes the report, and prints
the result. The on-chain write is skipped without `--broadcast`.

## 2. Deploy AllocationGate (Base Sepolia)

```bash
forge create contracts/AllocationGate.sol:AllocationGate --broadcast \
  --rpc-url https://base-sepolia-rpc.publicnode.com \
  --private-key $CRE_ETH_PRIVATE_KEY \
  --constructor-args <BASE_SEPOLIA_KEYSTONE_FORWARDER>
```

Set `consumerAddress` in `yield-allocation-workflow/config.staging.json` to the
deployed address, and `userAddress` to the Ledger/Unlink user you attest for.

## 3. End-to-end live (Attester → CRE → on-chain)

```bash
# terminal A: local HTTP-trigger server + broadcast the on-chain write
cre workflow simulate yield-allocation-workflow --broadcast    # listens on http://localhost:2000/trigger
# terminal B: expose it
ngrok http 2000                                                # → https://<id>.ngrok-free.dev
# terminal C: submit one confidential inference, callback to the CRE trigger
curl -s -X POST https://confidential-ai-dev-preview.cldev.cloud/v1/inference \
  -H "Authorization: Bearer $INFERENCE_API_KEY_VAR" -H "Content-Type: application/json" \
  -d '{ "model":"gemma4", "system_prompt":"...", "prompt":"<private profile>",
        "cre_callback":{"url":"https://<id>.ngrok-free.dev/trigger"} }'
```

The Attester runs the inference in its TEE and POSTs the result to the CRE
trigger; the workflow signs it into a report and writes it to AllocationGate.

## Notes on what is real

- The TEE inference response carries **SHA-256 digests, not a signature**. The
  cryptographic, on-chain-verifiable signature is the **CRE DON report**, verified
  by the KeystoneForwarder before `onReport` runs — the consumer trusts
  `msg.sender == forwarder`, it does not ecrecover an attestor key itself.
- The non-broadcast simulation needs no deployed contract or funded wallet.
- Without an Attester API key, the web app falls back to a local strategy proposer
  with a locally-signed attestation, clearly labelled as a preview in the UI.
