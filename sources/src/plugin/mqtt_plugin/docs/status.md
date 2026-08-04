The shared library links cleanly with no undefined symbols against OpenSSL. Let's do a final sanity check confirming the exported plugin entry points and TLS symbols resolved correctly:Everything resolves correctly against libssl/libcrypto — the plugin builds and links as a proper shared library. Now let's clean up the scratch build artifacts and write up the summary of the analysis and changes.Now let's package the full modified plugin directory (plus the small TCPIP change) for download:## What was missing, and what I added

Reading `mqtt_driver.cpp`/`.hpp` and `mqtt_plugin.cpp`/`.hpp`, the existing code covers a clean happy-path: CONNECT/CONNACK, PUBLISH with QoS 0/1/2 acking, SUBSCRIBE, and RECEIVE. To actually exercise the *standard* things people do against a Mosquitto broker, several pieces were missing or outright broken:

1. **Username/password auth was broken, not just missing.** `connect()` hardcoded `flags = 0xC2`, which *claims* User Name + Password are present in the CONNECT payload but never wrote those fields — a malformed packet every single time. Mosquitto's `password_file` auth (the most common non-default Mosquitto setup) couldn't be tested at all.
2. **No Last Will and Testament.** LWT is the standard way to test client-offline detection — there was no config surface or wire encoding for it.
3. **No UNSUBSCRIBE.** You could subscribe but never cleanly tear a subscription down.
4. **No keepalive PINGREQ/PINGRESP.** `Config::keepAlive` was sent in CONNECT and then never honored — an idle `MQTT.SUBSCRIBE`/`MQTT.RECEIVE` session (the normal pattern for testing subscriptions) would eventually get dropped by the broker.
5. **Clean Session was hardcoded true.** No way to test persistent sessions / queued QoS 1-2 delivery across reconnects.
6. **TLS was cosmetic.** `setupTls()` built an `SSL_CTX` and loaded a CA file, but the `SSL*` was never attached to the socket — all reads/writes went straight over plain TCP through `TCPIP::tout_write/tout_read`. Testing against Mosquitto's TLS listener (port 8883) or mutual TLS simply didn't work.

## What I changed

- **`TCPIP`**: added a `nativeHandle()` getter so a layered driver can hand its fd to OpenSSL.
- **`MqttDriver`**: fixed the CONNECT flags/payload to properly encode username/password and an optional Will (topic/payload/QoS/retain), added `cleanSession`, added `unsubscribe()` and `ping()`, added automatic keepalive (an idle `receiveMessage()` now pings itself before its long wait), and rewired all I/O through `m_sendRaw()`/`m_recvRaw()` so `useTls` now genuinely runs `SSL_read`/`SSL_write` over the socket, including CA verification, hostname verification (`SSL_set1_host`), and mutual TLS (client cert/key).
- **`MqttPlugin`**: new `CONFIG` keys `u=`/`pw=` (auth), `wt=`/`wp=`/`wq=`/`wr=` (Will), `cs=` (clean session), `id=` (client id), plus matching INI keys; new `MQTT.UNSUBSCRIBE` and `MQTT.PING` commands; updated `INFO` text.

I verified all of this actually compiles and links — I set up the include paths, compiled every changed `.cpp` with `-fsyntax-only`, then did a full `-fPIC` object build and linked a real `libmqtt_plugin.so` against `libssl`/`libcrypto` with zero unresolved symbols.

Attached: the full modified `mqtt_plugin` directory + the one-line `uTcpip.hpp` change (`mqtt_plugin_extended.tar.gz`), and a unified diff of everything (`mqtt_plugin_changes.diff`) if you'd rather review/apply it against your tree directly.

Not in scope here, but worth knowing about if you keep going: QoS 2 duplicate-delivery dedup on the subscriber side, and MQTT 5 (this is 3.1.1 only, which is what Mosquitto still defaults to, so likely fine for now).