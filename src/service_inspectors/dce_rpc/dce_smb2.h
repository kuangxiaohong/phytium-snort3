//--------------------------------------------------------------------------
// Copyright (C) 2015-2020 Cisco and/or its affiliates. All rights reserved.
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

// SMB2 file processing
// Author(s):  Hui Cao <huica@cisco.com>

#ifndef DCE_SMB2_H
#define DCE_SMB2_H

#include "dce_db.h"
#include "dce_smb.h"
#include "hash/lru_cache_shared.h"
#include "main/thread_config.h"
#include "memory/memory_cap.h"
#include "utils/util.h"

#define SMB_AVG_FILES_PER_SESSION 5

struct Smb2Hdr
{
    uint8_t smb_idf[4];       /* contains 0xFE,’SMB’ */
    uint16_t structure_size;  /* This MUST be set to 64 */
    uint16_t credit_charge;   /* # of credits that this request consumes */
    uint32_t status;          /* depends */
    uint16_t command;         /* command code  */
    uint16_t credit;          /* # of credits requesting/granted */
    uint32_t flags;           /* flags */
    uint32_t next_command;    /* used for compounded request */
    uint64_t message_id;      /* identifies a message uniquely on connection */
    uint64_t async_sync;      /* used for async and sync differently */
    uint64_t session_id;      /* identifies the established session for the command*/
    uint8_t signature[16];    /* signature of the message */
};

struct Smb2ASyncHdr
{
    uint8_t smb_idf[4];       /* contains 0xFE,’SMB’ */
    uint16_t structure_size;  /* This MUST be set to 64 */
    uint16_t credit_charge;   /* # of credits that this request consumes */
    uint32_t status;          /* depends */
    uint16_t command;         /* command code  */
    uint16_t credit;          /* # of credits requesting/granted */
    uint32_t flags;           /* flags */
    uint32_t next_command;    /* used for compounded request */
    uint64_t message_id;      /* identifies a message uniquely on connection */
    uint64_t async_id;        /* handle operations asynchronously */
    uint64_t session_id;      /* identifies the established session for the command*/
    uint8_t signature[16];    /* signature of the message */
};

struct Smb2SyncHdr
{
    uint8_t smb_idf[4];       /* contains 0xFE,’SMB’ */
    uint16_t structure_size;  /* This MUST be set to 64 */
    uint16_t credit_charge;   /* # of credits that this request consumes */
    uint32_t status;          /* depends */
    uint16_t command;         /* command code  */
    uint16_t credit;          /* # of credits requesting/granted */
    uint32_t flags;           /* flags */
    uint32_t next_command;    /* used for compounded request */
    uint64_t message_id;      /* identifies a message uniquely on connection */
    uint32_t reserved;        /* reserved */
    uint32_t tree_id;         /* identifies the tree connect for the command */
    uint64_t session_id;      /* identifies the established session for the command*/
    uint8_t signature[16];    /* signature of the message */
};

struct Smb2ErrorResponseHdr
{
    uint16_t structure_size;  /* This MUST be set to 9 */
    uint16_t reserved;        /* reserved */
    uint32_t byte_count;      /* The number of bytes of error_data */
    uint8_t error_data[1];    /* If byte_count is 0, this MUST be 0*/
};

class DCE2_Smb2TreeTracker;

class DCE2_Smb2RequestTracker
{
public:

    DCE2_Smb2RequestTracker() = delete;
    DCE2_Smb2RequestTracker(const DCE2_Smb2RequestTracker& arg) = delete;
    DCE2_Smb2RequestTracker& operator=(const DCE2_Smb2RequestTracker& arg) = delete;

    DCE2_Smb2RequestTracker(uint64_t file_id_v, uint64_t offset_v = 0);
    DCE2_Smb2RequestTracker(char* fname_v, uint16_t fname_len_v);
    ~DCE2_Smb2RequestTracker();

    uint64_t get_offset()
    {
        return offset;
    }

    uint64_t get_file_id()
    {
        return file_id;
    }

    void set_file_id(uint64_t fid)
    {
        file_id = fid;
    }

