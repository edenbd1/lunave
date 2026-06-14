// Deploy AllocationGate to Base Sepolia and exercise it end-to-end.
//
//   node --env-file=.env ledger/cre/scripts/deploy-gate.mjs
//
// Deploys with the funding wallet as the `forwarder` so this script can call
// onReport itself (standing in for the CRE KeystoneForwarder) and prove the gate:
// it encodes a report EXACTLY as the CRE workflow does, from the real
// local-attester inference output, calls onReport, then reads back the attested
// allocation. In the live CRE path the constructor forwarder is CRE's
// KeystoneForwarder on Base Sepolia and the workflow's writeReport delivers it.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import {
  createWalletClient, createPublicClient, http, getAddress, keccak256,
  encodeAbiParameters, parseAbiParameters, decodeEventLog,
} from "viem";
import { privateKeyToAccount } from "viem/accounts";
import { baseSepolia } from "viem/chains";

const HERE = dirname(fileURLToPath(import.meta.url));
const RPC = process.env.BASE_SEPOLIA_RPC || "https://base-sepolia-rpc.publicnode.com";
const USER = process.env.LEDGER_ETH_ADDRESS || "0x065dF3372c1f9f86f5cfC220db027da2A754fdbF";
const scan = (h) => `https://sepolia.basescan.org/tx/${h}`;
const ALLOCATION_ABI = "address user, address[] vaults, uint16[] bps, uint16 blendedApyBps, bool approved, bytes32 transcriptHash, string inferenceId";

const pk = (process.env.FUNDING_PRIVATE_KEY || "").replace(/^0x/, "");
if (!pk) throw new Error("FUNDING_PRIVATE_KEY missing (run with --env-file=.env)");
const account = privateKeyToAccount(`0x${pk}`);
const wallet = createWalletClient({ account, chain: baseSepolia, transport: http(RPC) });
const pub = createPublicClient({ chain: baseSepolia, transport: http(RPC) });

const artifact = JSON.parse(readFileSync(join(HERE, "..", "out", "AllocationGate.sol", "AllocationGate.json"), "utf8"));
const abi = artifact.abi;
const bytecode = artifact.bytecode.object;

// Parse the captured local-attester callback into the report fields the workflow encodes.
function reportFromCallback() {
  const cb = JSON.parse(readFileSync(join(HERE, "..", "simulation", "local-attester-callback.json"), "utf8"));
  const fenced = cb.output.trim().match(/^```(?:[a-zA-Z0-9]+)?\s*([\s\S]*?)\s*```$/);
  const dec = JSON.parse(fenced ? fenced[1].trim() : cb.output);
  const entries = (dec.allocations || []).filter((a) => a.vault && a.bps > 0);
  const vaults = entries.map((a) => getAddress(a.vault));
  const bps = entries.map((a) => Math.round(Number(a.bps)));
  const transcriptHash = `0x${cb.resources[0].response_digest}`;
  return {
    user: getAddress(USER), vaults, bps,
    blendedApyBps: Math.round(Number(dec.blended_apy_bps || 0)),
    approved: dec.approved === true, transcriptHash, inferenceId: cb.id,
    decision: dec,
  };
}

async function main() {
  console.log(`Deployer / forwarder: ${account.address}`);

  // 1. Deploy AllocationGate(forwarder = funding wallet)
  console.log("\n[1] Deploying AllocationGate...");
  const deployHash = await wallet.deployContract({ abi, bytecode, args: [account.address] });
  console.log(`    deploy tx: ${scan(deployHash)}`);
  const rc = await pub.waitForTransactionReceipt({ hash: deployHash });
  const gate = rc.contractAddress;
  console.log(`    ✓ AllocationGate @ ${gate}`);

  // 2. Build the report exactly as the CRE workflow does, from the real inference
  const r = reportFromCallback();
  const allocationsHash = keccak256(encodeAbiParameters(parseAbiParameters("address[], uint16[]"), [r.vaults, r.bps]));
  console.log(`\n[2] Report (from local-attester inference ${r.inferenceId}):`);
  console.log(`    approved=${r.approved} risk=${r.decision.risk_level} blendedApy=${r.blendedApyBps}bps`);
  console.log(`    ${r.bps.map((b, i) => `${b}bps ${r.vaults[i]}`).join("\n    ")}`);
  console.log(`    transcriptHash=${r.transcriptHash}`);
  console.log(`    allocationsHash=${allocationsHash}`);
  const report = encodeAbiParameters(parseAbiParameters(ALLOCATION_ABI),
    [r.user, r.vaults, r.bps, r.blendedApyBps, r.approved, r.transcriptHash, r.inferenceId]);

  // 3. Deliver the report through onReport (we are the forwarder)
  console.log(`\n[3] Calling onReport (as the forwarder)...`);
  const onHash = await wallet.writeContract({ address: gate, abi, functionName: "onReport", args: ["0x", report] });
  console.log(`    onReport tx: ${scan(onHash)}`);
  const onRc = await pub.waitForTransactionReceipt({ hash: onHash });
  const evt = onRc.logs.map((l) => { try { return decodeEventLog({ abi, data: l.data, topics: l.topics }); } catch { return null; } }).find(Boolean);
  console.log(`    ✓ event ${evt?.eventName}: approved=${evt?.args?.approved} blendedApyBps=${evt?.args?.blendedApyBps}`);

  // 4. Read the gate back — the Execution Account would read these before acting
  console.log(`\n[4] Reading the attested allocation back on-chain...`);
  const [approved, vaults, bps, blendedApyBps, transcriptHash] = await pub.readContract({ address: gate, abi, functionName: "approvedAllocation", args: [r.user] });
  const isApproved = await pub.readContract({ address: gate, abi, functionName: "isApproved", args: [r.user] });
  const byId = await pub.readContract({ address: gate, abi, functionName: "getAllocationById", args: [r.inferenceId] });
  console.log(`    approvedAllocation(user): approved=${approved} blendedApyBps=${blendedApyBps}`);
  console.log(`      vaults=${vaults.join(", ")}`);
  console.log(`      bps=${bps.join(", ")}  transcriptHash=${transcriptHash}`);
  console.log(`    isApproved(user)=${isApproved}`);
  console.log(`    getAllocationById(${r.inferenceId.slice(0, 8)}…): user=${byId.user} ts=${byId.timestamp}`);

  // 5. Assertions
  const ok =
    approved === r.approved &&
    isApproved === r.approved &&
    vaults.length === r.vaults.length &&
    vaults.every((v, i) => getAddress(v) === r.vaults[i]) &&
    bps.every((b, i) => Number(b) === r.bps[i]) &&
    Number(blendedApyBps) === r.blendedApyBps &&
    transcriptHash.toLowerCase() === r.transcriptHash.toLowerCase() &&
    byId.inferenceId === r.inferenceId;
  console.log(`\n[5] ${ok ? "✓ ALL CHECKS PASSED" : "✗ MISMATCH"} — on-chain allocation == attested inference`);
  console.log(`\nAllocationGate: https://sepolia.basescan.org/address/${gate}`);
  console.log(`Set consumerAddress in config.staging.json to: ${gate}`);
  if (!ok) process.exit(1);
}

main().catch((e) => { console.error(e); process.exit(1); });
