# MIDI Queue Flow

This note explains how MIDI output is queued and drained in the current codebase, with separate diagrams for USB and DIN.

The short version is:

- Both transports enqueue into the shared `MIDIQueueManagerState` / `MIDIQueueManager` policy layer.
- USB drains into 4-byte USB-MIDI packets and assembles a transfer buffer.
- DIN drains into a paced UART byte stream, where clock bytes and framed messages have different pop rules.

## Shared Queuing Model

```mermaid
flowchart TD
    A[Caller wants to send MIDI] --> B[Connected device class classifies message]
    B --> C[MIDIQueueManagerState::enqueue_with_cc_policy]
    C --> D{CC message?}
    D -- yes --> E[Shared CC policy may coalesce or track debt]
    D -- no --> F[Push into priority lane]
    E --> F
    F --> G[Lane stored in per-priority ring buffer]
    G --> H[Transport-specific drain later decides what actually goes out]
```

## USB Flow

USB output is packet-based. Each queued item is a packed 32-bit USB-MIDI event, and the drain code tries to build a short burst of packets for the next transfer.

```mermaid
flowchart TD
    U1[bufferMessage] --> U2{Queue too full?}
    U2 -- yes --> U3[Try flushUSBMIDIOutput if idle]
    U2 -- no --> U4[push_priority_message]
    U3 --> U4
    U4 --> U5[queue_manager_.enqueue_with_cc_policy]
    U5 --> U6[Packets wait in priority lanes]
    U6 --> U7[consumeSendData]
    U7 --> U8[Pick packet budget and CC packet budget]
    U8 --> U9[pop_priority_message]
    U9 --> U10[USBPriorityPopAdapter]
    U10 --> U11[pop_priority_lanes_with_cc_fairness]
    U11 --> U12{Current lane is CC?}
    U12 -- yes --> U13[handle_cc_lane]
    U13 --> U14{CC packet and budget allow fair pop?}
    U14 -- yes --> U15[pop_fair_queued_cc_message]
    U14 -- no --> U16[Skip lane or pop lane head]
    U12 -- no --> U17[pop lane head]
    U15 --> U18[Copy packet into dataSendingNow]
    U16 --> U18
    U17 --> U18
    U18 --> U19[USB transfer buffer is handed to driver]
```

```mermaid
sequenceDiagram
    participant Caller
    participant USB as ConnectedUSBMIDIDevice
    participant Queue as MIDIQueueManagerState/MIDIQueueManager
    participant Adapter as USBPriorityPopAdapter
    participant Driver as USB transfer

    Caller->>USB: bufferMessage(message, priority)
    USB->>Queue: enqueue_with_cc_policy(...)
    Note over Queue: CC messages can coalesce before they are stored
    USB->>USB: consumeSendData()
    USB->>USB: pop_priority_message()
    USB->>Adapter: pop_priority_lanes_with_cc_fairness(...)
    Adapter->>Queue: queue_count(priority)
    Adapter->>Queue: handle CC lane / pop lane
    Queue-->>USB: next packet
    USB->>Driver: memcpy into dataSendingNow
```

## DIN Flow

DIN output is byte-based and time-based. The queue still stores messages by priority, but the drain path must respect UART space, pacing budget, and special handling for the realtime clock lane.

```mermaid
flowchart TD
    D1[enqueue_serial_message] --> D2[classify_message]
    D2 --> D3[queue_manager_.enqueue_with_cc_policy]
    D3 --> D4[Bytes wait in priority lanes]
    D4 --> D5[flush_serial_output]
    D5 --> D6[update_serial_budget]
    D6 --> D7[Measure UART space and CC UART budget]
    D7 --> D8[pop_next_prioritized_bytes]
    D8 --> D9[DINPriorityPopAdapter]
    D9 --> D10[pop_priority_lanes_with_cc_fairness]
    D10 --> D11{Lane is clock?}
    D11 -- yes --> D12[pop one byte from clock lane]
    D11 -- no --> D13[validate head message length]
    D13 --> D14{CC lane and CC budget allow fair pop?}
    D14 -- yes --> D15[pop_fair_queued_cc_message]
    D14 -- no --> D16[pop whole framed message]
    D12 --> D17[bufferMIDIUart bytes]
    D15 --> D17
    D16 --> D17
    D17 --> D18[UART TX buffer receives bytes]
```

```mermaid
sequenceDiagram
    participant Caller
    participant DIN as ConnectedDINMIDIDevice
    participant Queue as MIDIQueueManagerState/MIDIQueueManager
    participant Adapter as DINPriorityPopAdapter
    participant UART as UART TX buffer

    Caller->>DIN: enqueue_serial_message(message)
    DIN->>Queue: enqueue_with_cc_policy(...)
    Note over Queue: CC messages can coalesce before enqueueing a duplicate
    DIN->>DIN: flush_serial_output(now)
    DIN->>DIN: update_serial_budget(now)
    DIN->>DIN: pop_next_prioritized_bytes(...)
    DIN->>Adapter: pop_priority_lanes_with_cc_fairness(...)
    Adapter->>Queue: queue_count(priority)
    Adapter->>Queue: validate head message / fairness / lane pop
    Queue-->>DIN: next bytes
    DIN->>UART: bufferMIDIUart(byte)
```

## USB vs DIN

