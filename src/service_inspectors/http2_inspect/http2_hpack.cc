//--------------------------------------------------------------------------
// Copyright (C) 2019-2020 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------
// http2_hpack.cc author Katura Harvey <katharve@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "http2_hpack.h"

#include "service_inspectors/http_inspect/http_field.h"
#include "service_inspectors/http_inspect/http_test_manager.h"

#include "http2_enum.h"
#include "http2_flow_data.h"
#include "http2_start_line.h"

using namespace HttpCommon;
using namespace Http2Enums;

Http2HpackIntDecode Http2HpackDecoder::decode_int7(7);
Http2HpackIntDecode Http2HpackDecoder::decode_int6(6);
Http2HpackIntDecode Http2HpackDecoder::decode_int5(5);
Http2HpackIntDecode Http2HpackDecoder::decode_int4(4);
Http2HpackStringDecode Http2HpackDecoder::decode_string;

bool Http2HpackDecoder::write_decoded_headers(const uint8_t* in_buffer, const uint32_t in_length,
    uint8_t* decoded_header_buffer, uint32_t decoded_header_length, uint32_t& bytes_written)
{
    bool ret = true;
    uint32_t length = in_length;
    bytes_written = 0;

    if (in_length > decoded_header_length)
    {
        length = decoded_header_length;
        *infractions += INF_DECODED_HEADER_BUFF_OUT_OF_SPACE;
        ret = false;
    }

    memcpy((void*)decoded_header_buffer, (void*) in_buffer, length);
    bytes_written = length;
    return ret;
}

bool Http2HpackDecoder::decode_string_literal(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, uint32_t& bytes_consumed,
    uint8_t* decoded_header_buffer, const uint32_t decoded_header_length,
    uint32_t& bytes_written, Field& field)
{
    bytes_written = 0;
    bytes_consumed = 0;

    if (!decode_string.translate(encoded_header_buffer, encoded_header_length, bytes_consumed,
        decoded_header_buffer, decoded_header_length, bytes_written, events, infractions,
        session_data->is_processing_partial_header()))
    {
        return false;
    }

    field.set(bytes_written, decoded_header_buffer, false);

    return true;
}

const HpackTableEntry* Http2HpackDecoder::get_hpack_table_entry(
    const uint8_t* encoded_header_buffer, const uint32_t encoded_header_length,
    const Http2HpackIntDecode& decode_int, uint32_t& bytes_consumed)
{
    uint64_t index;
    bytes_consumed = 0;

    if (!decode_int.translate(encoded_header_buffer, encoded_header_length, bytes_consumed,
        index, events, infractions, session_data->is_processing_partial_header()))
    {
        return nullptr;
    }

    const HpackTableEntry* entry = decode_table.lookup(index);
    if (!entry)
    {
        *infractions += INF_HPACK_INDEX_OUT_OF_BOUNDS;
    }
    return entry;
}

bool Http2HpackDecoder::decode_indexed_name(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, const Http2HpackIntDecode& decode_int,
    uint32_t& bytes_consumed, uint8_t* decoded_header_buffer, const uint32_t decoded_header_length,
    uint32_t& bytes_written, Field& name)
{
    bytes_written = bytes_consumed = 0;

    const HpackTableEntry* entry = get_hpack_table_entry(encoded_header_buffer,
        encoded_header_length, decode_int, bytes_consumed);
    if (!entry)
        return false;

    if (!write_decoded_headers(entry->name.start(), entry->name.length(), decoded_header_buffer,
        decoded_header_length, bytes_written))
    {
        return false;
    }

    name.set(entry->name);
    return true;
}

