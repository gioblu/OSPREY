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
Provides segmentation and reassembly of large payloads

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

#include "OSPREY_Defines.h"

template<typename Strategy>
class OSPREY {
  public:
    PJON<Strategy> bus;

    /* OSPREY initialization with no parameters:
       State: Local bus
       Acknowledge: true
       Device id: PJON_NOT_ASSIGNED (255) */

    OSPREY() : bus(PJON<Strategy>()) {
      set_default();
    };

    OSPREY(uint8_t device_id) : bus(PJON<Strategy>(device_id)) {
      set_default();
    };

    OSPREY(const uint8_t *b_id, uint8_t device_id) : 
      bus(PJON<Strategy>(b_id, device_id)) {
      set_default();
    };

    /* OSPREY destructor - cleanup all active exchanges */
    ~OSPREY() { 
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++) 
        if(_exchanges[i].active) clear_exchange(i);
    };

    /* Disable copy constructor, assignment operator to prevent double-free */
    OSPREY(const OSPREY&) = delete;
    OSPREY& operator=(const OSPREY&) = delete;

    /* Initialize OSPREY */
    void begin() {
      bus.begin();
      bus.set_receiver(receiver_callback);
      bus.set_custom_pointer(this);
      _exchange_id_seed = bus.device_id() + PJON_RANDOM(65535);
      // Initialize exchange array to zero
      memset(_exchanges, 0, sizeof(_exchanges));
      // Initialize request tracking array to zero
      memset(_requests, 0, sizeof(_requests));
    };

    /* Send large payload with automatic segmentation and specific transmission_id */
    uint16_t send_packet(
      const PJON_Endpoint &endpoint, 
      const void *payload, 
      uint32_t length,
      uint16_t use_transmission_id
    ) {
      if(!payload || length == 0) return OSPREY_FAIL;
      if(length > OSPREY_MAX_PAYLOAD_SIZE) return OSPREY_CONTENT_TOO_LONG;
      // Find available exchange slot
      uint8_t slot = find_free_exchange_slot();
      if(slot == 255) return OSPREY_BUFFER_FULL;
      // Calculate segments needed
      uint16_t segment_size = calculate_segment_size();
      uint32_t total_segments = (length + segment_size - 1) / segment_size;
      if(total_segments > OSPREY_MAX_SEGMENTS) return OSPREY_CONTENT_TOO_LONG;
      
      // Initialize outgoing exchange with specific transmission_id
      init_outgoing_exchange(
        slot, endpoint, (uint8_t*)payload, length,
        segment_size, total_segments, false, use_transmission_id
      );
      return OSPREY_SUCCESS;
    };

    /* Send large payload with automatic segmentation */
    uint16_t send(
      const PJON_Endpoint &endpoint, 
      const void *payload, 
      uint32_t length
    ) {
      if(!payload || length == 0) return OSPREY_FAIL;
      if(length > OSPREY_MAX_PAYLOAD_SIZE) return OSPREY_CONTENT_TOO_LONG;
      // Find available exchange slot
      uint8_t slot = find_free_exchange_slot();
      if(slot == 255) return OSPREY_BUFFER_FULL;
      // Calculate segments needed
      uint16_t segment_size = calculate_segment_size();
      uint32_t total_segments = (length + segment_size - 1) / segment_size;
      if(total_segments > OSPREY_MAX_SEGMENTS) return OSPREY_CONTENT_TOO_LONG;
      // Initialize outgoing exchange
      init_outgoing_exchange(
        slot, endpoint, (uint8_t*)payload, length,
        segment_size, total_segments, false
      );
      return OSPREY_SUCCESS;
    };

    /* Send data from file */
    uint16_t send_file(const PJON_Endpoint &endpoint, const char *filename) {
      FILE *file = fopen(filename, "rb");
      if(!file) return OSPREY_FAIL;
      // Get file size
      fseek(file, 0, SEEK_END);
      long file_size = ftell(file);
      fseek(file, 0, SEEK_SET);
      if(file_size < 0 || file_size > (long)OSPREY_MAX_PAYLOAD_SIZE) {
        fclose(file);
        return OSPREY_CONTENT_TOO_LONG;
      } // Allocate buffer for file reading
      uint8_t *buffer = new uint8_t[file_size];
      if(!buffer) {
        fclose(file);
        return OSPREY_FAIL;
      }
      size_t bytes_read = fread(buffer, 1, file_size, file);
      fclose(file);
      if(bytes_read != (size_t)file_size) {
        delete[] buffer;
        return OSPREY_FAIL;
      } // Find available exchange slot BEFORE allocating owned buffer
      uint8_t slot = find_free_exchange_slot();
      if(slot == 255) {
        delete[] buffer;
        return OSPREY_BUFFER_FULL;
      } // Now allocate owned buffer for transmission
      uint8_t *owned_buffer = new uint8_t[file_size];
      if(!owned_buffer) {
        delete[] buffer;
        return OSPREY_FAIL;
      } // Copy file data into owned buffer
      memcpy(owned_buffer, buffer, file_size);
      delete[] buffer;  // Free temporary file buffer
      // Calculate segments needed
      uint16_t segment_size = calculate_segment_size();
      uint32_t total_segments = 
        ((uint32_t)file_size + segment_size - 1) / segment_size;
      // Initialize outgoing exchange with owned buffer
      init_outgoing_exchange(
        slot, endpoint, owned_buffer, (uint32_t)file_size,
        segment_size, total_segments, true
      );
      return OSPREY_SUCCESS;
    };

    /* Update exchange state */
    void update() {
      bus.update();
      update_exchanges();
      update_requests();
    };

    /* Set receiver callback */
    void set_receiver(OSPREY_Receiver receiver) {
      _receiver = receiver;
    };

    /* Set error callback */
    void set_error(OSPREY_Error error) {
      _error = error;
    };

    /* Set request handler callback */
    void set_request_handler(OSPREY_Request_Handler handler) {
      _request_handler = handler;
    };

    /* Send GET request for a resource */
    uint16_t send_request(
      const PJON_Endpoint &endpoint, 
      const char *resource_path
    ) {
      if(!resource_path) return OSPREY_FAIL;
      uint16_t path_len = strlen(resource_path);
      if(path_len == 0) return OSPREY_FAIL;
      
      // Find free request slot to track this outgoing request
      uint8_t req_id = find_free_request_slot();
      if(req_id == 255) return OSPREY_BUFFER_FULL;
      
      // Generate transmission ID
      uint16_t transmission_id = _exchange_id_seed++;
      
      // Store outgoing request info to allow response reception
      _requests[req_id].endpoint = endpoint;
      _requests[req_id].transmission_id = transmission_id;
      _requests[req_id].timestamp = PJON_MICROS();
      _requests[req_id].active = true;
      // Copy resource path for reference
      uint16_t copy_len = (path_len < 255) ? path_len : 255;
      memcpy(_requests[req_id].resource_path, resource_path, copy_len);
      _requests[req_id].resource_path[copy_len] = '\0';
      
      // Prepare GET request packet
      OSPREY_Header header;
      header.type = OSPREY_SEGMENT_REQUEST;
      header.transmission_id = transmission_id;
      header.segment_id = 0;
      set_header_crc(header);
      
      // Prepare packet with header + resource path
      uint16_t packet_size = sizeof(OSPREY_Header) + path_len;
      if(packet_size > PJON_PACKET_MAX_LENGTH) return OSPREY_CONTENT_TOO_LONG;
      
      uint8_t packet[PJON_PACKET_MAX_LENGTH];
      memcpy(packet, &header, sizeof(OSPREY_Header));
      memcpy(packet + sizeof(OSPREY_Header), resource_path, path_len);
      
      uint16_t result = bus.send_packet_blocking(endpoint.id, packet, packet_size);
      
      // If send failed, clean up the request
      if(result != PJON_ACK) {
        clear_request(req_id);
        return result;
      }
      
      return PJON_ACK;
    };

    /* Reply to the last GET request received */
    uint16_t reply(const void *payload, uint32_t length) {
      if(!_last_request_endpoint.id) return OSPREY_FAIL; // No request to reply to
      if(!payload || length == 0) return OSPREY_FAIL;
      if(length > OSPREY_MAX_PAYLOAD_SIZE) return OSPREY_CONTENT_TOO_LONG;
      
      // Use the stored endpoint and transmission_id from the last request
      return send_response_with_data(
        _last_request_endpoint, 
        payload, 
        length, 
        _last_request_transmission_id
      );
    };

    /* Send response with data using specific transmission_id */
    uint16_t send_response_with_data(
      const PJON_Endpoint &endpoint, 
      const void *payload, 
      uint32_t length,
      uint16_t transmission_id
    ) {
      if(!payload || length == 0) return OSPREY_FAIL;
      if(length > OSPREY_MAX_PAYLOAD_SIZE) return OSPREY_CONTENT_TOO_LONG;
      
      // Find available exchange slot
      uint8_t slot = find_free_exchange_slot();
      if(slot == 255) return OSPREY_BUFFER_FULL;
      
      // Calculate segments needed
      uint16_t segment_size = calculate_segment_size();
      uint32_t total_segments = (length + segment_size - 1) / segment_size;
      if(total_segments > OSPREY_MAX_SEGMENTS) return OSPREY_CONTENT_TOO_LONG;
      
      // Initialize outgoing exchange with custom transmission_id
      init_outgoing_exchange(
        slot, endpoint, (uint8_t*)payload, length,
        segment_size, total_segments, true, transmission_id
      );
      
      return OSPREY_SUCCESS;
    };

    /* Save received payload to file (Linux only) */
    bool save_to_file(
      const uint8_t *payload, 
      uint32_t length, 
      const char *filename
    ) {
      FILE *file = fopen(filename, "wb");
      if(!file) return false;
      size_t written = fwrite(payload, 1, length, file);
      fclose(file);
      return written == length;
    };

    /* Check if there are active outgoing exchanges (transmissions) */
    uint8_t active_transmissions() {
      uint8_t count = 0;
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++)
        if(_exchanges[i].active && _exchanges[i].is_outgoing) count++;
      return count;
    };

    /* Check total active exchanges */
    uint8_t active_exchanges() {
      uint8_t count = 0;
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++)
        if(_exchanges[i].active) count++;
      return count;
    };

    /* Get list of missing segment IDs for an incomplete exchange */
    uint32_t get_missing_segments(
      uint8_t ex_id, 
      uint32_t *missing, 
      uint32_t max
    ) {
      if(ex_id == 255 || !_exchanges[ex_id].active) return 0;
      OSPREY_Exchange &ex = _exchanges[ex_id];
      uint32_t r = 0;
      for(uint32_t seg_id = 0; seg_id < ex.segments && r < max; seg_id++)
        if(!is_segment_received(ex_id, seg_id)) missing[r++] = seg_id;
      return r;
    };

    /* Send selective NACK with list of missing segment IDs */
    uint16_t send_selective_nack(
      const PJON_Endpoint &endpoint,
      uint16_t transmission_id,
      const uint32_t *missing_segments,
      uint32_t missing_count
    ) {
      if(missing_count > 32) missing_count = 32;
      uint16_t packet_size = 
        sizeof(OSPREY_Header) + 4 + (missing_count * 4);
      if(packet_size > PJON_PACKET_MAX_LENGTH) return OSPREY_FAIL;
      uint8_t packet[PJON_PACKET_MAX_LENGTH];
      OSPREY_Header *header = (OSPREY_Header*)packet;
      header->type = OSPREY_SEGMENT_NACK_SELECTIVE;
      header->transmission_id = transmission_id;
      header->segment_id = missing_count;
      set_header_crc(*header);
      uint8_t *count_pos = packet + sizeof(OSPREY_Header);
      *(uint32_t*)count_pos = missing_count;
      uint8_t *seg_pos = count_pos + 4;
      for(uint32_t i = 0; i < missing_count; i++)
        *(uint32_t*)(seg_pos + i * 4) = missing_segments[i];
      uint16_t result = 
        bus.send_packet_blocking(endpoint.id, packet, packet_size);
      return result;
    };

    /* Access exchange for testing and diagnostics */
    OSPREY_Exchange* get_exchange(uint8_t ex_id) {
      if(ex_id >= OSPREY_MAX_EXCHANGES) return nullptr;
      return &_exchanges[ex_id];
    };

  private:
    OSPREY_Exchange _exchanges[OSPREY_MAX_EXCHANGES];
    OSPREY_Request _requests[OSPREY_MAX_REQUESTS];  // Track pending GET requests
    OSPREY_Receiver _receiver = nullptr;
    OSPREY_Error _error = nullptr;
    OSPREY_Request_Handler _request_handler = nullptr;
    uint16_t _exchange_id_seed = 0;
    
    // Track last GET request for reply() functionality
    PJON_Endpoint _last_request_endpoint;
    uint16_t _last_request_transmission_id = 0;

    /* Set default configuration */
    void set_default() {
      // Enable features needed for OSPREY
      bus.set_packet_id(true);
      bus.set_acknowledge(true);
    };

    /* Calculate optimal segment size based on PJON configuration */
    uint16_t calculate_segment_size() {
      uint16_t overhead = sizeof(OSPREY_Header);
      uint16_t available = PJON_PACKET_MAX_LENGTH;
      available -= bus.packet_overhead();
      available -= overhead;
      if(available > OSPREY_DEFAULT_SEGMENT_SIZE) 
        return OSPREY_DEFAULT_SEGMENT_SIZE;
      return (available > 0) ? available : OSPREY_DEFAULT_SEGMENT_SIZE;
    };

    /* Find free exchange slot */
    uint8_t find_free_exchange_slot() {
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++) 
        if(!_exchanges[i].active) return i;
      return 255; // No free slot
    };

    /* Find exchange by endpoint, exchange_id, and direction.
       Set is_outgoing to true for transmissions, false for receptions.
       Returns 255 if not found. */
    uint8_t find_exchange(const PJON_Endpoint &endpoint, uint16_t id, bool is_outgoing) {
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++)
        if(
          _exchanges[i].active && 
          _exchanges[i].endpoint.id == endpoint.id &&
          _exchanges[i].exchange_id == id &&
          (_exchanges[i].is_outgoing == is_outgoing)
        ) return i;
      return 255;
    };

    /* Initialize outgoing exchange with common fields */
    void init_outgoing_exchange(
      uint8_t slot,
      const PJON_Endpoint &endpoint,
      uint8_t *payload,
      uint32_t length,
      uint16_t segment_size,
      uint32_t total_segments,
      bool payload_owned
    ) {
      OSPREY_Exchange &ex = _exchanges[slot];
      ex.endpoint = endpoint;
      ex.exchange_id = _exchange_id_seed++;
      
      if(payload_owned) {
        // Make a copy of the payload
        ex.payload = new uint8_t[length];
        if(ex.payload) {
          memcpy(ex.payload, payload, length);
          ex.payload_owned = true;
        } else {
          // Memory allocation failed
          ex.active = false;
          return;
        }
      } else {
        ex.payload = payload;
        ex.payload_owned = false;
      }
      
      ex.payload_length = length;
      ex.processed = 0;
      ex.segments = total_segments;
      ex.segment_size = segment_size;
      ex.last_activity = PJON_MICROS();
      ex.crc32 = PJON_crc32::compute(ex.payload, length);
      ex.active = true;
      ex.is_outgoing = true;
      ex.waiting_ack = (total_segments > 1);  // Wait for START ACK on multi-segment
      ex.segments_to_retransmit = nullptr;
      ex.segments_to_retransmit_size = 0;
      ex.start_ack_received = false;
      ex.start_retries = 0;
      ex.start_last_sent = 0;
    };

    /* Initialize outgoing exchange with specific transmission_id */
    void init_outgoing_exchange(
      uint8_t slot,
      const PJON_Endpoint &endpoint,
      uint8_t *payload,
      uint32_t length,
      uint16_t segment_size,
      uint32_t total_segments,
      bool payload_owned,
      uint16_t use_transmission_id
    ) {
      OSPREY_Exchange &ex = _exchanges[slot];
      ex.endpoint = endpoint;
      ex.exchange_id = use_transmission_id;  // Use provided transmission_id
      
      if(payload_owned) {
        // Make a copy of the payload
        ex.payload = new uint8_t[length];
        if(ex.payload) {
          memcpy(ex.payload, payload, length);
          ex.payload_owned = true;
        } else {
          // Memory allocation failed
          ex.active = false;
          return;
        }
      } else {
        ex.payload = payload;
        ex.payload_owned = false;
      }
      
      ex.payload_length = length;
      ex.processed = 0;
      ex.segments = total_segments;
      ex.segment_size = segment_size;
      ex.last_activity = PJON_MICROS();
      ex.crc32 = PJON_crc32::compute(ex.payload, length);
      ex.active = true;
      ex.is_outgoing = true;
      ex.waiting_ack = (total_segments > 1);  // Wait for START ACK on multi-segment
      ex.segments_to_retransmit = nullptr;
      ex.segments_to_retransmit_size = 0;
      ex.start_ack_received = false;
      ex.start_retries = 0;
      ex.start_last_sent = 0;
    };

    /* Bitmap helper functions for segment tracking */
    /* Compute and set header CRC */
    inline void set_header_crc(OSPREY_Header &header) {
      header.header_crc = PJON_crc8::compute(
        (const uint8_t*)&header, sizeof(OSPREY_Header) - 1
      );
    };

    inline bool is_segment_received(uint8_t ex_id, uint32_t seg_id) {
      OSPREY_Exchange &ex = _exchanges[ex_id];
      uint32_t byte_index = seg_id / 8;
      uint8_t bit_mask = 1 << (seg_id % 8);
      return ex.segments_map[byte_index] & bit_mask;
    };

    /* Generic bitmap update function
       Updates segment bit in either segments_map or segments_to_retransmit */
    inline void update_segment_bit(
      uint8_t ex_id,  // Exchange ID
      uint32_t seg_id, // Segment ID
      bool value, // 0 to clear bit, 1 to set bit
      bool is_map // true for segments_map, false for segments_to_retransmit
    ) {
      OSPREY_Exchange &ex = _exchanges[ex_id];
      uint8_t *bitmap = is_map ? ex.segments_map : ex.segments_to_retransmit;
      if(!bitmap) return;  // Bitmap not allocated
      uint32_t byte_index = seg_id / 8;
      uint8_t bit_mask = 1 << (seg_id % 8);
      if(value) bitmap[byte_index] |= bit_mask;
      else bitmap[byte_index] &= ~bit_mask;
    };
    
    inline bool is_segment_to_retransmit(uint8_t ex_id, uint32_t seg_id) {
      OSPREY_Exchange &ex = _exchanges[ex_id];
      if(!ex.segments_to_retransmit) return false;
      uint32_t byte_index = seg_id / 8;
      uint8_t bit_mask = 1 << (seg_id % 8);
      return ex.segments_to_retransmit[byte_index] & bit_mask;
    };

    /* Update ongoing exchanges */
    void update_exchanges() {
      uint32_t t = PJON_MICROS();
      for(uint8_t i = 0; i < OSPREY_MAX_EXCHANGES; i++) {
        if(!_exchanges[i].active) continue;
        OSPREY_Exchange &ex = _exchanges[i];
        uint32_t idle_time = t - ex.last_activity;
        // Handle START ACK retry for outgoing multi-segment transmissions
        if(ex.is_outgoing && ex.segments > 1 && ex.waiting_ack && !ex.start_ack_received) {
          uint32_t start_idle_time = t - ex.start_last_sent;
          uint32_t retry_timeout = 0;
          // Determine if we need to retry START
          if(ex.start_last_sent == 0) {
            // First time sending START - send immediately
            send_next_segment(i);
          } else if(ex.start_retries < OSPREY_START_ACK_MAX_RETRIES) {
            // Determine retry timeout based on retry count
            if(ex.start_retries == 0) retry_timeout = OSPREY_START_ACK_RETRY_1;
            else if(ex.start_retries == 1) retry_timeout = OSPREY_START_ACK_RETRY_2;
            else if(ex.start_retries == 2) retry_timeout = OSPREY_START_ACK_RETRY_3;
            // Add random jitter to retry timeout
            uint16_t jitter = PJON_RANDOM(OSPREY_START_ACK_JITTER_MAX);
            // Check if it's time to retry
            if(start_idle_time >= (retry_timeout + jitter)) {
              ex.start_retries++;
              send_next_segment(i);  // Retransmit START
            }
          } else if(start_idle_time > OSPREY_START_ACK_TOTAL_TIMEOUT) {
            // 5 seconds after last START transmission - abort if no ACK_START received
            if(_error) _error(OSPREY_TIMEOUT, ex.exchange_id, ex.endpoint);
            clear_exchange(i);
            continue;
          }
        }
        
        // For incoming exchanges: send selective NACK every 1 second 
        // plus random jitter when missing segments are detected
        if(!ex.is_outgoing && ex.processed < ex.segments) {
          uint32_t nack_idle_time = t - ex.nack_last_sent;
          // Calculate retry interval with jitter
          uint16_t jitter = PJON_RANDOM(OSPREY_SELECTIVE_NACK_JITTER_MAX);
          uint32_t nack_interval = OSPREY_SELECTIVE_NACK_INTERVAL + jitter;
          
          // Time to send selective NACK?
          if(nack_idle_time >= nack_interval) {
            // Find missing segments
            uint32_t missing_segments[32];
            uint32_t missing_count = 
              get_missing_segments(i, missing_segments, 32);
            
            if(missing_count > 0) {
              // Send selective NACK with list of missing segments
              if(send_selective_nack(
                ex.endpoint, 
                ex.exchange_id, 
                missing_segments, 
                missing_count
              ) == PJON_ACK) {
                // Update timestamp only if send was successful
                ex.nack_last_sent = t;
              }
            }
          }
        }
        
        // Check for hard timeout (120 seconds)
        if(idle_time > OSPREY_TRANSMISSION_TIMEOUT) {
          if(_error) _error(OSPREY_TIMEOUT, ex.exchange_id, ex.endpoint);
          clear_exchange(i);
          continue;
        }
        // Handle outgoing exchanges (transmissions) - send DATA only after START ACK
        if(ex.is_outgoing) {
          if(!ex.waiting_ack && ex.processed < ex.segments) {
            send_next_segment(i);
          }
        }
      }
    };

    /* Update request timeouts */
    void update_requests() {
      uint32_t current_time = PJON_MICROS();
      for(uint8_t i = 0; i < OSPREY_MAX_REQUESTS; i++) {
        if(_requests[i].active) {
          uint32_t idle_time = current_time - _requests[i].timestamp;
          // Timeout old requests after 60 seconds
          if(idle_time > OSPREY_REQUEST_TIMEOUT) {
            clear_request(i);
          }
        }
      }
    };

    /* Send next segment of outgoing exchange */
    void send_next_segment(uint8_t ex_id) {
      OSPREY_Exchange &ex = _exchanges[ex_id];
      // Determine which segment to send (normal or selective retransmission)
      uint32_t segment_id;
      bool is_selective_retransmit = false;

      // For multi-segment transfers, only send DATA if START ACK has been received
      if(ex.segments > 1 && !ex.start_ack_received && ex.segments_to_retransmit == nullptr) {
        // Wait for START ACK before sending any DATA
        // START (segment 0) will be retried via update_exchanges() mechanism
        return;
      }

      if(ex.segments_to_retransmit) {
        // In selective retransmission mode: find next segment to retransmit
        bool found = false;
        for(uint32_t i = 0; i < ex.segments; i++)
          if(is_segment_to_retransmit(ex_id, i)) {
            segment_id = i;
            is_selective_retransmit = true;
            found = true;
            break;
          }
        if(!found) return; // No more segments to retransmit
      // Normal mode: send next sequential segment
      } else segment_id = ex.processed;
      
      uint32_t offset = (uint32_t)segment_id * ex.segment_size;
      uint16_t segment_payload_size = ex.segment_size;

      // Adjust size for last segment
      if(offset + segment_payload_size > ex.payload_length) 
        segment_payload_size = ex.payload_length - offset;

      // Prepare segment header
      OSPREY_Header header;
      if(ex.segments == 1) header.type = OSPREY_SEGMENT_SINGLE;
      else if(segment_id == 0) header.type = OSPREY_SEGMENT_START;
      else header.type = OSPREY_SEGMENT_DATA;  // All non-start segments are DATA
      header.transmission_id = ex.exchange_id;  // Use exchange_id in protocol
      header.segment_id = segment_id;
      // Calculate CRC8 of header data (excluding the CRC field itself)
      set_header_crc(header);
      
      // Prepare packet (static buffer bounded by PJON_PACKET_MAX_LENGTH)
      uint16_t packet_size = sizeof(OSPREY_Header) + segment_payload_size;
      // For START segments, prepend total_segments (4 bytes) before payload
      bool is_start_segment = (header.type == OSPREY_SEGMENT_START);
      if(is_start_segment) packet_size += 4; // Add 4 bytes for total_segments
      // Add CRC32 to last DATA segment (when segment_id == segments - 1)
      bool is_last_segment = (
        segment_id == ex.segments - 1 || 
        header.type == OSPREY_SEGMENT_SINGLE
      );
      if(is_last_segment) packet_size += 4; // Add 4 bytes for CRC32
      // Use static buffer (PJON_PACKET_MAX_LENGTH = 1024 bytes max)
      uint8_t packet[PJON_PACKET_MAX_LENGTH];
      if(packet_size > PJON_PACKET_MAX_LENGTH) return; // Packet too large
      memcpy(packet, &header, sizeof(OSPREY_Header));
      uint8_t *payload_pos = packet + sizeof(OSPREY_Header);
      // For START segments, prepend total_segments
      if(is_start_segment) {
        uint32_t total_segs_value = ex.segments;
        payload_pos[0] = (total_segs_value >> 24) & 0xFF;
        payload_pos[1] = (total_segs_value >> 16) & 0xFF;
        payload_pos[2] = (total_segs_value >> 8) & 0xFF;
        payload_pos[3] = total_segs_value & 0xFF;
        payload_pos += 4;
      }
      memcpy(payload_pos, ex.payload + offset, segment_payload_size);
      payload_pos += segment_payload_size;
      // Append CRC32 to last segment
      if(is_last_segment) {
        uint32_t crc32_value = ex.crc32;
        // Store CRC32 in big-endian format
        payload_pos[0] = (crc32_value >> 24) & 0xFF;
        payload_pos[1] = (crc32_value >> 16) & 0xFF;
        payload_pos[2] = (crc32_value >> 8) & 0xFF;
        payload_pos[3] = crc32_value & 0xFF;
      }

      // Send segment using endpoint (blocking send)
      uint16_t result = 
        bus.send_packet_blocking(ex.endpoint.id, packet, packet_size);
      
      // Accept ACK result as success
      if(result == PJON_ACK) {
        ex.last_activity = PJON_MICROS();
        
        // Track START segment send for retry purposes
        if(is_start_segment) {
          ex.start_last_sent = PJON_MICROS();
        }
        
        if(is_selective_retransmit) {
          // Mark this segment as no longer needing retransmission
          update_segment_bit(ex_id, segment_id, 0, true);
          // Check if all segments have been retransmitted
          bool all_retransmitted = true;
          for(uint32_t i = 0; i < ex.segments; i++)
            if(is_segment_to_retransmit(ex_id, i)) {
              all_retransmitted = false;
              break;
            }
          if(all_retransmitted) {
            // All segments retransmitted, clear the retransmit bitmap
            delete[] ex.segments_to_retransmit;
            ex.segments_to_retransmit = nullptr;
            ex.segments_to_retransmit_size = 0;
          }
        } else {
          // Normal sequential transmission
          // Only increment processed if not START (START waits for ACK_START)
          if(!is_start_segment) {
            ex.processed++;
            // Single segment completes immediately
            if(header.type == OSPREY_SEGMENT_SINGLE) {
              clear_exchange(ex_id);
            } else if(ex.processed >= ex.segments) {
              // All segments of multi-segment transmission sent successfully
              clear_exchange(ex_id);
            }
          }
          // For START segment, waiting_ack stays true until ACK_START arrives
        }
      } else if(result == PJON_FAIL) {
        // Send failure - let PJON handle retry, exchange continues
      }
    };

    /* Clear exchange */
    void clear_exchange(uint8_t index) {
      OSPREY_Exchange &ex = _exchanges[index];
      // Clean up payload if owned
      if(ex.payload_owned && ex.payload) delete[] ex.payload;
      // Clean up segments map (only used for incoming exchanges)
      if(ex.segments_map) delete[] ex.segments_map;
      // Clean up retransmit bitmap (only used for outgoing exchanges)
      if(ex.segments_to_retransmit) delete[] ex.segments_to_retransmit;
      // Reset entire exchange to zero
      memset(&ex, 0, sizeof(OSPREY_Exchange));
    };

    /* Static wrapper for PJON receiver callback */
    static void receiver_callback(
      uint8_t *payload,
      uint16_t length,
      const PJON_Packet_Info &packet_info
    ) {
      OSPREY<Strategy> *instance = 
        (OSPREY<Strategy>*)packet_info.custom_pointer
      ;
      if(instance) instance->segment_received(payload, length, packet_info);
    };

    /* Handle received packet */
    void segment_received(
      uint8_t *payload,
      uint16_t length,
      const PJON_Packet_Info &packet_info
    ) {
      if(length < sizeof(OSPREY_Header)) return;
      // Extract header from start of payload
      OSPREY_Header *header = (OSPREY_Header*)payload;
      // Validate header CRC (CRC of header excluding the CRC field itself)
      if(
        PJON_crc8::compute(
          payload, sizeof(OSPREY_Header) - 1 // Exclude the CRC field
        ) != header->header_crc
      ) return;
      // Actual segment payload starts after the header
      uint8_t *segment_payload = payload + sizeof(OSPREY_Header);
      uint16_t segment_payload_size = length - sizeof(OSPREY_Header);

      switch(header->type) {
        case OSPREY_SEGMENT_REQUEST:
          handle_request(
            packet_info.tx, 
            header, 
            segment_payload, 
            segment_payload_size, 
            packet_info
          );
          break;
        case OSPREY_SEGMENT_SINGLE:
          handle_single_segment(
            packet_info.tx, 
            header, 
            segment_payload, 
            segment_payload_size, 
            packet_info
          );
          break;
        case OSPREY_SEGMENT_START:
        case OSPREY_SEGMENT_DATA:
          handle_multi_segment(
            packet_info.tx, 
            header, 
            segment_payload, 
            segment_payload_size, 
            packet_info
          );
          break;
        case OSPREY_SEGMENT_ACK_START:
          handle_ack_start(packet_info.tx, header);
          break;
        case OSPREY_SEGMENT_ACK:
          handle_segment_ack(packet_info.tx, header);
          break;
        case OSPREY_SEGMENT_NACK:
        case OSPREY_SEGMENT_NACK_SELECTIVE:
          handle_segment_nack(packet_info.tx, header, segment_payload, segment_payload_size);
          break;
      }
    };

    /* Handle single segment transmission */
    void handle_single_segment(
      const PJON_Endpoint &endpoint,
      OSPREY_Header *header,
      uint8_t *payload,
      uint16_t payload_size,
      const PJON_Packet_Info &packet_info
    ) {
      if(_receiver) {
        // Check if there's a pending outgoing request for this transmission_id and endpoint
        // (i.e., we sent a GET request and this is the response)
        uint8_t req_id = find_pending_request(endpoint, header->transmission_id);
        if(req_id == 255) {
          // No outgoing request found - reject unsolicited transfer
          send_response(endpoint, header->transmission_id, OSPREY_SEGMENT_NACK);
          return;
        }
        
        // Valid response to our request - clear the request and proceed
        clear_request(req_id);
        
        // Payload size excludes CRC32 (4 bytes) at the end
        uint16_t data_size = payload_size - 4;
        // Extract and verify CRC32 (last 4 bytes of payload)
        const uint8_t *crc32_pos = payload + data_size;
        
        uint32_t received_crc32 = 
            ((uint32_t)crc32_pos[0] << 24) |
            ((uint32_t)crc32_pos[1] << 16) |
            ((uint32_t)crc32_pos[2] << 8) |
            ((uint32_t)crc32_pos[3])
        ;
        
        uint32_t computed_crc32 = PJON_crc32::compute(payload, data_size);
        // CRC32 mismatch - payload corrupted, don't deliver
        if(computed_crc32 != received_crc32) return;
        // Deliver only the actual data, not the CRC32
        _receiver(payload, data_size, packet_info);
      }
    };

    /* Handle multi-segment transmission */
    void handle_multi_segment(
      const PJON_Endpoint &endpoint,
      OSPREY_Header *header,
      uint8_t *payload,
      uint16_t payload_size,
      const PJON_Packet_Info &packet_info
    ) {
      uint8_t ex_id = 
        find_exchange(endpoint, header->transmission_id, false);

      if(header->type == OSPREY_SEGMENT_START) {
        // Check if there's a pending outgoing request for this transmission_id and endpoint
        // (i.e., we sent a GET request and this is the response)
        uint8_t req_id = find_pending_request(endpoint, header->transmission_id);
        if(req_id == 255) {
          // No outgoing request found - reject unsolicited multi-segment transfer
          send_response(endpoint, header->transmission_id, OSPREY_SEGMENT_NACK);
          return;
        }
        
        // Valid response to our request - clear the request and proceed
        clear_request(req_id);
        
        // Exchange already exists, clear it
        if(ex_id != 255) clear_exchange(ex_id);  
        ex_id = find_free_exchange_slot();
        if(ex_id == 255) return; // No free slots
        
        // Extract total_segments from START segment payload (first 4 bytes)
        if(payload_size < 4) return;  // START must have at least 4 bytes for total_segments
        uint32_t total_segments = (
            ((uint32_t)payload[0] << 24) |
            ((uint32_t)payload[1] << 16) |
            ((uint32_t)payload[2] << 8) |
            ((uint32_t)payload[3])
        );
        
        // Validate segment count
        if(total_segments > OSPREY_MAX_SEGMENTS) return;
        uint32_t estimated_payload_size = 
          (uint32_t)total_segments * calculate_segment_size();
        if(estimated_payload_size > OSPREY_MAX_PAYLOAD_SIZE) return;
        // Calculate segments map size (1 bit per segment, packed into bytes)
        uint32_t segments_map_bytes = (total_segments + 7) / 8;
        // Initialize incoming exchange
        _exchanges[ex_id].endpoint = endpoint;
        _exchanges[ex_id].exchange_id = header->transmission_id;
        _exchanges[ex_id].payload = new uint8_t[estimated_payload_size];
        _exchanges[ex_id].payload_length = 0;
        _exchanges[ex_id].processed = 0;  // segments received
        _exchanges[ex_id].segments = total_segments;
        _exchanges[ex_id].last_activity = PJON_MICROS();
        _exchanges[ex_id].active = true;
        _exchanges[ex_id].is_outgoing = false;     // This is a reception
        _exchanges[ex_id].waiting_ack = false;     // Not used for incoming
        _exchanges[ex_id].payload_owned = true;    // We own the received payload
        _exchanges[ex_id].segments_map = new uint8_t[segments_map_bytes];
        _exchanges[ex_id].segments_map_size = segments_map_bytes;
        _exchanges[ex_id].nack_last_sent = PJON_MICROS();  // Initialize NACK timing
        // Initialize segments map to 0
        if(_exchanges[ex_id].segments_map)
          memset(_exchanges[ex_id].segments_map, 0, segments_map_bytes);
        if(!_exchanges[ex_id].payload || !_exchanges[ex_id].segments_map) {
          clear_exchange(ex_id);
          return; // Memory allocation failed
        }
        
        // Handle START segment payload (skip the 4-byte total_segments prefix)
        uint8_t *start_payload = payload + 4;
        uint16_t start_payload_size = payload_size - 4;
        if(start_payload_size > 0) {
          memcpy(_exchanges[ex_id].payload, start_payload, start_payload_size);
          update_segment_bit(ex_id, 0, 1, false);
          _exchanges[ex_id].processed++;
        }
        
        // Send ACK_START to confirm receiver is ready for DATA segments
        send_response(endpoint, header->transmission_id, OSPREY_SEGMENT_ACK_START);
        
        return; // START segment handling complete
      }
      if(ex_id == 255) return; // Exchange not found
      OSPREY_Exchange &ex = _exchanges[ex_id];
      if(header->segment_id >= ex.segments) return;
      // Check if this segment was already received
      if(is_segment_received(ex_id, header->segment_id)) return;
      // Calculate offset and validate bounds
      uint32_t offset = 
        (uint32_t)header->segment_id * calculate_segment_size();
      uint32_t max_allocated = ex.segments * calculate_segment_size();
      // Single comprehensive check handles both offset and size
      if(offset + payload_size > max_allocated) return;
      
      // For last DATA segment, CRC32 included in payload_size shouldn't count
      // toward actual data storage. The CRC32 bytes are after the actual data.
      uint16_t actual_data_size = payload_size;
      bool is_last_segment = (header->segment_id == ex.segments - 1);
      if(is_last_segment && payload_size >= 4) 
        actual_data_size = payload_size - 4;  // Exclude CRC32 from data copy
      
      // Store segment data (without CRC32 for last segments)
      if(actual_data_size > 0) 
        memcpy(ex.payload + offset, payload, actual_data_size);
      // Mark segment as received
      update_segment_bit(ex_id, header->segment_id, 1, false);
      ex.processed++;
      ex.last_activity = PJON_MICROS();
      // Update total payload length and extract CRC32 for last segment
      if(is_last_segment) {
        // Last segment includes CRC32 (4 bytes) at the end
        // Actual payload length excludes CRC32
        if(payload_size >= 4) 
          ex.payload_length = offset + payload_size - 4;
        else ex.payload_length = offset;
        // Extract CRC32 from last segment (last 4 bytes of payload)
        if(payload_size >= 4) {
          const uint8_t *crc32_pos = payload + payload_size - 4;
          uint32_t received_crc32 = (
              (uint32_t)crc32_pos[0] << 24) |
              ((uint32_t)crc32_pos[1] << 16) |
              ((uint32_t)crc32_pos[2] << 8) |
              ((uint32_t)crc32_pos[3])
          ;
          ex.crc32 = received_crc32;
        }
      }

      // Check if exchange is complete
      if(ex.processed == ex.segments) {
        // Verify payload CRC32
        uint32_t computed_crc32 = 
          PJON_crc32::compute(ex.payload, ex.payload_length);
        if(computed_crc32 != ex.crc32) {
          if(
            send_response(
              endpoint, header->transmission_id, OSPREY_SEGMENT_NACK
            ) != PJON_ACK
          ) if(_error) _error(OSPREY_FAIL, header->transmission_id, endpoint);
          clear_exchange(ex_id);
          return;
        } // Send ACK response
        if(
          send_response(
            endpoint, header->transmission_id, OSPREY_SEGMENT_ACK
          ) != PJON_ACK
        ) if(_error) _error(OSPREY_FAIL, header->transmission_id, endpoint);
        
        // Deliver payload to application
        if(_receiver) _receiver(ex.payload, ex.payload_length, packet_info);
        clear_exchange(ex_id);
      }
    };

    /* Handle segment ACK */
    /* Handle START ACK_START reception from receiver */
    void handle_ack_start(const PJON_Endpoint &endpoint, OSPREY_Header *header) {
      uint8_t i = find_exchange(endpoint, header->transmission_id, true);
      if(i == 255) return;
      OSPREY_Exchange &ex = _exchanges[i];
      // Mark that receiver acknowledged START segment
      if(ex.segments > 1 && ex.waiting_ack) {
        ex.start_ack_received = true;
        ex.waiting_ack = false;  // Now safe to send DATA segments
        ex.last_activity = PJON_MICROS();
      }
    };

    void handle_segment_ack(const PJON_Endpoint &endpoint, OSPREY_Header *header) {
      uint8_t i = find_exchange(endpoint, header->transmission_id, true);
      if(i != 255) clear_exchange(i);
    };

    /* Handle segment NACK */
    void handle_segment_nack(
      const PJON_Endpoint &endpoint, 
      OSPREY_Header *header,
      uint8_t *payload,
      uint16_t payload_size
    ) {
      uint8_t ex_id = find_exchange(endpoint, header->transmission_id, true);
      if(ex_id != 255) {
        // Check if this is a selective NACK
        if(header->type == OSPREY_SEGMENT_NACK_SELECTIVE) {
          // Selective NACK: parse missing segments and set up for targeted retransmission
          OSPREY_Exchange &ex = _exchanges[ex_id];
          // Allocate retransmit bitmap if not already done
          if(!ex.segments_to_retransmit) {
            uint32_t bitmap_bytes = (ex.segments + 7) / 8;
            ex.segments_to_retransmit = new uint8_t[bitmap_bytes];
            ex.segments_to_retransmit_size = bitmap_bytes;
            if(ex.segments_to_retransmit) {
              memset(ex.segments_to_retransmit, 0, bitmap_bytes);
            } else return; // Memory allocation failed
          }
          // Parse the selective NACK payload to extract missing segment list
          if(payload_size >= 4) {
            uint32_t missing_count = *(uint32_t*)payload;
            uint8_t *seg_pos = payload + 4;
            // Validate we have enough payload data
            if(payload_size >= 4 + (missing_count * 4)) {
              // Mark each missing segment for retransmission
              for(uint32_t i = 0; i < missing_count && i < 32; i++) {
                uint32_t segment_id = *(uint32_t*)(seg_pos + i * 4);
                if(segment_id < ex.segments) {
                  update_segment_bit(ex_id, segment_id, 1, true); // Mark for retransmit
                }
              }
              // Set up selective retransmission mode
              ex.waiting_ack = false;
            }
          }
        } else { 
          // Full NACK: reset exchange to retry everything
          _exchanges[ex_id].processed = 0;
          _exchanges[ex_id].waiting_ack = false;
          // Clear any partial retransmit bitmap
          if(_exchanges[ex_id].segments_to_retransmit) {
            delete[] _exchanges[ex_id].segments_to_retransmit;
            _exchanges[ex_id].segments_to_retransmit = nullptr;
            _exchanges[ex_id].segments_to_retransmit_size = 0;
          }
        }
      }
    };

    /* Send response packet (ACK or NACK) */
    uint16_t send_response(
      const PJON_Endpoint &endpoint, 
      uint16_t transmission_id, 
      uint8_t response_type
    ) {
      OSPREY_Header response_header;
      response_header.type = response_type; 
      response_header.transmission_id = transmission_id;
      response_header.segment_id = 0;
      // Calculate CRC8 of header data
      set_header_crc(response_header);
      return bus.send_packet_blocking(
        endpoint.id, 
        (uint8_t*)&response_header, 
        sizeof(OSPREY_Header)
      );
    };

    /* Handle GET request */
    void handle_request(
      const PJON_Endpoint &endpoint,
      OSPREY_Header *header,
      uint8_t *payload,
      uint16_t payload_size,
      const PJON_Packet_Info &packet_info
    ) {
      // Store request info for reply() functionality
      _last_request_endpoint = endpoint;
      _last_request_transmission_id = header->transmission_id;
      // Add request to pending requests to allow multi-segment response
      uint8_t req_id = find_free_request_slot();
      if(req_id != 255) {
        _requests[req_id].endpoint = endpoint;
        _requests[req_id].transmission_id = header->transmission_id;
        _requests[req_id].timestamp = PJON_MICROS();
        _requests[req_id].active = true;
        // Extract resource path from payload (null-terminated string)
        uint16_t path_len = (payload_size < 255) ? payload_size : 255;
        memcpy(_requests[req_id].resource_path, payload, path_len);
        _requests[req_id].resource_path[path_len] = '\0';
        // Call request handler callback if set
        if(_request_handler) {
          _request_handler(_requests[req_id].resource_path);
        } else {
          // No handler set - send NACK (404 Not Found)
          send_response(endpoint, header->transmission_id, OSPREY_SEGMENT_NACK);
          clear_request(req_id);
        }
      } else {
        // No free slots - send NACK (503 Service Unavailable)
        send_response(endpoint, header->transmission_id, OSPREY_SEGMENT_NACK);
      }
    };

    /* Find free request slot */
    uint8_t find_free_request_slot() {
      for(uint8_t i = 0; i < OSPREY_MAX_REQUESTS; i++) 
        if(!_requests[i].active) return i;
      return 255; // No free slot
    };

    /* Find pending request by endpoint and transmission_id */
    uint8_t find_pending_request(const PJON_Endpoint &endpoint, uint16_t transmission_id) {
      for(uint8_t i = 0; i < OSPREY_MAX_REQUESTS; i++) {
        if(_requests[i].active && 
           _requests[i].endpoint.id == endpoint.id &&
           _requests[i].transmission_id == transmission_id) {
          return i;
        }
      }
      return 255; // Not found
    };

    /* Clear request */
    void clear_request(uint8_t index) {
      if(index < OSPREY_MAX_REQUESTS) {
        memset(&_requests[index], 0, sizeof(OSPREY_Request));
      }
    };
};