| Topic | USB | DIN |
| --- | --- | --- |
| Transport unit | 4-byte USB-MIDI event packet | Raw MIDI bytes |
| Drain trigger | Build a send buffer for the next USB transfer | Fill UART TX buffer while pacing allows |
| Budgeting | CC packet budget during transfer assembly | Q8 time budget plus UART space plus CC UART budget |
| Clock lane | No special byte-level lane handling | Clock lane is byte-oriented and emits one byte at a time |
| Framed messages | Always pop as packed packets | Pop whole messages when the length fits |
| Shared fairness | Same shared CC lane policy and priority traversal | Same shared CC lane policy and priority traversal |

## Reading The Code

If you want to follow this in code, the most useful entry points are:

- [src/deluge/io/midi/midi_device_manager.cpp](../../src/deluge/io/midi/midi_device_manager.cpp)
- [src/deluge/io/midi/midi_queue_manager.h](../../src/deluge/io/midi/midi_queue_manager.h)

The USB side starts at `ConnectedUSBMIDIDevice::bufferMessage()` and drains through `consumeSendData()`.
The DIN side starts at `ConnectedDINMIDIDevice::enqueue_serial_message()` and drains through `flush_serial_output()`.

## Code-Centric Call Chains

This version follows the exact function names and shows the call chain step by step.

### USB Call Chain

```mermaid
sequenceDiagram
    participant Caller
    participant USB as ConnectedUSBMIDIDevice
    participant MQ as MIDIQueueManagerState
    participant QM as MIDIQueueManager
    participant A as USBPriorityPopAdapter

    Caller->>USB: bufferMessage(fullMessage, priority)
    USB->>USB: total_queued_messages()
    USB->>USB: queue_count(priority)
    USB->>USB: push_priority_message(priority, fullMessage)
    USB->>MQ: enqueue_with_cc_policy(*this, priority, fullMessage, ...)
    MQ->>QM: enqueue_with_cc_policy(...)
    Note over QM: CC lane may coalesce or track debt before enqueueing

    Caller->>USB: consumeSendData()
    USB->>USB: total_queued_messages()
    USB->>USB: pop_priority_message(message, cc_budget_packets_remaining)
    USB->>A: pop_priority_lanes_with_cc_fairness(adapter, QUEUE_PRIORITY_CLOCK, QUEUE_PRIORITY_CC, context)
    A->>QM: pop_priority_lanes_with_cc_fairness(...)
    QM->>A: queue_count(priority)
    QM->>A: handle_cc_lane(priority, context)
    QM->>A: pop_lane(priority, context)
    A->>USB: pop_fair_queued_cc_message(message_out)
    USB->>MQ: pop_fair_cc_candidate(*this, begin_scan, next_scan, remove_selected, message_out)
    MQ->>QM: pop_fair_cc_candidate(...)
    QM-->>USB: selected packet
    USB-->>Caller: copied into dataSendingNow
```

### DIN Call Chain

```mermaid
sequenceDiagram
    participant Caller
    participant DIN as ConnectedDINMIDIDevice
    participant MQ as MIDIQueueManagerState
    participant QM as MIDIQueueManager
    participant A as DINPriorityPopAdapter

    Caller->>DIN: enqueue_serial_message(message)
    DIN->>QM: classify_message(message)
    DIN->>MQ: enqueue_with_cc_policy(*this, priority, message, ...)
    MQ->>QM: enqueue_with_cc_policy(...)
    Note over QM: DIN CC messages can be coalesced before appending a duplicate

    Caller->>DIN: flush_serial_output(now_sample_timer)
    DIN->>DIN: update_serial_budget(now_sample_timer)
    DIN->>DIN: pop_next_prioritized_bytes(out_bytes, max_len, budget_bytes, uart_space, cc_uart_budget, popped_priority)
    DIN->>A: pop_priority_lanes_with_cc_fairness(adapter, QUEUE_PRIORITY_CLOCK, QUEUE_PRIORITY_CC, context)
    A->>QM: pop_priority_lanes_with_cc_fairness(...)
    QM->>A: queue_count(priority)
    QM->>A: handle_cc_lane(priority, context)
    QM->>A: pop_lane(priority, context)
    A->>DIN: read_priority_queue_head_byte(priority)
    A->>DIN: pop_priority_queue_head_byte(priority, out_byte)
    A->>DIN: pop_priority_queue_message_bytes(priority, out_bytes, message_len)
    DIN->>MQ: pop_fair_cc_candidate / queue_manager_ operations
    DIN-->>Caller: bytes written to UART buffer
```

### Step By Step

1. USB starts at `ConnectedUSBMIDIDevice::bufferMessage()` and eventually calls `ConnectedUSBMIDIDevice::consumeSendData()` to assemble the next transfer.
2. DIN starts at `ConnectedDINMIDIDevice::enqueue_serial_message()` and eventually calls `ConnectedDINMIDIDevice::flush_serial_output()` to fill the UART buffer.
3. Both transports enqueue through `MIDIQueueManagerState::enqueue_with_cc_policy()`, which forwards into `MIDIQueueManager::enqueue_with_cc_policy()`.
4. Both transports drain through `MIDIQueueManager::pop_priority_lanes_with_cc_fairness()`.
5. USB uses `USBPriorityPopAdapter::handle_cc_lane()` and `USBPriorityPopAdapter::pop_lane()` to choose between fair CC dequeue and direct head pop.
6. DIN uses `DINPriorityPopAdapter::handle_cc_lane()` and `DINPriorityPopAdapter::pop_lane()` to handle the clock lane, framed messages, and fair CC dequeue.
7. USB ultimately copies packed packets into `dataSendingNow`, while DIN writes byte sequences into the UART TX buffer via `bufferMIDIUart()`.
