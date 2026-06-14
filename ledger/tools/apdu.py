import hid, time, sys
from ledgerblue.ledgerWrapper import wrapCommandAPDU, unwrapResponseAPDU
CH=0x0101
apdu=bytes.fromhex(sys.argv[1]); tmo=int(sys.argv[2]) if len(sys.argv)>2 else 120
# Pick the Ledger APDU HID interface (usage_page 0xffa0); fall back to any Ledger
# interface. Don't crash with IndexError when the device isn't presenting it
# (locked, mid app-switch, or no app open).
dev=None
for _attempt in range(16):  # retry ~8s: after an app switch (OpenPGP -> Unlink)
    devs=hid.enumerate(0x2c97,0)  # the device re-enumerates and is briefly absent
    cands=[d for d in devs if d.get('usage_page',0)==0xffa0] or [d for d in devs if d.get('path')]
    for d in cands:
        try: dev=hid.device(); dev.open_path(d['path']); break
        except Exception: dev=None; continue
    if dev: break
    time.sleep(0.5)
if not dev:
    print("NO_DEVICE (no Ledger HID interface — unlock the device and open the right app)"); sys.exit(0)
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
