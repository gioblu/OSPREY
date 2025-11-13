
#### Transport layer
- **[OSPREY v1.0](/specification/OSPREY-transport-layer-specification-v1.0.md)**

#### Network layer
- [PJON (Padded Jittering Operative Network) v4.0](https://github.com/gioblu/PJON/specification/PJON-protocol-specification-v4.0.md)
- [Network services list](https://github.com/gioblu/PJON/specification/PJON-network-services-list.md)

#### Data link layer
- [PJDL (Padded Jittering Data Link) v5.0](https://github.com/gioblu/PJON/src/strategies/SoftwareBitBang/specification/PJDL-specification-v5.0.md)
- [PJDLR (Padded Jittering Data Link over Radio) v3.0](https://github.com/gioblu/PJON/src/strategies/OverSampling/specification/PJDLR-specification-v3.0.md)
- [PJDLS (Padded Jittering Data Link byte Stuffed) v2.0](https://github.com/gioblu/PJON/src/strategies/AnalogSampling/specification/PJDLS-specification-v2.0.md)
- [TSDL (Tardy Serial Data Link) v3.0](https://github.com/gioblu/PJON/src/strategies/ThroughSerial/specification/TSDL-specification-v3.0.md)
- [SFSP (Secure Frame Separation Protocol) v1.0](https://github.com/gioblu/PJON/specification/SFSP-frame-separation-specification-v1.0.md)

---

## OSPREY v1.0
```
Invented by Giovanni Blu Mitolo
Originally published: 09/11/2025
Last updated: 13/11/2025
Related work: https://github.com/gioblu/OSPREY/
Compliant implementations: OSPREY v1.0 and following
Released into the public domain

13/11/2025 1.0 - Initial Draft
```

The OSPREY transport protocol is designed to enable cheap and efficient peer-to-peer (P2P) networking without central authority. OSPREY operates using PJON as underlying network protocol and supports exchange of resources (raw data, files) of up to 4294967295 bytes (2^32 - 1) between peers (devices or processes connected to an OSPREY network) using segments. OSPREY implements segmentation, retransmission, reassembly and data-integrity verification of resources. An OSPREY network is a group of peers that share a network connection and exchange resources. The protocol is designed to let each peer easily and efficiently request and serve resources to other peers. OSPREY is very efficient because it is designed to have a very low overhead (8-12 bytes per segment). 

The only optional feature of PJON required by OSPREY is the packet identification feature, for this reason the underlying overhead is minimal (7-10 bytes). 

| Protocol stack    | Overhead | 
| ----------------- | -------- |
| OSPREY-PJON       | 15-47B   | 
| TCP-IPV4          | 40-120B  | 
| TCP-IPV6          | 60-132B  | 

Note: PJON overhead includes 6 bytes for the MAC address, the MAC address field is included in an Ethernet frame, so it is not part of the accounted overhead for TCP-IPV4 and TCP-IPV6.

The graph below shows the conceptual model that characterizes and standardizes the protocol. Its goal is the interoperability of diverse systems on a wide range of media with the use of a new set of Open Standards. The graph partitions represent abstraction layers.

```
 ________________________________________________
| 4 Transport layer: OSPREY (segment)            |
|- Optional features ----------------------------|
| Encryption                                     |
|- Core features --------------------------------|
| Resource transfer                              |
| Resource Discovery and Advertisement           |
| Network configuration exchange                 |
| Process identification (16 bits)               |
| Automatic payload segmentation (96 bits)       |
| Out-of-order delivery tolerance                |
|________________________________________________|
| 3 Network layer: PJON (packet)                 |
|- Optional features ----------------------------|
| Routing and switching                          |
| Hop count (8 bits)                             |
| Hardware identification (48 bits)              |
| Service identification (16 bits)               |
| Packet identification (16 bits)                |
| Bus identification (32 bits)                   |
| Sender identification                          |
| Packet transmission, maximum length 65535B     |
|- Core features --------------------------------|
| Congestion control                             |
| Packet transmission, maximum length 255B       |
| Error detection (16 or 40 bits)                |
| Device identification (8 bits)                 |
| Broadcast                                      |
|________________________________________________|
| 2 Data link layer: PJDL or others (frame)      |
| Acknowledgement                                |
| Frame transmission                             |
| Medium access control                          |
|________________________________________________|
| 1 Physical layer:                              |
| Electric, radio or light impulses              |
|________________________________________________|

```

### Overview
* OSPREY operates as a transport layer above PJON
* Transport of resources (raw data or files) of up to 4294967295 bytes (2^32 - 1)
* Resources are transmitted using one or more segments
* Segments are identified with a 32 bit segment ID
* An OSPREY segment is contained in one PJON packet
* Large resources are segmented into multiple segments
* Original resources are reconstructed from out-of-order segments
* Each participant in an OSPREY network is a peer
* Each peer can both request and serve resources concurrently
* OSPREY enables symmetric, bidirectional communication without central authority

### Segment types
The OSPREY segment header identifies the packet type using an 8-bit type field:

| Type            | Value | Description                                   |
|-----------------|-------|-----------------------------------------------|
| REQUEST         | 0     | GET request for resource                      |
| START           | 1     | First segment of multi-segment transmission   |
| DATA            | 2     | Data segment of multi-segment transmission    |
| SINGLE          | 4     | Payload fits in single PJON packet            |
| ACK             | 6     | Resource reception completion acknowledgement |
| ACK_START       | 7     | START segment acknowledgement                 |
| NACK            | 21    | Negative acknowledgement from receiver        |
| NACK_SELECTIVE  | 22    | Selective NACK with missing segment list      |

### Segment header structure
The OSPREY segment header is 8 bytes and is prepended to each payload segment:

```cpp
OSPREY SEGMENT HEADER (8 bytes)
 _______________________________________________
|         |              |           |          |
| SEGMENT | TRANSMISSION | SEGMENT   | HEADER   |
| TYPE    | ID           | ID        | CRC8     |
|_________|______________|___________|__________|
 1 Byte     2 Bytes        4 Bytes     1 Byte    = 8 Bytes
```

### Requesting a single-segment resource

Each peer can request a single-segment resource to another peer.

In this scenario 2 peers are present on the same collision domain and can reach each other directly using OSPREY-PJON using device id 44 and device 45.

```cpp
  ___________                 ___________ 
 | PEER      |  OSPREY-PJON  | PEER      |
 | ID 44     |<------------->| ID 45     |
 |___________|               |___________| 
```

Peer 44 requests the resource `README.md` from peer 45:

```cpp
 ________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | PAYLOAD       |
|________|_________|____________|________|_______________|
|   0    |    1    |     0      |   ?    | README.md     |
|________|_________|____________|________|_______________|
 8 bits   16 bits   32 bits      8 bits   72 bits        

```
When a request is received the PJON network configuration (the packet header) used by the requester will be used for the following exchange. Peer 45 responds with a single-segment transmission (type 4) containing the `README.md` file:
```cpp
 ________________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | PAYLOAD     |  CRC32  |
|________|_________|____________|________|_____________|_________|
|   4    |    1    |     0      |   ?    | Hello World |    ?    |
|________|_________|____________|________|_____________|_________|
 8 bits   16 bits   32 bits      8 bits   88 bits       32 bits  

```
Peer 44 sends acknowledgement (type 6) at the end of transmission:

```cpp
 ________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  |
|________|_________|____________|________|
|   6    |    1    |     0      |   ?    |
|________|_________|____________|________|
 8 bits   16 bits   32 bits      8 bits  

```
### Requesting a multi-segment resource

Each peer can request a multi-segment resource to another peer. 

In this scenario 2 peers are present on the same collision domain and can reach each other directly using OSPREY over PJON using device id 44 and device 45.

```cpp
  ___________                 ___________ 
 | PEER      |  OSPREY-PJON  | PEER      |
 | ID 44     |<------------->| ID 45     |
 |___________|               |___________| 
```

#### Request transmission

Peer 44 requests the resource `README.md`, a text file (2400 bits) to device 45:

```cpp
 ________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | PAYLOAD       |
|________|_________|____________|________|_______________|
|   0    |    1    |     0      |   ?    | README.md     |
|________|_________|____________|________|_______________|
 8 bits   16 bits   32 bits      8 bits   72 bits        

```

#### START segment transmission

Peer 45 responds with a multi-segment transmission containing the `README.md` file: starts with a START segment (type 1) that contains the count of segments required to transmit the resource and a first portion of its data:
```cpp
 __________________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | COUNT + DATA  |  CRC32  |
|________|_________|____________|________|_______________|_________|
|   1    |    1    |     0      |   ?    | 3 + data      |    ?    |
|________|_________|____________|________|_______________|_________|
 8 bits   16 bits   32 bits      8 bits   32 + x bits     32 bits 
```

#### START acknowledgement transmission

Peer 44 responds with an acknowledgement of the START (type 7) segment: 
```cpp
 ________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  |
|________|_________|____________|________|
|   7    |    1    |     0      |   ?    |
|________|_________|____________|________|
  8 bits   16 bits   32 bits      8 bits 
```

Peer must verify that the receiver is aware of the ongoing response, this is required to avoid initiating a multi-segment transmission towards a peer. For this reason peer 45 waits for the acknowledgement of the START segment. 

If that is not received:

1. Peer 45 retransmits the START segment 0.5 seconds plus a small random time after the last transmission terminated
2. Peer 45 retransmits the START segment 1.5 seconds plus a small random time after the last transmission terminated
3. Peer 45 retransmits the START segment 3.0 seconds plus a small random time after the last transmission terminated

If 5 seconds after the last transmission terminated no acknowledgement of the START (type 7) segment is received the multi-segment transmission is aborted.

If peer 44 receives a duplicate START segment, it simply sends another ACK for that START segment. 

#### DATA segments transmission

Peer 45 then transmits all DATA segments (type 2). 

```cpp
 ________________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | PAYLOAD     |  CRC32  |
|________|_________|____________|________|_____________|_________|
|   2    |    1    |     1      |   ?    | data        |    ?    |
|________|_________|____________|________|_____________|_________|
 8 bits   16 bits   32 bits      8 bits   88 bits       32 bits 
```

#### Selective NACK transmission

OSPREY implements selective NACK to efficiently handle packet loss in multi-segment transmissions:

```cpp
 ___________________________________________________________________________
|  TYPE  |  TX ID  |  SEG COUNT  |  CRC8  | MISSING SEGMENT IDS             |
|________|_________|_____________|________|_________________________________|
|   22   |    1    |      2      |   ?    |                                 |
|________|_________|_____________|________|_________________________________|
 8 bits   16 bits   32 bits      8 bits    32 bits per segment id (up to 32)       
```
1. Every second plus a small random time if the receiver detects missing segments sends a NACK_SELECTIVE packet containing the ids of those segments (up to 32 segments per NACK)
2. The transmitter retransmits the specifically requested missing segments
4. This process repeats until all segments are successfully received or a global timeout occurs

#### Last DATA segment transmission

The last DATA segment (type 2) contains the CRC32: 
```cpp
 ____________________________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  | DATA    |  CRC32  |
|________|_________|____________|________|_________|_________|
|   2    |    1    |     2      |   ?    | data    |    ?    |
|________|_________|____________|________|_________|_________|
 8 bits   16 bits   32 bits      8 bits   88 bits   32 bits  
```

#### Acknowledgement segment transmission

Peer 44 sends acknowledgement (type 6) at the end of transmission:

```cpp
 ________________________________________
|  TYPE  |  TX ID  | SEGMENT ID |  CRC8  |
|________|_________|____________|________|
|   6    |    1    |     0      |   ?    |
|________|_________|____________|________|
  8 bits   16 bits   32 bits      8 bits 
```
