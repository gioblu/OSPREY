/*
  ______  _______ ______  ______  _______ __    __
  ██████╗ ███████╗██████╗ ██████╗ ███████╗██╗   ██╗
 ██╔═══██╗██╔════╝██╔══██╗██╔══██╗██╔════╝╚██╗ ██╔╝
 ██║   ██║███████╗██████╔╝██████╔╝█████╗   ╚████╔╝
 ██║   ██║╚════██║██╔═══╝ ██╔══██╗██╔══╝    ╚██╔╝
 ╚██████╔╝███████║██║     ██║  ██║███████╗   ██║
  ╚═════╝ ╚══════╝╚═╝     ╚═╝  ╚═╝╚══════╝   ╚═╝
 Copyright 2025-2026 by Giovanni Blu Mitolo gioscarab@gmail.com
 _____________________________________________________________________________

OSPREY - Transport layer for PJON protocol
Constants and data types definitions

Copyright 2025-2026 by Giovanni Blu Mitolo gioscarab@gmail.com

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

/* PJON PACKET SIZE CONFIGURATION ========================================== */

/* PJON packet size - Set to 1024 bytes */
#ifndef PJON_PACKET_MAX_LENGTH
  #define PJON_PACKET_MAX_LENGTH 1024
#endif

#include "PJON.h"

/* OSPREY CONFIGURATION CONSTANTS =========================================  */

/* Maximum number of concurrent exchanges (transmissions and receptions) */
#ifndef OSPREY_MAX_EXCHANGES
  #define OSPREY_MAX_EXCHANGES 10
#endif

/* Maximum payload size for segmentation (bytes) - Set to 4GB (2^32 - 1) */
#ifndef OSPREY_MAX_PAYLOAD_SIZE
  #define OSPREY_MAX_PAYLOAD_SIZE 4294967295UL  // 4GB - 1 (2^32 - 1)
#endif

/* Maximum number of segments per transmission */
#ifndef OSPREY_MAX_SEGMENTS
  #define OSPREY_MAX_SEGMENTS 4294967295UL
#endif

/* Default segment size (PJON packet size minus headers) */
#ifndef OSPREY_DEFAULT_SEGMENT_SIZE
  /* Reduced from 1000 for compatibility with LocalFile strategy */
  #define OSPREY_DEFAULT_SEGMENT_SIZE 500  
#endif

/* Timeout for incomplete transmissions (microseconds) */
#ifndef OSPREY_TRANSMISSION_TIMEOUT
  #define OSPREY_TRANSMISSION_TIMEOUT 120000000  // 120 seconds
#endif

/* START segment ACK timeout values (microseconds) */
#ifndef OSPREY_START_ACK_RETRY_1
  #define OSPREY_START_ACK_RETRY_1 500000  // 0.5 seconds (first retry)
#endif

#ifndef OSPREY_START_ACK_RETRY_2
  #define OSPREY_START_ACK_RETRY_2 1500000  // 1.5 seconds (second retry)
#endif

#ifndef OSPREY_START_ACK_RETRY_3
  #define OSPREY_START_ACK_RETRY_3 3000000  // 3.0 seconds (third retry)
#endif

#ifndef OSPREY_START_ACK_MAX_RETRIES
  #define OSPREY_START_ACK_MAX_RETRIES 3  // Maximum START retransmission attempts
#endif

#ifndef OSPREY_START_ACK_TOTAL_TIMEOUT
  #define OSPREY_START_ACK_TOTAL_TIMEOUT 5000000  // 5 seconds total timeout
#endif

/* Random jitter for START retransmissions (microseconds) - max random delay */
#ifndef OSPREY_START_ACK_JITTER_MAX
  #define OSPREY_START_ACK_JITTER_MAX 100000  // Max 100ms random jitter
#endif

/* Selective NACK transmission interval (microseconds) */
#ifndef OSPREY_SELECTIVE_NACK_INTERVAL
  #define OSPREY_SELECTIVE_NACK_INTERVAL 1000000  // 1 second - send every 1s when missing segments detected
#endif

/* Random jitter for selective NACK timing (microseconds) - max random delay */
#ifndef OSPREY_SELECTIVE_NACK_JITTER_MAX
  #define OSPREY_SELECTIVE_NACK_JITTER_MAX 100000  // Max 100ms random jitter
#endif

/* Maximum number of pending requests to track */
#ifndef OSPREY_MAX_REQUESTS
  #define OSPREY_MAX_REQUESTS 8
#endif

/* Timeout for pending requests (microseconds) */
#ifndef OSPREY_REQUEST_TIMEOUT
  /* Default: 3 seconds (3,000,000 microseconds)
     Reduced from 60s to better support fast request/response flows
     (used by wait helpers and request cleanup). */
  #define OSPREY_REQUEST_TIMEOUT 3000000  // 3 seconds
#endif

/* OSPREY SEGMENT TYPES ==================================================== */

/* OSPREY packet types (decimal values) */
#define OSPREY_SEGMENT_REQUEST         0  // GET request for resource
#define OSPREY_SEGMENT_START           1  // First segment of multi-segment transfer
#define OSPREY_SEGMENT_DATA            2  // Data segment of multi-segment transfer
#define OSPREY_SEGMENT_SINGLE          4  // Complete transfer in single segment
#define OSPREY_SEGMENT_ACK             6  // Acknowledgment response
#define OSPREY_SEGMENT_ACK_START       7  // START segment acknowledgement
#define OSPREY_SEGMENT_NACK           21  // Negative acknowledgment (full retry)
#define OSPREY_SEGMENT_NACK_SELECTIVE 22  // Selective NACK with missing segment list

