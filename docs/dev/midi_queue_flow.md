# MIDI Queue Flow

This note explains how MIDI output is queued and drained in the current codebase, with separate diagrams for USB and DIN.

The short version is:

- Both transports enqueue into the shared `MIDIQueueManagerDeviceState` / `MIDIQueueManager` policy layer.
- Both transports drain through the shared traversal API `pop_priority_lanes_with_transport_rules`.
- USB drains packed 32-bit USB-MIDI events into a transfer buffer.
- DIN drains a paced UART byte stream with clock-lane and message-fit constraints.

## Shared Queuing Model

```mermaid
flowchart TD
    A[Caller wants to send MIDI] --> B[Transport entry point]
    B --> C[MIDIQueueManagerDeviceState::enqueue_with_cc_policy]
    C --> D{CC message?}
    D -- yes --> E[Shared CC policy may coalesce and/or track debt]
    D -- no --> F[Push into transport priority lane]
    E --> F
    F --> G[Stored in per-priority ring buffer]
    G --> H[Transport rules decide dequeue behavior later]
```

## USB Flow

USB output is packet-based. Each queued item is a packed 32-bit USB-MIDI event.

```mermaid
flowchart TD
    U1[bufferMessage] --> U2{Selected lane full?}
    U2 -- yes --> U3[Try flushUSBMIDIOutput if idle]
    U2 -- no --> U4[queue_manager_.enqueue_with_cc_policy]
    U3 --> U4
    U4 --> U5[Packets wait in USB priority lanes]
    U5 --> U6[consumeSendData]
    U6 --> U7[Set max packets and CC packet budget]
    U7 --> U8[USBSendRules + USBSendContext]
    U8 --> U9[MIDIQueueManager::pop_priority_lanes_with_transport_rules]
    U9 --> U10{CC lane?}
    U10 -- yes --> U11[USBSendRules::handle_cc_lane]
    U11 --> U12{head is CC and budget allows?}
    U12 -- yes --> U13[fair CC pop in rules]
    U12 -- no --> U14[skip lane or direct head pop]
    U10 -- no --> U15[USBSendRules::pop_lane]
    U13 --> U16[memcpy packet into dataSendingNow]
    U14 --> U16
    U15 --> U16
    U16 --> U17[USB transfer buffer handed to driver]
```

## DIN Flow

DIN output is byte-based and time-based.

```mermaid
flowchart TD
    D1[enqueue_serial_message] --> D2[classify_message]
    D2 --> D3[queue_manager_.enqueue_with_cc_policy]
    D3 --> D4[Bytes wait in DIN priority lanes]
    D4 --> D5[flush_serial_output]
    D5 --> D6[update local Q8 pacing budget]
    D6 --> D7[measure UART space and CC UART budget]
    D7 --> D8[DINSendRules + DINSendContext]
    D8 --> D9[MIDIQueueManager::pop_priority_lanes_with_transport_rules]
    D9 --> D10{Lane is clock?}
    D10 -- yes --> D11[pop one byte]
    D10 -- no --> D12[validate framed message fits]
    D12 --> D13{CC lane and CC budget allow fair pop?}
    D13 -- yes --> D14[fair CC pop in rules]
    D13 -- no --> D15[pop whole framed message]
    D11 --> D16[bufferMIDIUart bytes]
    D14 --> D16
    D15 --> D16
    D16 --> D17[UART TX buffer receives bytes]
```

## USB vs DIN

| Topic | USB | DIN |
| --- | --- | --- |
| Transport unit | 4-byte USB-MIDI event packet | Raw MIDI bytes |
| Occupancy check API | `hasBufferedSendData()` (bool), `total_queued_messages()` (count) | `has_serial_data()` (bool) |
| Drain trigger | Build transfer buffer in `consumeSendData()` | Fill UART TX while pacing allows in `flush_serial_output()` |
| Budgeting | CC packet budget during transfer assembly | Q8 pacing budget + UART space + CC UART budget |
| Clock lane handling | No byte-level special lane handling | Clock lane emits one byte at a time |
| Framed-message fit checks | Not needed (already packetized) | Required for non-clock message pop |
| Shared fairness core | Same shared CC fairness + lane traversal | Same shared CC fairness + lane traversal |

