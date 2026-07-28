## ASCII diagram for a standard **ISO-TP (ISO 15765-2)** communication flow using two distinct addresses.

### Setup
*   **Sender (Client/External)**: Sends data to `RX_ID` (`0x100`). Listens for Flow Control/Response on `TX_ID` (`0x101`).
*   **Receiver (Server/Your App)**: Listens on `RX_ID` (`0x100`). Sends responses/Flow Control to `TX_ID` (`0x101`).

```ascii
Sender (External)                  CAN Bus (vcan0)                  Receiver (Your App)
      |                                     |                               |
      |                                     |                               |
      |  (1) First Frame (SF/FF)            |                               |
      |  ID: 0x100 (RX)                     |                               |
      |  Payload: [PCI][LenHigh][LenLow][Data...]                           |
      | ----------------------------------> |                               |
      |                                     |                               |
      |                                     |  (2) Receive First Frame      |
      |                                     |  Extract Total Length         |
      |                                     |  Prepare Buffer               |
      |                                     |                               |
      |  (3) Flow Control (FC)              |                               |
      |  ID: 0x101 (TX)                     |                               |
      |  [PCI_FC][BlockSz][STmin]           |                               |
      | <---------------------------------- |                               |
      |                                     |                               |
      |  (4) Consecutive Frame 1 (CF)       |                               |
      |  ID: 0x100 (RX)                     |                               |
      |  [PCI_CF][SequenceNum][Data]        |                               |
      | ----------------------------------> |                               |
      |                                     |  (5) Receive CF #1            |
      |                                     |  Copy to Buffer               |
      |                                     |                               |
      |  (6) Consecutive Frame N (CF)       |                               |
      |  ID: 0x100 (RX)                     |                               |
      |  [PCI_CF][SequenceNum][Data]        |                               |
      | ----------------------------------> |                               |
      |                                     |  (7) Receive CF #N            |
      |                                     |  Copy to Buffer               |
      |                                     |                               |
      |                                     |  (8) PDU Complete             |
      |                                     |  Return Payload to App        |
      |                                     |                               |
      |  (9) Response PDU (e.g., Echo)      |                               |
      |  ID: 0x101 (TX)                     |                               |
      |  [PCI][Data...]                     |                               |
      | <---------------------------------- |                               |
      |                                     |                               |
      |  (10) Receive Response              |                               |
      |  ID: 0x101 (TX)                     |                               |
      | ----------------------------------> |                               |
      |                                     |                               |
```

### Key Details

1.  **Directionality**:
    *   **Incoming to App**: Frames arrive on `0x100`.
    *   **Outgoing from App**: Frames leave on `0x101`.

2.  **First Frame (FF)**:
    *   Used for payloads > 7 bytes.
    *   Contains the total length of the message in bytes 1-2 (after the PCI byte).
    *   Carries the first 6 bytes of data (7 bytes total per CAN frame).

3.  **Flow Control (FC)**:
    *   Sent by the Receiver (App) back to the Sender.
    *   Tells the Sender how many consecutive frames to send before stopping (`Block Size`) and the time delay between frames (`STmin`).
    *   *Note*: If `AUTO_FLOW_CONTROL` is enabled in your `TpConfig`, the app sends this automatically.

4.  **Consecutive Frames (CF)**:
    *   Sent by the Sender.
    *   Contains the actual data payload.
    *   Includes a sequence number (0-14, wraps to 15) to ensure order and detect loss.
    *   The last CF has the **Last Frame** bit set (or is simply the last one received).

5.  **Single Frame (SF)**:
    *   For payloads <= 6 bytes.
    *   The PCI byte indicates the length (e.g., `0x03` for 3 bytes).
    *   No Flow Control needed.

6.  **Why Two Addresses?**
    *   If you used one address (e.g., `0x100` for both), the Sender would see its own FC and CF frames, potentially causing confusion or loops if not handled carefully by `CAN_RAW_RECV_OWN_MSGS`.
    *   Two addresses cleanly separate the "Request" stream (`0x100`) from the "Response/Flow Control" stream (`0x101`).