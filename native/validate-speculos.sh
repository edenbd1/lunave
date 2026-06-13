#!/usr/bin/env bash
# Build the Unlink BOLOS app, run it on Speculos, and check the on-device signature
# (seed-derived spending key) verifies under the reference @zk-kit/eddsa-poseidon.
set -e
cd "$(dirname "$0")/.."
IMG=ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder-lite:latest
echo "› build (Nano S Plus)…"
docker run --rm -v "$(pwd):/repo" $IMG bash -c "cd /repo/native/app && make -j TARGET=nanos2 >/dev/null 2>&1"
echo "› speculos…"
docker rm -f speculos >/dev/null 2>&1 || true
docker run --rm -d --name speculos -v "$(pwd)/native/app/bin:/app" -p 5001:5000 \
  ghcr.io/ledgerhq/speculos:latest --model nanosp --display headless --api-port 5000 /app/app.elf >/dev/null 2>&1
sleep 8
RESP=$(curl -s -m 15 -X POST localhost:5001/apdu -H 'Content-Type: application/json' -d '{"data":"e00500000100"}' | python3 -c "import sys,json;print(json.load(sys.stdin).get('data',''))")
docker rm -f speculos >/dev/null 2>&1 || true
node --input-type=module -e "
import * as S from './node_modules/@unlink-xyz/sdk/dist/eddsa-poseidon-blake-2b-2AP2O5KZ.js';
let r='$RESP'.toLowerCase().replace(/9000\$/,'');
const f=i=>BigInt('0x'+r.slice(i*64,(i+1)*64));
const A=[f(0),f(1)], sig={R8:[f(2),f(3)], S:f(4)};
const ok=S.verifySignature(42n, sig, A);
console.log(ok ? '✅ on-device signature verifies (seed-derived key in the SE)' : '❌ invalid');
process.exit(ok?0:1);
"
