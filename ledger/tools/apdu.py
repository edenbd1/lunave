import hid, time, sys
from ledgerblue.ledgerWrapper import wrapCommandAPDU, unwrapResponseAPDU
CH=0x0101
apdu=bytes.fromhex(sys.argv[1]); tmo=int(sys.argv[2]) if len(sys.argv)>2 else 120
# Pick the Ledger APDU HID interface (usage_page 0xffa0); fall back to any Ledger
# interface. Don't crash with IndexError when the device isn't presenting it
# (locked, mid app-switch, or no app open).
devs=hid.enumerate(0x2c97,0)
cands=[d for d in devs if d.get('usage_page',0)==0xffa0] or [d for d in devs if d.get('path')]
if not cands:
    print("NO_DEVICE (no Ledger HID interface — unlock the device and open the right app)"); sys.exit(0)
dev=hid.device(); opened=False
for d in cands:
    try: dev.open_path(d['path']); opened=True; break
    except Exception: continue
if not opened:
    print("NO_DEVICE (could not open the Ledger — is the right app open?)"); sys.exit(0)
def rd(ms):
    try: return bytes(dev.read(64, ms))
    except Exception: return b""
while rd(120): pass
pk=wrapCommandAPDU(CH, apdu, 64)
for i in range(0,len(pk),64): dev.write(b'\x00'+pk[i:i+64])
buf=b""; t=time.time()
while time.time()-t<tmo:
    c=rd(800)
    if not c: continue
    buf+=c
    try: r=unwrapResponseAPDU(CH,buf,64)
    except Exception: r=None
    if r: print("RESP "+bytes(r).hex()); sys.exit(0)
print("NO_RESP after %.0fs"%(time.time()-t))
