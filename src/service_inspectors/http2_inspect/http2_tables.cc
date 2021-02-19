//--------------------------------------------------------------------------
// Copyright (C) 2018-2020 Cisco and/or its affiliates. All rights reserved.
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
// http2_tables.cc author Tom Peters <thopeter@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "framework/counts.h"

#include "http2_enum.h"
#include "http2_module.h"

using namespace Http2Enums;
using namespace snort;

const RuleMap Http2Module::http2_events[] =
{
    { EVENT_INVALID_FLAG, "invalid flag set on HTTP/2 frame" },
    { EVENT_INT_LEADING_ZEROS, "HPACK integer value has leading zeros" },
    { EVENT_INVALID_STREAM_ID, "HTTP/2 stream initiated with invalid stream id" },
    { EVENT_MISSING_CONTINUATION, "missing HTTP/2 continuation frame" },
    { EVENT_UNEXPECTED_CONTINUATION, "unexpected HTTP/2 continuation frame" },
    { EVENT_MISFORMATTED_HTTP2, "misformatted HTTP/2 traffic" },
    { EVENT_PREFACE_MATCH_FAILURE, "HTTP/2 connection preface does not match" },
    { EVENT_REQUEST_WITHOUT_REQUIRED_FIELD, "HTTP/2 request missing required header field" },
    { EVENT_RESPONSE_WITHOUT_STATUS, "HTTP/2 response has no status code" },
    { EVENT_CONNECT_WITH_SCHEME_OR_PATH, "HTTP/2 CONNECT request with scheme or path" },
    { EVENT_SETTINGS_FRAME_ERROR, "error in HTTP/2 settings frame" },
    { EVENT_SETTINGS_FRAME_UNKN_PARAM, "unknown parameter in HTTP/2 settings frame" },
    { EVENT_FRAME_SEQUENCE, "invalid HTTP/2 frame sequence" },
    { EVENT_DYNAMIC_TABLE_OVERFLOW, "HTTP/2 dynamic table size limit exceeded" },
    { EVENT_INVALID_PROMISED_STREAM, "HTTP/2 push promise frame with invalid promised stream id" },
    { EVENT_PADDING_LEN, "HTTP/2 padding length is bigger than frame data size" },
    { EVENT_PSEUDO_HEADER_AFTER_REGULAR_HEADER, "HTTP/2 pseudo-header after regular header" },
    { EVENT_PSEUDO_HEADER_IN_TRAILERS, "HTTP/2 pseudo-header in trailers" },
    { EVENT_INVALID_PSEUDO_HEADER, "invalid HTTP/2 pseudo-header" },
    { EVENT_TRAILERS_NOT_END, "HTTP/2 trailers without END_STREAM bit" },
    { EVENT_PUSH_WHEN_PROHIBITED, "HTTP/2 push promise frame sent when prohibited by receiver" },
    { EVENT_PADDING_ON_EMPTY_FRAME, "padding flag set on HTTP/2 frame with zero length" },
    { EVENT_C2S_PUSH, "HTTP/2 push promise frame in c2s direction" },
    { EVENT_INVALID_PUSH_FRAME, "invalid HTTP/2 push promise frame" },
    { EVENT_BAD_PUSH_SEQUENCE, "HTTP/2 push promise frame sent at invalid time" },
    { EVENT_BAD_SETTINGS_VALUE, "invalid parameter value sent in HTTP/2 settings frame" },
    { 0, nullptr }
};

const PegInfo Http2Module::peg_names[PEG_COUNT__MAX+1] =
{
    { CountType::SUM, "flows", "HTTP/2 connections inspected" },
    { CountType::NOW, "concurrent_sessions", "total concurrent HTTP/2 sessions" },
    { CountType::MAX, "max_concurrent_sessions", "maximum concurrent HTTP/2 sessions" },
    { CountType::MAX, "max_table_entries", "maximum entries in an HTTP/2 dynamic table" },
    { CountType::MAX, "max_concurrent_files", "maximum concurrent file transfers per HTTP/2 "
        "connection" },
    { CountType::SUM, "total_bytes", "total HTTP/2 data bytes inspected" },
    { CountType::END, nullptr, nullptr }
};

