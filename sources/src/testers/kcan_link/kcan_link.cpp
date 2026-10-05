// kcan_link - tiny command line front-end for the KCAN link layer (uKCanLink).
// Same effect as `ip link set canX type can bitrate N` / `ip link set canX up|down`, handy to check
// an adapter without starting the script engine.
//
//   kcan_link status  can0
//   kcan_link up      can0 [bitrate] [sample_point] [fd=<dbitrate>]     e.g. kcan_link up can0 500k 0.875
//   kcan_link down    can0
//   kcan_link restart can0
//
// up/down/restart need root or CAP_NET_ADMIN; status does not.
#include "kcan_parse.hpp"
#include "uKCanLink.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s status|up|down|restart <iface> [bitrate [sample_point]] [fd=<data_bitrate>]\n", argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    const std::string iface = argv[2];
    int rc = 0;

    if (cmd == "status") {
        kcan::LinkStatus st;
        rc = kcan::link_get_status(iface, st);
        if (rc == 0) {
            std::puts(kcan::format_status(iface, st).c_str());
        }
    } else if (cmd == "up") {
        kcan::LinkConfig cfg;
        int pos = 0;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            if (a.rfind("fd=", 0) == 0) {
                if (!kcan_parse::bitrate(a.substr(3), cfg.dataBitrate)) {
                    std::fprintf(stderr, "bad data bitrate '%s'\n", a.c_str() + 3);
                    return 2;
                }
                cfg.ctrlModeMask |= kcan::CTRLMODE_FD;
                cfg.ctrlModeFlags |= kcan::CTRLMODE_FD;
            } else if (pos == 0) {
                if (!kcan_parse::bitrate(a, cfg.bitrate)) {
                    std::fprintf(stderr, "bad bitrate '%s'\n", a.c_str());
                    return 2;
                }
                ++pos;
            } else if (pos == 1) {
                if (!kcan_parse::samplePoint(a, cfg.samplePoint)) {
                    std::fprintf(stderr, "bad sample point '%s'\n", a.c_str());
                    return 2;
                }
                ++pos;
            }
        }
        bool changed = false;
        rc = kcan::link_apply(iface, cfg, changed);
        if (rc == 0) {
            kcan::LinkStatus st;
            kcan::link_get_status(iface, st);
            std::printf("%s %s\n", changed ? "configured:" : "already as requested:", kcan::format_status(iface, st).c_str());
        }
    } else if (cmd == "down") {
        rc = kcan::link_set_up(iface, false);
    } else if (cmd == "restart") {
        rc = kcan::link_restart(iface);
    } else {
        std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
        return 2;
    }

    if (rc != 0) {
        std::fprintf(stderr, "%s %s: %s\n", cmd.c_str(), iface.c_str(), kcan::describe_errno(rc).c_str());
        return 1;
    }
    return 0;
}
