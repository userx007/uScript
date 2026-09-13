#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>

// W5500 Pinout (Example for ESP32 + W5500)
#define W5500_SS 5
#define W5500_RST 18

WiFiServer server(5000);

void setup() {
  Serial.begin(115200);

  // Init WiFi
  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());

  // Init W5500 (Simplified SPI init)
  // ... (Standard W5500 SPI init code here) ...

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        // Read Command Packet
        // Header: Cmd(1) LenH(1) LenL(1)
        uint8_t cmd_buf[3];
        client.readBytes(cmd_buf, 3);

        uint8_t cmd_id = cmd_buf[0];
        uint16_t len = (cmd_buf[1] << 8) | cmd_buf[2];

        if (len > 255) {
          // Skip payload if too large for demo
          client.skip(len);
          continue;
        }

        // Read Payload
        std::vector<uint8_t> payload(len);
        if (len > 0) client.readBytes(payload.data(), len);

        // Process Command
        uint8_t resp_payload[256];
        uint8_t resp_len = 0;
        uint8_t status = 0x00; // Success

        if (cmd_id == 0x03) { // SEND
          // Payload[0] is Socket ID
          uint8_t sock = payload[0];
          // Write remaining bytes to W5500
          // W5500.writeTX(sock, &payload[1], len-1);
          // For demo, just echo back status
          resp_len = 0;
        }
        else if (cmd_id == 0x04) { // GET RX
          // Payload[0] is Socket ID
          uint8_t sock = payload[0];
          // uint16_t avail = W5500.getRX(sock);
          uint16_t avail = 0; // Mock
          resp_payload[0] = (avail >> 8) & 0xFF;
          resp_payload[1] = avail & 0xFF;
          resp_len = 2;
        }
        else if (cmd_id == 0x05) { // READ
          // Payload[0] is Socket ID
          uint8_t sock = payload[0];
          // uint16_t avail = W5500.getRX(sock);
          // W5500.readRX(sock, resp_payload, avail);
          uint16_t avail = 0; // Mock
          resp_len = avail;
        }

        // Send Response Packet
        client.write(0x00); // Status
        client.write(resp_len >> 8);
        client.write(resp_len & 0xFF);
        if (resp_len > 0) {
          client.write(resp_payload, resp_len);
        }
      }
    }
    client.stop();
  }
}




---

#if 0

#include "uW5500Net.hpp"

int main() {
    W5500Net driver;

    // Connect to the board
    if (driver.open("192.168.1.100") != Status::SUCCESS) {
        return 1;
    }

    // Read exactly 10 bytes from Socket 0
    uint8_t buf[10];
    auto result = driver.tout_read(2000,
                                   std::span(buf),
                                   ReadOptions{ReadMode::Exact},
                                   "0"); // xtra_params = "0" for Socket 0

    if (result.status == Status::SUCCESS) {
        // Process buf[0..result.bytes_read]
    }

    driver.close();
    return 0;
}

#endif