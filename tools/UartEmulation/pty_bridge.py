import os
import pty
import select
import sys

def create_pty_pair():
    master1, slave1 = pty.openpty()
    master2, slave2 = pty.openpty()
    slave_name1 = os.ttyname(slave1)
    slave_name2 = os.ttyname(slave2)
    print(f"Virtual serial ports created:")
    print(f"  Port A: {slave_name1}")
    print(f"  Port B: {slave_name2}")
    return master1, master2

def bridge_ports(master1, master2):
    print("Bridging ports... Press Ctrl+C to exit.")
    try:
        while True:
            rlist, _, _ = select.select([master1, master2], [], [])
            for src in rlist:
                data = os.read(src, 1024)
                dst = master2 if src == master1 else master1
                os.write(dst, data)
    except KeyboardInterrupt:
        print("\nBridge terminated.")

if __name__ == "__main__":
    m1, m2 = create_pty_pair()
    bridge_ports(m1, m2)
