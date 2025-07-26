#pip install python-daemon

# Runs the PTY bridge as a true daemon (no terminal, no user session).
# Saves the PTY device paths to /tmp/pty_bridge_ports.txt so you can access them later.
#
# Keeps running until manually stopped.
#   ps aux | grep pty_bridge
#   kill <PID>


import os
import pty
import select
import daemon

def create_pty_pair():
    master1, slave1 = pty.openpty()
    master2, slave2 = pty.openpty()
    slave_name1 = os.ttyname(slave1)
    slave_name2 = os.ttyname(slave2)
    with open("/tmp/pty_bridge_ports.txt", "w") as f:
        f.write(f"{slave_name1}\n{slave_name2}\n")
    return master1, master2

def bridge_ports(master1, master2):
    while True:
        rlist, _, _ = select.select([master1, master2], [], [])
        for src in rlist:
            data = os.read(src, 1024)
            dst = master2 if src == master1 else master1
            os.write(dst, data)

def run_daemon():
    with daemon.DaemonContext():
        m1, m2 = create_pty_pair()
        bridge_ports(m1, m2)

if __name__ == "__main__":
    run_daemon()