bool Http2HpackDecoder::decode_literal_header_line(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, const uint8_t name_index_mask,
    const Http2HpackIntDecode& decode_int, bool with_indexing, uint32_t& bytes_consumed,
    uint8_t* decoded_header_buffer, const uint32_t decoded_header_length, uint32_t& bytes_written,
    Field& name, Field& value)
{
    bytes_written = bytes_consumed = 0;
    uint32_t partial_bytes_consumed, partial_bytes_written;

    // Indexed field name
    if (encoded_header_buffer[0] & name_index_mask)
    {
        if (!decode_indexed_name(encoded_header_buffer, encoded_header_length, decode_int,
                partial_bytes_consumed, decoded_header_buffer, decoded_header_length,
                partial_bytes_written, name))
            return false;
    }

    // Literal field name
    else
    {
        // Skip over the byte with the parsed pattern and zeroed index
        bytes_consumed++;

        if (!decode_string_literal(encoded_header_buffer + bytes_consumed, encoded_header_length -
                bytes_consumed, partial_bytes_consumed, decoded_header_buffer,
                decoded_header_length, partial_bytes_written, name))
            return false;
    }
    bytes_consumed += partial_bytes_consumed;
    bytes_written += partial_bytes_written;

    if (!write_decoded_headers((const uint8_t*)": ", 2, decoded_header_buffer + bytes_written,
            decoded_header_length - bytes_written, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;

    // Value is always a string literal
    if (!decode_string_literal(encoded_header_buffer + bytes_consumed, encoded_header_length -
            bytes_consumed, partial_bytes_consumed, decoded_header_buffer + bytes_written,
            decoded_header_length - bytes_written, partial_bytes_written, value))
        return false;
    bytes_written += partial_bytes_written;
    bytes_consumed += partial_bytes_consumed;

    if (!write_decoded_headers((const uint8_t*)"\r\n", 2, decoded_header_buffer + bytes_written,
            decoded_header_length - bytes_written, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;

    if (with_indexing)
    {
        // Adding the entry to the dynamic table fails if the number of entries in the dynamic
        // table exceeds the Snort hard-coded limit of 512
        if (!decode_table.add_index(name, value))
        {
            *infractions += INF_DYNAMIC_TABLE_OVERFLOW;
            events->create_event(EVENT_DYNAMIC_TABLE_OVERFLOW);
            return false;
        }
    }
    return true;
}

bool Http2HpackDecoder::decode_indexed_header(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, const Http2HpackIntDecode& decode_int,
    uint32_t &bytes_consumed, uint8_t* decoded_header_buffer, const uint32_t decoded_header_length,
    uint32_t& bytes_written, Field& name, Field& value)
{
    uint32_t partial_bytes_written = 0;
    bytes_written = bytes_consumed = 0;

    const HpackTableEntry* const entry = get_hpack_table_entry(encoded_header_buffer,
        encoded_header_length, decode_int, bytes_consumed);
    if (!entry)
        return false;
    name.set(entry->name);
    value.set(entry->value);

    if (!write_header_part(name, (const uint8_t*)": ", 2, decoded_header_buffer,
        decoded_header_length, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;
    if (!write_header_part(value, (const uint8_t*)"\r\n", 2, decoded_header_buffer + bytes_written,
        decoded_header_length - bytes_written, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;
    return true;
}

bool Http2HpackDecoder::write_header_part(const Field& header, const uint8_t* suffix,
    uint32_t suffix_length, uint8_t* decoded_header_buffer, const uint32_t decoded_header_length,
    uint32_t& bytes_written)
{
    bytes_written = 0;
    uint32_t partial_bytes_written;

    if (!write_decoded_headers(header.start(), header.length(), decoded_header_buffer,
            decoded_header_length, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;
    if (!write_decoded_headers(suffix, suffix_length, decoded_header_buffer + bytes_written,
            decoded_header_length - bytes_written, partial_bytes_written))
        return false;
    bytes_written += partial_bytes_written;
    return true;
}

bool Http2HpackDecoder::handle_dynamic_size_update(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, uint32_t &bytes_consumed)
{
    uint64_t decoded_int;
    uint32_t encoded_bytes_consumed;
    bytes_consumed = 0;

    if (!decode_int5.translate(encoded_header_buffer, encoded_header_length,
        encoded_bytes_consumed, decoded_int, events, infractions,
        session_data->is_processing_partial_header()))
    {
        return false;
    }
    bytes_consumed += encoded_bytes_consumed;

    // Table size update shenanigans are dangerous because we cannot be sure how the target will
    // interpret them.
    if (!table_size_update_allowed)
    {
        *infractions += INF_TABLE_SIZE_UPDATE_WITHIN_HEADER;
        return false;
    }
    if (++num_table_size_updates > 2)
    {
        *infractions += INF_TOO_MANY_TABLE_SIZE_UPDATES;
        return false;
    }

    if (!decode_table.hpack_table_size_update(decoded_int))
    {
        *infractions += INF_INVALID_TABLE_SIZE_UPDATE;
        return false;
    }

    return true;
}

bool Http2HpackDecoder::decode_header_line(const uint8_t* encoded_header_buffer,
    const uint32_t encoded_header_length, uint32_t& bytes_consumed, uint8_t* decoded_header_buffer,
    const uint32_t decoded_header_length, uint32_t& bytes_written)
{
    const static uint8_t DYN_TABLE_SIZE_UPDATE_MASK = 0xe0;
    const static uint8_t DYN_TABLE_SIZE_UPDATE_PATTERN = 0x20;
    const static uint8_t INDEX_MASK = 0x80;
    const static uint8_t LITERAL_INDEX_MASK = 0x40;
    const static uint8_t LITERAL_INDEX_NAME_INDEX_MASK = 0x3f;
    const static uint8_t LITERAL_NO_INDEX_NAME_INDEX_MASK = 0x0f;

    Field name, value;
    bytes_consumed = bytes_written = 0;
    bool ret;

    if ((encoded_header_buffer[0] & DYN_TABLE_SIZE_UPDATE_MASK) == DYN_TABLE_SIZE_UPDATE_PATTERN)
        return handle_dynamic_size_update(encoded_header_buffer,
            encoded_header_length, bytes_consumed);

    table_size_update_allowed = false;

    // Indexed header representation
    if (encoded_header_buffer[0] & INDEX_MASK)
        ret = decode_indexed_header(encoded_header_buffer,
            encoded_header_length, decode_int7, bytes_consumed,
            decoded_header_buffer, decoded_header_length, bytes_written, name, value);

    // Literal header representation to be added to dynamic table
    else if (encoded_header_buffer[0] & LITERAL_INDEX_MASK)
        ret = decode_literal_header_line(encoded_header_buffer, encoded_header_length,
            LITERAL_INDEX_NAME_INDEX_MASK, decode_int6, true, bytes_consumed, decoded_header_buffer,
            decoded_header_length, bytes_written, name, value);

    // Literal header field representation not to be added to dynamic table
    // Note that this includes two representation types from the RFC - literal without index and
    // literal never index. From a decoding standpoint these are identical.
    else
        ret = decode_literal_header_line(encoded_header_buffer, encoded_header_length,
            LITERAL_NO_INDEX_NAME_INDEX_MASK, decode_int4, false, bytes_consumed,
            decoded_header_buffer, decoded_header_length, bytes_written, name, value);

    // Handle pseudoheaders
    if (ret and bytes_written > 0)
    {
        if (decoded_header_buffer[0] == ':')
        {
            if (pseudo_headers_allowed)
                start_line->process_pseudo_header(name, value);
            else
            {
                if (is_trailers)
                {
                    *infractions += INF_PSEUDO_HEADER_IN_TRAILERS;
                    events->create_event(EVENT_PSEUDO_HEADER_IN_TRAILERS);
                }
                else
                {
                    *infractions += INF_PSEUDO_HEADER_AFTER_REGULAR_HEADER;
                    events->create_event(EVENT_PSEUDO_HEADER_AFTER_REGULAR_HEADER);
                }
            }
            bytes_written = 0;
        }
        else if (pseudo_headers_allowed)
            pseudo_headers_allowed = false;
    }
    return ret;
}

// Entry point to decode an HPACK-encoded header block. This function returns true on successful
// decode and false on an unrecoverable decode error. Note that alerts may still be generated for
// recoverable errors while the function returns true. This function performs all decoding, but
// does not output the start line or decoded headers - this function must be followed by calls to
// generate_start_line() and get_decoded_headers() to generate and obtain these fields.
bool Http2HpackDecoder::decode_headers(const uint8_t* encoded_headers,
    const uint32_t encoded_headers_length, uint8_t* decoded_headers,
    Http2StartLine *start_line_generator, bool trailers)
{
    uint32_t total_bytes_consumed = 0;
    uint32_t line_bytes_consumed = 0;
    uint32_t line_bytes_written = 0;
    bool success = true;
    start_line = start_line_generator;
    decoded_headers_size = 0;
    is_trailers = trailers;
    pseudo_headers_allowed = !is_trailers;

    // A maximum of two table size updates are allowed, and must be at the start of the header
    // block
    table_size_update_allowed = true;
    num_table_size_updates = 0;

    while (success and total_bytes_consumed < encoded_headers_length)
    {
        success = decode_header_line(encoded_headers + total_bytes_consumed,
            encoded_headers_length - total_bytes_consumed, line_bytes_consumed,
            decoded_headers + decoded_headers_size, MAX_OCTETS - decoded_headers_size,
            line_bytes_written);
        total_bytes_consumed  += line_bytes_consumed;
        decoded_headers_size += line_bytes_written;
    }

    // Write the last CRLF to end the header. A truncated header may not have encountered an error
    // if the truncation is between header lines, but still shouldn't complete the header block
    // with the final CRLF.
    if (success and !session_data->is_processing_partial_header())
    {
        success = write_decoded_headers((const uint8_t*)"\r\n", 2, decoded_headers +
            decoded_headers_size, MAX_OCTETS - decoded_headers_size, line_bytes_written);
        decoded_headers_size += line_bytes_written;
    }

    return success;
}

Field Http2HpackDecoder::get_decoded_headers(const uint8_t* const decoded_headers)
{
    return Field(decoded_headers_size, decoded_headers, false);
}