    char* fname = nullptr;
    uint16_t fname_len = 0;

private:

    uint64_t file_id = 0;
    uint64_t offset = 0;
};

struct DCE2_Smb2SsnData;
class DCE2_Smb2SessionTracker;

class DCE2_Smb2FileTracker
{
public:

    DCE2_Smb2FileTracker() = delete;
    DCE2_Smb2FileTracker(const DCE2_Smb2FileTracker& arg) = delete;
    DCE2_Smb2FileTracker& operator=(const DCE2_Smb2FileTracker& arg) = delete;

    DCE2_Smb2FileTracker(uint64_t file_id_v, DCE2_Smb2TreeTracker* ttr_v,
         DCE2_Smb2SessionTracker* str_v, snort::Flow* flow_v);
    ~DCE2_Smb2FileTracker();

    bool ignore = false;
    bool upload = false;
    uint16_t file_name_len = 0;
    uint64_t bytes_processed = 0;
    uint64_t file_offset = 0;
    uint64_t file_id = 0;
    uint64_t file_size = 0;
    uint64_t file_name_hash = 0;
    char* file_name = nullptr;
    DCE2_SmbPduState smb2_pdu_state;
    DCE2_Smb2TreeTracker* ttr = nullptr;
    DCE2_Smb2SessionTracker* str = nullptr;
    snort::Flow *flow = nullptr;
};

typedef DCE2_DbMap<uint64_t, DCE2_Smb2FileTracker*, std::hash<uint64_t> > DCE2_DbMapFtracker;
typedef DCE2_DbMap<uint64_t, DCE2_Smb2RequestTracker*, std::hash<uint64_t> > DCE2_DbMapRtracker;
class DCE2_Smb2TreeTracker
{
public:

    DCE2_Smb2TreeTracker() = delete;
    DCE2_Smb2TreeTracker(const DCE2_Smb2TreeTracker& arg) = delete;
    DCE2_Smb2TreeTracker& operator=(const DCE2_Smb2TreeTracker& arg) = delete;

    DCE2_Smb2TreeTracker (uint32_t tid_v, uint8_t share_type_v);
    ~DCE2_Smb2TreeTracker();

    // File Tracker
    DCE2_Smb2FileTracker* findFtracker(uint64_t file_id)
    {
        return file_trackers.Find(file_id);
    }

    bool insertFtracker(uint64_t file_id, DCE2_Smb2FileTracker* ftracker)
    {
        return file_trackers.Insert(file_id, ftracker);
    }

    void removeFtracker(uint64_t file_id)
    {
        file_trackers.Remove(file_id);
    }

    // Request Tracker
    DCE2_Smb2RequestTracker* findRtracker(uint64_t mid)
    {
        return req_trackers.Find(mid);
    }

    bool insertRtracker(uint64_t message_id, DCE2_Smb2RequestTracker* rtracker)
    {
        return req_trackers.Insert(message_id, rtracker);
    }

    void removeRtracker(uint64_t message_id)
    {
        req_trackers.Remove(message_id);
    }

    int getRtrackerSize()
    {
        return req_trackers.GetSize();
    }

    // common methods
    uint8_t get_share_type()
    {
        return share_type;
    }

    uint32_t get_tid()
    {
        return tid;
    }

private:
    uint8_t share_type = 0;
    uint32_t tid = 0;

    DCE2_DbMapRtracker req_trackers;
    DCE2_DbMapFtracker file_trackers;
};

PADDING_GUARD_BEGIN
struct Smb2SidHashKey
{
    //must be of size 3*x*sizeof(uint32_t)
    uint32_t cip[4];
    uint32_t sip[4];
    uint64_t sid;
    int16_t cgroup;
    int16_t sgroup;
    uint16_t asid;
    uint16_t padding;

    bool operator== (const Smb2SidHashKey &other) const
    {
        return( sid == other.sid and
                cip[0] == other.cip[0] and
                cip[1] == other.cip[1] and
                cip[2] == other.cip[2] and
                cip[3] == other.cip[3] and
                sip[0] == other.sip[0] and
                sip[1] == other.sip[1] and
                sip[2] == other.sip[2] and
                sip[3] == other.sip[3] and
                cgroup == other.cgroup and
                sgroup == other.sgroup and
                asid == other.asid);
    }
};

