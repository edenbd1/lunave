# Unlink x Ledger

Native Ledger custody for private DeFi. The Unlink spending key is derived inside
the Ledger Secure Element and never leaves it. The device itself signs Unlink
private transactions (EdDSA-Poseidon on BabyJubJub), and every move is approved
on screen with a physical tap.

You shield USDC from your Ledger into the private pool, send privately to another
Unlink address, and deposit into a DeFi vault through an Execution Account. Each
one is signed on the device.

## The idea

Unlink is an EVM privacy protocol with shielded balances and private transfers.
Its accounts sign with EdDSA-Poseidon on the BabyJubJub curve. Normally that key
sits in software. This project moves it into a real Ledger app on the Secure
Element, the way Ledger's own Aleo app works: the key is born in the chip, the
chip does the signing, nothing sensitive is exported.

## How it works

Three pieces.

### 1. The native Ledger app (`native/app`)

A BOLOS app for the Ledger Apex P. It implements the full Unlink signer on the
Secure Element:

* derives the spending key from the device seed, which never leaves the chip,
* computes the public key and the address material,
* signs EdDSA-Poseidon: BabyJubJub scalar multiplication, projective point
  addition, modular inverse, and the 68 round Poseidon hash, all on the chip's
  hardware bignum unit (`cx_bn`),
* shows a review screen and waits for a tap before it signs.

The Apex P has no native BabyJubJub support, so the whole curve and the Poseidon
hash were reimplemented on the chip. The signature is byte exact against the
`@zk-kit/eddsa-poseidon` reference. A fixed base comb brought a signature from
about 65s down to about 26s, which matters because the Unlink engine expires a
prepared transaction if you take too long to submit the signature.

APDU protocol (CLA `0xE0`):

| INS | Name | Purpose |
| --- | --- | --- |
| `0x05` | GET_PUBLIC_KEY | return the account public key `Ax`, `Ay` |
| `0x06` | SIGN_TX | sign a 32 byte message hash, return `Ax`, `Ay`, `R8x`, `R8y`, `S` |
| `0x08` | GET_VIEWING_KEY | return the viewing key (a read capability) |
| `0x09` | REVIEW_INTENT | show amount and full recipient, wait for approval |
| `0x0A` | CONNECT | pairing approval when a host opens a session |

### 2. The host bridge (`native/host`)

It rebuilds the full Unlink account from what the device exports, the spending
public key and the viewing key, without ever holding the spending private key.
It then exposes the device as the Unlink SDK signer, so `transfer` and `execute`
are signed on the Ledger. It talks to the Unlink app over a reliable HID poller,
and to the Ledger Ethereum app (for the deposit) over `hw-app-eth`.

### 3. The test front (`native/web`)

A small page on `localhost` that drives the whole flow.

## The account model

An Unlink account has four key parts: spending, viewing, nullifying, master. The
whole set derives from just the spending public key and the viewing key:

```
nullifyingKey   = poseidon1(viewingKey)
masterPublicKey = poseidon3(Ax, Ay, nullifyingKey)
address         = bech32m("unlink", masterPublicKey, viewingPublicKey)
```

So the device exports only its public spending key and its viewing key (a read
capability, shared the way Aleo shares view keys). The spending private key, the
one that authorizes spends, stays in the Secure Element. The host rebuilds the
account and registers it on the network, the device does the signing. This was
validated byte identical against `account.fromSeed`.

## The flow

| Step | What happens | Signed on |
| --- | --- | --- |
| Connect | Read or cache the device keys, build and register the account | Ledger, Unlink app |
| Shield | USDC on your Ledger ETH address enters the private pool via Permit2 | Ledger, Ethereum app |
| Send | Private transfer to another `unlink1...` address | Ledger, Unlink app |
| Vault | Withdraw privately into an Execution Account that deposits into an ERC-4626 vault | Ledger, Unlink app |

For the deposit, the one time Permit2 approval is clear signed on the device
(it reads "Approve USDC"). The send and the vault deposit are authorized inside
the Secure Element, and the recipient address is shown in full on the screen so
it can be verified.

## Repo layout

```
native/app        the BOLOS Ledger app (the on chip Unlink signer)
native/host       the host bridge (device account, signer, Ethereum app)
native/web        the localhost test front and its server
native/tools      verify.py (standalone signature verifier), apdu.py, eth_apdu.py
native/contracts  a minimal ERC-4626 demo vault
companion, server, src   the earlier OpenPGP custody web app (see below)
```

## Run

Install dependencies and set the environment:

```bash
npm install
cp .env.example .env
# set UNLINK_API_KEY, UNLINK_ENVIRONMENT, FUNDING_PRIVATE_KEY
```

Build and install the Ledger app (Apex P), then run the front:

```bash
# build the BOLOS app
cd native/app
docker run --rm -v "$PWD:/app" -w /app \
  ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder-lite:latest \
  bash -lc 'git init -q && git add -A && git commit -qm x && TARGET=apex_p make -j4'

# install it on the device (see native/SIDELOAD.md)

# run the device test front
cd ../..
npm run web        # http://localhost:8799
```

Open the front, connect, and try the three actions. The Unlink spending key
stays in the Secure Element the whole time.

## What is proven on hardware

Validated end to end on a real Ledger Apex P against the live Unlink network
(base-sepolia):

* the device signs Unlink EdDSA-Poseidon byte exact, in about 26s,
* the device custodied account registers and is funded by a real on chain deposit,
* USDC is shielded from the Ledger ETH address into the private pool,
* a private transfer to another Unlink address completes, signed in the chip,
* a private balance is deposited into an ERC-4626 vault through an Execution
  Account, signed in the chip.

See `native/SE-DEBUG.md` and `native/TRANSFER-DEBUG.md` for the engineering notes.

## The earlier OpenPGP path

Before the native app, the same goal was reached a different way: the Unlink seed
encrypted to the Ledger with OpenPGP, decrypted on the device, and a FIDO2 tap to
authorize each action. That web app still lives in `companion`, `server` and
`src`, and runs with `npm run dev:all`. The native app supersedes it: instead of
encrypting a software seed to the device, the key is generated and used inside the
Secure Element and never exists in software at all.

Built at ETHGlobal NYC 2026.
