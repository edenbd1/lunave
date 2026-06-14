# Vision: private AI managed yield, custodied by Ledger

Notes for later. The pieces below build on what already works in this repo
(device custody, shield, private transfer, vault deposit via an Execution
Account).

## The pitch

Private yield managed by AI, custodied by your Ledger. You define a strategy with
an AI co-pilot, you approve it on your Ledger (the only thing that can move
funds), and verifiable AI agents execute and rebalance the yield while the
strategy and the positions stay confidential.

## Architecture (6 pieces)

### 1. Strategy co-pilot (AI agent number 1, advisory)
You describe your goals (risk, tokens, target APY) in plain language. The agent
proposes a concrete strategy: allocations across vaults, rebalancing rules,
thresholds. It can sign nothing. There is already a base in `server/copilot.ts`
(Claude), to extend toward yield.

### 2. Ledger approval (the custody gate)
When you validate the strategy, the Ledger signs two things:
* the initial deposit (Unlink shield into a vault via an EA), already working,
* a strategy mandate: a signature over the approved parameters (thresholds,
  allowed vaults, limits) that bounds what the agents are allowed to do.

### 3. Execution agents (AI agents number 2 and up, autonomous)
They watch the positions and rebalance within the approved mandate. The key idea:
the Ledger authorizes the strategy once (a policy plus the initial allocation),
not every rebalance. This is exactly Unlink's Execution Account model: you
authorize an EA with a spending policy through the Ledger, and the agents drive
the EA within that limit, with no tap on every move.

### 4. OpenPGP on the Ledger (strategy confidentiality)
The strategy, and the agents' operational secrets, are encrypted with the Ledger
OpenPGP app, so only your Ledger can decrypt them. The agents work on encrypted
strategy data and the sensitive parameters stay confidential. There is already a
flow in `companion/gpg-custody.ts`.

### 5. Confidential AI attestation
The agents run inside an attested environment (a TEE) and produce an attestation
that proves "this exact agent ran this exact strategy on this exact data". You
can verify that the AI did what was agreed: no drift, no leak of the strategy.

### 6. Chainlink CRE (the agents' runtime)
The Chainlink Runtime Environment runs the execution workflow in a decentralized,
verifiable way: it watches conditions (APY, prices), triggers the attested AI
decision, and executes the rebalance through the EA. Automation, the data oracle,
and the compute, in one.

## End to end flow

```
1. You plus the AI co-pilot      ->  a concrete strategy
2. strategy encrypted (Ledger OpenPGP)            [confidential]
3. you approve on the Ledger     ->  sign the initial deposit plus the mandate
4. funds shielded                ->  initial vault via the EA      [already done]
5. Chainlink CRE watches         ->  attested AI decision -> rebalance via the EA
6. every move produces an attestation             [auditable]
```

## Status (what is built)

| Piece | Status | Where |
| --- | --- | --- |
| Strategy co-pilot (AI) | done | `host/yield-agent.mjs` (Mistral) |
| Ledger approval plus deposit | done | `web/server.mjs` `/api/strategy/deploy` |
| Vault via EA | done | `/api/execute`, `/api/rebalance` |
| EA mandate plus auto rebalance | done | `host/yield-bot.mjs`, `/api/agent/*` |
| OpenPGP mandate encryption | done | `host/mandate-seal.mjs` (Ledger OpenPGP) |
| Confidential AI attestation | done | `host/cre-attestation.mjs` |
| Chainlink CRE | workflow written | `cre/yield-strategy.workflow.ts` |

## How the last three bricks work

### Autonomous agent (brick 3)
`host/yield-bot.mjs` watches each vault's live APY and rebalances the position to
the best risk-adjusted vault when the edge clears the mandate threshold, capped
per vault, only among the mandate's allowed vaults. The mandate is approved ONCE
on the Ledger; from then on the agent moves funds with no tap per rebalance. This
is honest custody, not a bypass: every rebalance is still an Unlink spend signed
inside the Secure Element (the native app's immediate-sign path), so the spending
key never leaves the chip. Skipping the per-move review is exactly what the
once-approved mandate authorizes. Drive it with `/api/agent/start|stop|tick|status`.

### Confidential AI attestation (brick 5) + Chainlink CRE (brick 6)
The strategy proposal runs as a Chainlink CRE confidential workflow. The AI call
goes out over Confidential HTTP (the prompt — your goals and capital — stays
inside the TEE), the DON agrees on the result, and the workflow emits a signed
report: the AI Attestation. It is an EVM-encoded `keccak256`/`ecdsa` report that
binds the request and the allocation by digest, so a consumer (or the front) can
verify the allocation came from the attested confidential run without ever seeing
the private inputs. `host/cre-attestation.mjs` runs this locally (the report is
signed by a single attestor key, the stand-in for the DON's aggregated
signature); `cre/yield-strategy.workflow.ts` is the deployable workflow that runs
the same logic on the DON (`cre workflow deploy`, `MISTRAL_API_KEY` as a CRE
Secret).

### OpenPGP mandate (brick 4)
`host/mandate-seal.mjs` encrypts the approved mandate to the Ledger OpenPGP key
(`gpg --encrypt`), so the rules the agent obeys are themselves under hardware
custody: only the physical device can decrypt or rewrite them. Set up the OpenPGP
app on the Ledger, generate a card key, and point `LEDGER_PGP_RECIPIENT` at it;
with no card the mandate is kept unsealed so the demo still runs.

## Suggested MVP for a demo

A vertical slice: the co-pilot proposes a strategy, you approve it on the Ledger
(sign the deposit plus the mandate), one agent does a single rebalance (vault A to
vault B) through the EA, with the strategy encrypted via OpenPGP and a simple
attestation. Chainlink CRE and the full TEE come after.

Order to consider:
1. the strategy co-pilot (the AI that proposes, plus the Ledger mandate screen),
2. the rebalance via EA (vault A to vault B, autonomous),
3. the OpenPGP encryption of the strategy,
4. wiring Chainlink CRE first (the automation).