struct SmbFlowKey
{
    uint32_t ip_l[4];   /* Low IP */
    uint32_t ip_h[4];   /* High IP */
    uint32_t mplsLabel;
    uint16_t port_l;    /* Low Port - 0 if ICMP */
    uint16_t port_h;    /* High Port - 0 if ICMP */
    int16_t group_l;
    int16_t group_h;
    uint16_t vlan_tag;
    uint16_t addressSpaceId;
    uint8_t ip_protocol;
    uint8_t pkt_type;
    uint8_t version;
    uint8_t padding;

    bool operator==(const SmbFlowKey& other) const
    {
        return (ip_l[0] == other.ip_l[0] and
               ip_l[1] == other.ip_l[1] and
               ip_l[2] == other.ip_l[2] and
               ip_l[3] == other.ip_l[3] and
               ip_h[0] == other.ip_h[0] and
               ip_l[1] == other.ip_l[1] and
               ip_l[2] == other.ip_l[2] and
               ip_l[3] == other.ip_l[3] and
               mplsLabel == other.mplsLabel and
               port_l == other.port_l and
               port_h == other.port_h and
               group_l == other.group_l and
               group_h == other.group_h and
               vlan_tag == other.vlan_tag and
               addressSpaceId == other.addressSpaceId and
               ip_protocol == other.ip_protocol and
               pkt_type == other.pkt_type and
               version == other.version);
    }
};
PADDING_GUARD_END

//The below value is taken from Hash Key class static hash hardener
#define SMB_KEY_HASH_HARDENER 133824503

struct SmbKeyHash
{
    size_t operator() (const SmbFlowKey& key) const
    {
        return  do_hash_flow_key((const uint32_t*)&key);
    }

    size_t operator() (const Smb2SidHashKey& key) const
    {
        return do_hash((const uint32_t*)&key);
    }

private:
    size_t do_hash(const uint32_t* d) const
    {
        uint32_t a, b, c;
        a = b = c = SMB_KEY_HASH_HARDENER;
        a += d[0]; b += d[1];  c += d[2];  mix(a, b, c);
        a += d[3]; b += d[4];  c += d[5];  mix(a, b, c);
        a += d[6]; b += d[7];  c += d[8];  mix(a, b, c);
        a += d[9]; b += d[10]; c += d[11]; finalize(a, b, c);
        return c;
    }

    size_t do_hash_flow_key(const uint32_t* d) const
    {
        uint32_t a, b, c;
        a = b = c = SMB_KEY_HASH_HARDENER;
        a += d[0]; b += d[1];  c += d[2];  mix(a, b, c);
        a += d[3]; b += d[4];  c += d[5];  mix(a, b, c);
        a += d[6]; b += d[7];  c += d[8];  mix(a, b, c);
        a += d[9]; b += d[10]; c += d[11]; mix(a, b, c);
        a += d[12]; finalize(a, b, c);
        return c;
    }

    inline uint32_t rot(uint32_t x, unsigned k) const
    { return (x << k) | (x >> (32 - k)); }

    inline void mix(uint32_t& a, uint32_t& b, uint32_t& c) const
    {
        a -= c; a ^= rot(c, 4); c += b;
        b -= a; b ^= rot(a, 6); a += c;
        c -= b; c ^= rot(b, 8); b += a;
        a -= c; a ^= rot(c,16); c += b;
        b -= a; b ^= rot(a,19); a += c;
        c -= b; c ^= rot(b, 4); b += a;
    }

    inline void finalize(uint32_t& a, uint32_t& b, uint32_t& c) const
    {
        c ^= b; c -= rot(b,14);
        a ^= c; a -= rot(c,11);
        b ^= a; b -= rot(a,25);
        c ^= b; c -= rot(b,16);
        a ^= c; a -= rot(c,4);
        b ^= a; b -= rot(a,14);
        c ^= b; c -= rot(b,24);
    }
};