## Reading The Code

If you want to follow this in code, the most useful entry points are:

- [src/deluge/io/midi/midi_device_manager.cpp](../../src/deluge/io/midi/midi_device_manager.cpp)
- [src/deluge/io/midi/midi_send_rules.h](../../src/deluge/io/midi/midi_send_rules.h)
- [src/deluge/io/midi/midi_queue_manager.h](../../src/deluge/io/midi/midi_queue_manager.h)

USB starts at `ConnectedUSBMIDIDevice::bufferMessage()` and drains through `consumeSendData()`.
DIN starts at `ConnectedDINMIDIDevice::enqueue_serial_message()` and drains through `flush_serial_output()`.

## Code-Centric Call Chains

### USB Call Chain

```mermaid
sequenceDiagram
    participant Caller
    participant USB as ConnectedUSBMIDIDevice
    participant MQ as MIDIQueueManagerDeviceState
    participant QM as MIDIQueueManager
    participant Rules as USBSendRules

    Caller->>USB: bufferMessage(fullMessage, priority)
    USB->>USB: total_queued_messages() and queue_count(priority)
    USB->>MQ: enqueue_with_cc_policy(priority, fullMessage, ...)
    MQ->>QM: enqueue_with_cc_policy(...)
    Note over QM: CC may coalesce or track debt before enqueue

    Caller->>USB: consumeSendData()
    USB->>USB: total_queued_messages()
    USB->>Rules: build USBSendContext(message_out, cc_budget)
    USB->>QM: pop_priority_lanes_with_transport_rules(device, rules, first, last, context)
    QM->>Rules: queue_count(device, priority)
    QM->>Rules: handle_cc_lane(device, priority, context)
    QM->>Rules: pop_lane(device, priority, context)
    Rules->>MQ: pop_fair_cc_candidate(...) when fair CC pop is selected
    USB-->>Caller: packet copied into dataSendingNow
```

### DIN Call Chain

```mermaid
sequenceDiagram
    participant Caller
    participant DIN as ConnectedDINMIDIDevice
    participant MQ as MIDIQueueManagerDeviceState
    participant QM as MIDIQueueManager
    participant Rules as DINSendRules

    Caller->>DIN: enqueue_serial_message(message)
    DIN->>QM: classify_message(message)
    DIN->>MQ: enqueue_with_cc_policy(priority, message, ...)
    MQ->>QM: enqueue_with_cc_policy(...)
    Note over QM: CC may coalesce or track debt before enqueue

    Caller->>DIN: flush_serial_output(now)
    DIN->>DIN: update local pacing budget and UART limits
    DIN->>Rules: build DINSendContext(out, budget, uart, max, ccBudget, poppedPriority)
    DIN->>QM: pop_priority_lanes_with_transport_rules(device, rules, first, last, context)
    QM->>Rules: queue_count(device, priority)
    QM->>Rules: handle_cc_lane(device, priority, context)
    QM->>Rules: pop_lane(device, priority, context)
    Rules->>MQ: pop_fair_cc_candidate(...) when fair CC pop is selected
    DIN-->>Caller: bytes emitted via bufferMIDIUart
```

## Step By Step

1. USB starts at `ConnectedUSBMIDIDevice::bufferMessage()` and drains via `consumeSendData()`.
2. DIN starts at `ConnectedDINMIDIDevice::enqueue_serial_message()` and drains via `flush_serial_output()`.
3. Both enqueue through `MIDIQueueManagerDeviceState::enqueue_with_cc_policy()`, forwarding to `MIDIQueueManager::enqueue_with_cc_policy()`.
4. Both dequeue through `MIDIQueueManager::pop_priority_lanes_with_transport_rules()`.
5. Transport-specific behavior lives in `USBSendRules` and `DINSendRules`, including CC-lane fair-pop decisions.
6. Both use shared CC fairness state (`pop_fair_cc_candidate`) but apply different transport constraints (USB packet burst limits vs DIN pacing/UART limits).