/* OSPREY ERROR CODES ======================================================= */

#define OSPREY_SUCCESS          0     // Operation successful
#define OSPREY_FAIL         65535     // Generic failure
#define OSPREY_BUFFER_FULL      2     // No free exchange slots available
#define OSPREY_INVALID_PACKET   3     // Received packet validation failed
#define OSPREY_TIMEOUT          4     // Exchange timed out
#define OSPREY_CONTENT_TOO_LONG 5     // Payload exceeds maximum size

/* OSPREY DATA STRUCTURES ==================================================== */

/* OSPREY segment header structure (8 bytes total, packed)
   
   Design optimization: total_segments is only sent in START segment
   as part of segment payload (first 4 bytes after header), not in every header.
   This saves 4 bytes per DATA and END segment.
*/
struct OSPREY_Header {
  uint8_t   type;               // Segment type - one of OSPREY_SEGMENT_* (1B)
  uint16_t  transmission_id;    // Unique transmission identifier (2B)
  uint32_t  segment_id;         // Segment index in transmission (4B)
  uint8_t   header_crc;         // CRC8 of header excluding this field (1B)
} __attribute__((packed));      // Total: 8 bytes (strict packing required)

/* Structure to track ongoing exchanges (transmissions and receptions)
   
   An "exchange" represents a single file/payload transfer, either:
   - Outgoing (TX): Transmission of segments to a remote endpoint
   - Incoming (RX): Reception and reassembly of segments from a remote endpoint
   
   Up to OSPREY_MAX_EXCHANGES exchanges can be active concurrently.
*/
struct OSPREY_Exchange {
  /* Common fields (used for both TX and RX) */
  PJON_Endpoint endpoint;            // Remote endpoint address
  uint16_t  exchange_id;             // Unique exchange identifier
  uint8_t  *payload;                 // Buffer for payload data
  uint32_t  payload_length;          // Total bytes in payload (final value for RX)
  uint32_t  processed;               // Segments sent (TX) or received (RX)
  uint32_t  segments;                // Total number of segments
  uint16_t  segment_size;            // Bytes per segment (configurable)
  uint32_t  last_activity;           // Timestamp of last activity (microseconds)
  uint32_t  crc32;                   // CRC32 checksum of entire payload
  bool      active;                  // Exchange is currently active
  bool      is_outgoing;             // true = transmission, false = reception
  bool      payload_owned;           // Whether this struct owns the payload buffer
  
  /* Incoming transfer fields (RX only) */
  uint8_t  *segments_map;            // Bit-map of received segments (1 bit per segment)
  uint32_t  segments_map_size;       // Size of segments_map in bytes
  uint32_t  nack_last_sent;          // Timestamp when selective NACK was last sent (microseconds)
  
  /* Outgoing transfer fields (TX only) */
  bool      waiting_ack;             // Awaiting START ACK_START from receiver
  uint8_t  *segments_to_retransmit;  // Bit-map of segments to retransmit (for selective NACK)
  uint32_t  segments_to_retransmit_size;  // Size of retransmit bitmap in bytes
  
  /* START segment ACK tracking (TX only, for multi-segment) */
  bool      start_ack_received;      // Has receiver sent ACK_START?
  uint8_t   start_retries;           // Current START retry attempt (0, 1, 2, ...)
  uint32_t  start_last_sent;         // Timestamp when START was last sent (microseconds)
};

/* Structure to track pending GET requests 
   
   To prevent unsolicited multi-segment transfers, OSPREY tracks
   GET requests and only accepts multi-segment responses that match
   a pending request with the same transmission_id and endpoint.
*/
struct OSPREY_Request {
  PJON_Endpoint endpoint;           // Remote endpoint that made the request
  uint16_t      transmission_id;    // Transmission ID from GET request
  uint32_t      timestamp;          // When request was received (microseconds)
  bool          active;             // Request is currently pending
  char          resource_path[256]; // Requested resource path/name
};

/* ============================================================================
   OSPREY CALLBACK FUNCTION TYPES
   ============================================================================ */

/* Callback type for payload reception
   
   Called when a complete payload (single or multi-segment) is received
   and verified via CRC32.
   
   Parameters:
   - payload: Pointer to received data
   - length: Size of payload in bytes
   - packet_info: PJON packet metadata (sender, bus ID, etc.)
*/
typedef void (* OSPREY_Receiver)(
  const uint8_t *payload,
  uint32_t length,
  const PJON_Packet_Info &packet_info
);

/* Callback type for error reporting
   
   Called when a transfer fails or times out.
   
   Parameters:
   - error_code: One of OSPREY_* error codes
   - exchange_id: ID of the failed exchange
   - endpoint: Remote endpoint involved in the failed exchange
*/
typedef void (* OSPREY_Error)(
  uint16_t error_code,
  uint16_t exchange_id,
  const PJON_Endpoint &endpoint
);

/* Callback type for handling GET requests
   
   Called when a GET request is received. The handler should
   examine the resource_path and respond by calling osprey.reply()
   with the requested resource data.
   
   Parameters:
   - resource_path: Null-terminated string with the requested resource
*/
typedef void (* OSPREY_Request_Handler)(
  const char *resource_path
);