typedef DCE2_DbMap<uint32_t, DCE2_Smb2TreeTracker*, std::hash<uint32_t> > DCE2_DbMapTtracker;
typedef DCE2_DbMap<struct SmbFlowKey, DCE2_Smb2SsnData*, SmbKeyHash> DCE2_DbMapConntracker;
class DCE2_Smb2SessionTracker
{
public:

    DCE2_Smb2SessionTracker();
    ~DCE2_Smb2SessionTracker();

    void removeSessionFromAllConnection();

    // tree tracker
    bool insertTtracker(uint32_t tree_id, DCE2_Smb2TreeTracker* ttr)
    {
        return tree_trackers.Insert(tree_id, ttr);
    }

    DCE2_Smb2TreeTracker* findTtracker(uint32_t tree_id)
    {
        return tree_trackers.Find(tree_id);
    }

    void removeTtracker(uint32_t tree_id)
    {
        tree_trackers.Remove(tree_id);
    }

    // ssd tracker
    bool insertConnTracker(SmbFlowKey key, DCE2_Smb2SsnData* ssd)
    {
        return conn_trackers.Insert(key, ssd);
    }

    DCE2_Smb2SsnData* findConnTracker(SmbFlowKey key)
    {
        return conn_trackers.Find(key);
    }

    void removeConnTracker(SmbFlowKey key)
    {
        conn_trackers.Remove(key);
    }

    int getConnTrackerSize()
    {
        return conn_trackers.GetSize();
    }

    uint16_t getTotalRequestsPending()
    {
        uint16_t total_count = 0;
        auto all_tree_trackers = tree_trackers.get_all_entry();
        for ( auto& h : all_tree_trackers )
        {
            total_count += h.second->getRtrackerSize();
        }
        return total_count;
    }

    void set_session_id(uint64_t sid)
    {
        session_id = sid;
        conn_trackers.SetDoNotFree();
    }

    DCE2_DbMapConntracker conn_trackers;
    DCE2_DbMapTtracker tree_trackers;
    Smb2SidHashKey session_key;
    uint64_t session_id = 0;
};

typedef DCE2_DbMap<uint64_t, DCE2_Smb2SessionTracker*, std::hash<uint64_t> > DCE2_DbMapStracker;
struct DCE2_Smb2SsnData
{
    DCE2_SsnData sd;  // This member must be first
    uint8_t smb_id;
    DCE2_Policy policy;
    int dialect_index;
    int ssn_state_flags;
    int64_t max_file_depth; // Maximum file depth as returned from file API
    int16_t max_outstanding_requests; // Maximum number of request that can stay pending
    DCE2_DbMapStracker session_trackers;
    DCE2_Smb2FileTracker* ftracker_tcp; //To keep tab of current file being transferred over TCP
    SmbFlowKey flow_key;
};

/* SMB2 command codes */
#define SMB2_COM_NEGOTIATE        0x00
#define SMB2_COM_SESSION_SETUP    0x01
#define SMB2_COM_LOGOFF           0x02
#define SMB2_COM_TREE_CONNECT     0x03
#define SMB2_COM_TREE_DISCONNECT  0x04
#define SMB2_COM_CREATE           0x05
#define SMB2_COM_CLOSE            0x06
#define SMB2_COM_FLUSH            0x07
#define SMB2_COM_READ             0x08
#define SMB2_COM_WRITE            0x09
#define SMB2_COM_LOCK             0x0A
#define SMB2_COM_IOCTL            0x0B
#define SMB2_COM_CANCEL           0x0C
#define SMB2_COM_ECHO             0x0D
#define SMB2_COM_QUERY_DIRECTORY  0x0E
#define SMB2_COM_CHANGE_NOTIFY    0x0F
#define SMB2_COM_QUERY_INFO       0x10
#define SMB2_COM_SET_INFO         0x11
#define SMB2_COM_OPLOCK_BREAK     0x12
#define SMB2_COM_MAX              0x13

struct Smb2WriteRequestHdr
{
    uint16_t structure_size;  /* This MUST be set to 49 */
    uint16_t data_offset;     /* offset in bytes from the beginning of smb2 header */
    uint32_t length;          /* length of data being written in bytes */
    uint64_t offset;          /* offset in the destination file */
    uint64_t fileId_persistent;  /* fileId that is persistent */
    uint64_t fileId_volatile;    /* fileId that is volatile */
    uint32_t channel;            /* channel */
    uint32_t remaining_bytes;    /* subsequent bytes the client intends to write*/
    uint16_t write_channel_info_offset;      /* channel data info */
    uint16_t write_channel_info_length;      /* channel data info */
    uint32_t flags;      /* flags*/
};

struct Smb2WriteResponseHdr
{
    uint16_t structure_size;  /* This MUST be set to 17 */
    uint16_t reserved;        /* reserved */
    uint32_t count;           /* The number of bytes written */
    uint32_t remaining;       /* MUST be 0*/
    uint16_t write_channel_info_offset;      /* channel data info */
    uint16_t write_channel_info_length;      /* channel data info */
};

struct Smb2ReadRequestHdr
{
    uint16_t structure_size;  /* This MUST be set to 49 */
    uint8_t padding;          /* Padding */
    uint8_t flags;            /* Flags */
    uint32_t length;          /* length of data to read from the file */
    uint64_t offset;          /* offset in the destination file */
    uint64_t fileId_persistent;  /* fileId that is persistent */
    uint64_t fileId_volatile;    /* fileId that is volatile */
    uint32_t minimum_count;      /* The minimum # of bytes to be read */
    uint32_t channel;            /* channel */
    uint32_t remaining_bytes;    /* subsequent bytes the client intends to read*/
    uint16_t read_channel_info_offset;      /* channel data info */
    uint16_t read_channel_info_length;      /* channel data info */
};

struct Smb2ReadResponseHdr
{
    uint16_t structure_size; /* This MUST be set to 17 */
    uint8_t data_offset;     /* offset in bytes from beginning of smb2 header*/
    uint8_t reserved;        /* reserved */
    uint32_t length;         /* The number of bytes being returned in response */
    uint32_t remaining;      /* The number of data being sent on the channel*/
    uint32_t reserved2;      /* reserved */
};

struct Smb2SetInfoRequestHdr
{
    uint16_t structure_size;   /* This MUST be set to 33 */
    uint8_t info_type;         /* info type */
    uint8_t file_info_class;   /* file info class after header */
    uint32_t buffer_length;    /* buffer length */
    uint16_t buffer_offset;    /* buffer offset */
    uint16_t reserved;         /* reserved */
    uint32_t additional_info;  /* additional information */
    uint64_t fileId_persistent; /* fileId that is persistent */
    uint64_t fileId_volatile;  /* fileId that is volatile */
};

struct Smb2CreateRequestHdr
{
    uint16_t structure_size;          /* This MUST be set to 57 */
    uint8_t security_flags;           /* security flag, should be 0 */
    uint8_t requested_oplock_level;   /* */
    uint32_t impersonation_level;     /* */
    uint64_t smb_create_flags;        /* should be 0 */
    uint64_t reserved;                /* can be any value */
    uint32_t desired_access;          /*  */
    uint32_t file_attributes;         /* */
    uint32_t share_access;            /* READ WRITE DELETE etc */
    uint32_t create_disposition;      /* actions when file exists*/
    uint32_t create_options;          /* options for creating file*/
    uint16_t name_offset;             /* file name offset from SMB2 header */
    uint16_t name_length;             /* length of file name */
    uint32_t create_contexts_offset;  /* offset of contexts from beginning of header */
    uint32_t create_contexts_length;  /* length of contexts */
};

// file attribute for create response
#define SMB2_CREATE_RESPONSE_DIRECTORY 0x10

struct Smb2CreateResponseHdr
{
    uint16_t structure_size;          /* This MUST be set to 89 */
    uint8_t oplock_level;             /* oplock level granted, values limited */
    uint8_t flags;                    /* flags, values limited */
    uint32_t create_action;           /* action taken, values limited */
    uint64_t creation_time;           /* time created */
    uint64_t last_access_time;        /* access time */
    uint64_t last_write_time;         /* write  time */
    uint64_t change_time;             /* time modified*/
    uint64_t allocation_size;         /* size allocated */
    uint64_t end_of_file;             /* file size*/
    uint32_t file_attributes;         /* attributes of the file*/
    uint32_t reserved2;               /* */
    uint64_t fileId_persistent;       /* fileId that is persistent */
    uint64_t fileId_volatile;         /* fileId that is volatile */
    uint32_t create_contexts_offset;  /*  */
    uint32_t create_contexts_length;  /*  */
};

struct Smb2CloseRequestHdr
{
    uint16_t structure_size;          /* This MUST be set to 24 */
    uint16_t flags;                   /* flags */
    uint32_t reserved;                /* can be any value */
    uint64_t fileId_persistent;       /* fileId that is persistent */
    uint64_t fileId_volatile;         /* fileId that is volatile */
};

#define SMB2_SHARE_TYPE_DISK  0x01
#define SMB2_SHARE_TYPE_PIPE  0x02
#define SMB2_SHARE_TYPE_PRINT 0x03

struct Smb2TreeConnectResponseHdr
{
    uint16_t structure_size;          /* This MUST be set to 16 */
    uint8_t share_type;               /* type of share being accessed */
    uint8_t reserved;                 /* reserved */
    uint32_t share_flags;             /* properties for this share*/
    uint32_t capabilities;            /* various capabilities for this share */
    uint32_t maximal_access;          /* maximal access for the user */
};

struct Smb2TreeDisConnectHdr
{
    uint16_t structure_size;          /* This MUST be set to 4 */
    uint16_t reserved;                 /* reserved */
};

struct  Smb2SetupRequestHdr
{
    uint16_t structure_size;            /* This MUST be set to 25 (0x19) bytes */
    uint8_t flags;
    uint8_t security_mode;
    uint32_t capabilities;
    uint32_t channel;
    uint16_t secblob_ofs;
    uint16_t secblob_size;
    uint64_t previous_sessionid;
};

struct Smb2SetupResponseHdr
{
    uint16_t structure_size;            /* This MUST be set to 9 (0x09) bytes */
    uint16_t session_flags;
    uint16_t secblob_ofs;
    uint16_t secblob_size;
};

#define SMB2_HEADER_LENGTH 64

#define SMB2_ERROR_RESPONSE_STRUC_SIZE 9

#define SMB2_CREATE_REQUEST_STRUC_SIZE 57
#define SMB2_CREATE_RESPONSE_STRUC_SIZE 89
#define SMB2_CREATE_REQUEST_DATA_OFFSET 120

#define SMB2_CLOSE_REQUEST_STRUC_SIZE 24
#define SMB2_CLOSE_RESPONSE_STRUC_SIZE 60

#define SMB2_WRITE_REQUEST_STRUC_SIZE 49
#define SMB2_WRITE_RESPONSE_STRUC_SIZE 17

#define SMB2_READ_REQUEST_STRUC_SIZE 49
#define SMB2_READ_RESPONSE_STRUC_SIZE 17

#define SMB2_SET_INFO_REQUEST_STRUC_SIZE 33
#define SMB2_SET_INFO_RESPONSE_STRUC_SIZE 2

#define SMB2_TREE_CONNECT_REQUEST_STRUC_SIZE 9
#define SMB2_TREE_CONNECT_RESPONSE_STRUC_SIZE 16
#define SMB2_TREE_DISCONNECT_REQUEST_STRUC_SIZE 4

#define SMB2_FILE_ENDOFFILE_INFO 0x14

#define SMB2_SETUP_REQUEST_STRUC_SIZE 25
#define SMB2_SETUP_RESPONSE_STRUC_SIZE 9

#define SMB2_LOGOFF_REQUEST_STRUC_SIZE 4

extern const char* smb2_command_string[SMB2_COM_MAX];
/* Process smb2 message */
void DCE2_Smb2Process(DCE2_Smb2SsnData* ssd);

/* Check smb version based on smb header */
DCE2_SmbVersion DCE2_Smb2Version(const snort::Packet* p);

#endif  /* _DCE_SMB2_H_ */

