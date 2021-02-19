//--------------------------------------------------------------------------
// Copyright (C) 2016-2020 Cisco and/or its affiliates. All rights reserved.
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

// host_tracker.cc author Steve Chew <stechew@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <algorithm>

#include "host_cache.h"
#include "host_cache_allocator.cc"
#include "host_tracker.h"
#include "utils/util.h"

using namespace snort;
using namespace std;

#define USER_LOGIN_SUCCESS 1
#define USER_LOGIN_FAILURE 2

THREAD_LOCAL struct HostTrackerStats host_tracker_stats;

const uint8_t snort::zero_mac[MAC_SIZE] = {0, 0, 0, 0, 0, 0};


HostTracker::HostTracker() : hops(-1)
{
    last_seen = nat_count_start = (uint32_t) packet_time();
    last_event = -1;
    visibility = host_cache.get_valid_id();
}

void HostTracker::update_last_seen()
{
    lock_guard<mutex> lck(host_tracker_lock);
    last_seen = (uint32_t) packet_time();
}

void HostTracker::update_last_event(uint32_t time)
{
    lock_guard<mutex> lck(host_tracker_lock);
    last_event = time ? time : last_seen;
}

bool HostTracker::add_network_proto(const uint16_t type)
{
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& proto : network_protos )
    {
        if ( proto.first == type )
        {
            if ( proto.second )
                return false;
            else
            {
                proto.second = true;
                return true;
            }
        }
    }

    network_protos.emplace_back(type, true);
    return true;
}

bool HostTracker::add_xport_proto(const uint8_t type)
{
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& proto : xport_protos )
    {
        if ( proto.first == type )
        {
            if ( proto.second )
                return false;
            else
            {
                proto.second = true;
                return true;
            }
        }
    }

    xport_protos.emplace_back(type, true);
    return true;
}

bool HostTracker::add_mac(const uint8_t* mac, uint8_t ttl, uint8_t primary)
{
    if ( !mac or !memcmp(mac, zero_mac, MAC_SIZE) )
        return false;

    HostMac_t* invisible_swap_candidate = nullptr;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& hm_t : macs )
    {
        if ( !memcmp(mac, hm_t.mac, MAC_SIZE) )
        {
            if ( hm_t.visibility )
            {
                return false;
            }

            hm_t.visibility = true;
            num_visible_macs++;
            return true;
        }

        if ( !invisible_swap_candidate and !hm_t.visibility )
            invisible_swap_candidate = &hm_t;
    }

    if ( invisible_swap_candidate )
    {
        memcpy(invisible_swap_candidate->mac, mac, MAC_SIZE);
        invisible_swap_candidate->ttl = ttl;
        invisible_swap_candidate->primary = primary;
        invisible_swap_candidate->visibility = true;
        num_visible_macs++;
        return true;
    }

    macs.emplace_back(ttl, mac, primary, last_seen);
    num_visible_macs++;

    return true;
}

bool HostTracker::add_payload_no_lock(const AppId pld, HostApplication* ha, size_t max_payloads)
{
    Payload_t* invisible_swap_candidate = nullptr;

    for ( auto& p : ha->payloads )
    {
        if ( p.first == pld )
        {
            if ( p.second )
            {
                return false;
            }
            else
            {
                p.second = true;
                ha->num_visible_payloads++;
                return true;
            }
        }

        if ( !invisible_swap_candidate and !p.second )
            invisible_swap_candidate = &p;
    }

    if ( invisible_swap_candidate )
    {
        invisible_swap_candidate->first = pld;
        invisible_swap_candidate->second = true;
        ha->num_visible_payloads++;
        return true;
    }

    if ( ha->payloads.size() >= max_payloads )
        return false;

    ha->payloads.emplace_back(pld, true);
    ha->num_visible_payloads++;

    return true;
}

bool HostTracker::get_hostmac(const uint8_t* mac, HostMac& hm)
{
    if ( !mac or !memcmp(mac, zero_mac, MAC_SIZE) )
        return false;

    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& ahm : macs )
        if ( !memcmp(mac, ahm.mac, MAC_SIZE) )
        {
            if (!ahm.visibility)
                return false;

            hm = static_cast<HostMac>(ahm);
            return true;
        }

    return false;
}

const uint8_t* HostTracker::get_last_seen_mac()
{
    lock_guard<mutex> lck(host_tracker_lock);
    const HostMac_t* max_hm = nullptr;

    for ( const auto& hm : macs )
        if ( !max_hm or max_hm->last_seen < hm.last_seen )
            if (hm.visibility)
                max_hm = &hm;

    if ( max_hm )
        return max_hm->mac;

    return zero_mac;
}

bool HostTracker::update_mac_ttl(const uint8_t* mac, uint8_t new_ttl)
{
    if ( !mac or !memcmp(mac, zero_mac, MAC_SIZE) )
        return false;

    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& hm : macs )
        if ( !memcmp(mac, hm.mac, MAC_SIZE) )
        {
            if (hm.ttl < new_ttl and hm.visibility)
            {
                hm.ttl = new_ttl;
                return true;
            }

            return false;
        }

    return false;
}

bool HostTracker::make_primary(const uint8_t* mac)
{
    if ( !mac or !memcmp(mac, zero_mac, MAC_SIZE) )
        return false;

    HostMac_t* hm = nullptr;

    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& hm_iter : macs )
        if ( !memcmp(mac, hm_iter.mac, MAC_SIZE) )
        {
            if ( !hm_iter.visibility )
                return false;

            hm = &hm_iter;
            break;
        }

    if ( !hm )
        return false;

    hm->last_seen = last_seen;
    if ( !hm->primary )
    {
        hm->primary = true;
        return true;
    }

    return false;
}

HostMac* HostTracker::get_max_ttl_hostmac()
{
    lock_guard<mutex> lck(host_tracker_lock);

    HostMac_t* max_ttl_hm = nullptr;
    uint8_t max_ttl = 0;

    for ( auto& hm : macs )
    {
        if ( hm.primary and hm.visibility )
            return static_cast<HostMac*> (&hm);

        if ( hm.ttl > max_ttl and hm.visibility )
        {
            max_ttl = hm.ttl;
            max_ttl_hm = &hm;
        }
    }

    return static_cast<HostMac*>(max_ttl_hm);
}

void HostTracker::update_vlan(uint16_t vth_pri_cfi_vlan, uint16_t vth_proto)
{
    vlan_tag_present = true;
    vlan_tag.vth_pri_cfi_vlan = vth_pri_cfi_vlan;
    vlan_tag.vth_proto = vth_proto;
}

bool HostTracker::has_vlan()
{
    return vlan_tag_present;
}

uint16_t HostTracker::get_vlan()
{
    return vlan_tag.vth_pri_cfi_vlan;
}

void HostTracker::get_vlan_details(uint8_t& cfi, uint8_t& priority, uint16_t& vid)
{
    cfi = vlan_tag.cfi();
    priority = vlan_tag.priority();
    vid = vlan_tag.vid();
}

void HostTracker::copy_data(uint8_t& p_hops, uint32_t& p_last_seen, list<HostMac>*& p_macs)
{
    lock_guard<mutex> lck(host_tracker_lock);

    p_hops = hops;
    p_last_seen = last_seen;
    if ( !macs.empty() )
        p_macs = new list<HostMac>(macs.begin(), macs.end());
}

bool HostTracker::add_service(Port port, IpProtocol proto, AppId appid, bool inferred_appid,
    bool* added)
{
    host_tracker_stats.service_adds++;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& s : services )
    {
        if ( s.port == port and s.proto == proto )
        {
            if ( s.appid != appid and appid != APP_ID_NONE )
            {
                s.appid = appid;
                s.inferred_appid = inferred_appid;
                if (added)
                    *added = true;
            }

            if ( s.visibility == false )
            {
                if ( added )
                    *added = true;
                s.visibility = true;
                num_visible_services++;
            }

            return true;
        }
    }

    services.emplace_back(port, proto, appid, inferred_appid);
    num_visible_services++;
    if (added)
        *added = true;

    return true;
}

void HostTracker::clear_service(HostApplication& ha)
{
    lock_guard<mutex> lck(host_tracker_lock);
    ha.port = 0;
    ha.proto = (IpProtocol) 0;
    ha.appid = (AppId) 0;
    ha.inferred_appid = false;
    ha.hits = 0;
    ha.last_seen = 0;
    ha.payloads.clear();
    ha.info.clear();
    ha.banner_updated = false;
}

bool HostTracker::add_client_payload(HostClient& hc, AppId payload, size_t max_payloads)
{
    Payload_t* invisible_swap_candidate = nullptr;
    std::lock_guard<std::mutex> lck(host_tracker_lock);

    for ( auto& client : clients )
        if ( client.id == hc.id and client.service == hc.service )
        {
            for ( auto& pld : client.payloads )
            {
                if ( pld.first == payload )
                {
                    if ( pld.second )
                        return false;

                    pld.second = true;
                    client.num_visible_payloads++;
                    return true;
                }
                if ( !invisible_swap_candidate and !pld.second )
                    invisible_swap_candidate = &pld;
            }

            if ( invisible_swap_candidate )
            {
                invisible_swap_candidate->second = true;
                invisible_swap_candidate->first = payload;
                client.num_visible_payloads++;
                hc.payloads = client.payloads;
                return true;
            }

            if ( client.payloads.size() >= max_payloads )
                return false;

            client.payloads.emplace_back(payload, true);
            hc.payloads = client.payloads;
            strncpy(hc.version, client.version, INFO_SIZE);
            client.num_visible_payloads++;
            return true;
        }

    return false;
}

bool HostTracker::add_service(const HostApplication& app, bool* added)
{
    host_tracker_stats.service_adds++;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& s : services )
    {
        if ( s.port == app.port and s.proto == app.proto )
        {
            if ( s.appid != app.appid and app.appid != APP_ID_NONE )
            {
                s.appid = app.appid;
                s.inferred_appid = app.inferred_appid;
                if (added)
                    *added = true;
            }

            if ( s.visibility == false )
            {
                if (added)
                    *added = true;
                s.visibility = true;
                num_visible_services++;
            }

            return true;
        }
    }

    services.emplace_back(app.port, app.proto, app.appid, app.inferred_appid);
    num_visible_services++;
    if ( added )
        *added = true;

    return true;
}

AppId HostTracker::get_appid(Port port, IpProtocol proto, bool inferred_only,
    bool allow_port_wildcard)
{
    host_tracker_stats.service_finds++;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( const auto& s : services )
    {
        bool matched = (s.port == port and s.proto == proto and
            (!inferred_only or s.inferred_appid == inferred_only));
        if ( matched or ( allow_port_wildcard and s.inferred_appid ) )
            return s.appid;
    }

    return APP_ID_NONE;
}

size_t HostTracker::get_service_count()
{
    lock_guard<mutex> lck(host_tracker_lock);
    return num_visible_services;
}

HostApplication* HostTracker::find_service_no_lock(Port port, IpProtocol proto, AppId appid)
{
    for ( auto& s : services )
    {
        if ( s.port == port and s.proto == proto )
        {
            if (s.visibility == false)
                return nullptr;
            if ( appid != APP_ID_NONE and s.appid == appid )
                return &s;
        }
    }

    return nullptr;
}

bool HostTracker::add_payload(HostApplication& local_ha, Port port, IpProtocol proto, AppId payload,
    AppId service, size_t max_payloads)
{
    // This lock is responsible for find_service and add_payload
    lock_guard<mutex> lck(host_tracker_lock);

    auto ha = find_service_no_lock(port, proto, service);

    if ( ha )
    {
        bool success = add_payload_no_lock(payload, ha, max_payloads);
        local_ha = *ha;
        local_ha.payloads = ha->payloads;
        return success;
    }

    return false;
}

HostApplication* HostTracker::find_and_add_service_no_lock(Port port, IpProtocol proto,
    uint32_t lseen, bool& is_new, AppId appid, uint16_t max_services)
{
    host_tracker_stats.service_finds++;
    HostApplication *available = nullptr;

    for ( auto& s : services )
    {
        if ( s.port == port and s.proto == proto )
        {
            if ( (appid != APP_ID_NONE and s.appid != appid) or !s.visibility )
            {
                s.appid = appid;
                is_new = true;
                s.hits = 1;
                if ( !s.visibility )
                {
                    s.visibility = true;
                    num_visible_services++;
                }
                else
                    s.hits = 0;
            }
            else if ( s.last_seen == 0 )
            {
                is_new = true;
                s.hits = 1;
            }
            else
                ++s.hits;

            s.last_seen = lseen;

            return &s;
        }
        else if ( !available and !s.visibility )
            available = &s;
    }

    is_new = true;
    host_tracker_stats.service_adds++;
    num_visible_services++;
    if ( available )
    {
        available->port = port;
        available->proto = proto;
        available->appid = appid;
        available->hits = 1;
        available->last_seen = lseen;
        available->inferred_appid = false;
        available->user[0] = '\0';
        available->user_login = 0;
        available->banner_updated = false;
        available->visibility = true;
        return available;
    }

    if ( max_services == 0 or num_visible_services < max_services )
    {
        services.emplace_back(port, proto, appid, false, 1, lseen);
        return &services.back();
    }
    return nullptr;
}

HostApplication HostTracker::add_service(Port port, IpProtocol proto, uint32_t lseen,
    bool& is_new, AppId appid)
{
    lock_guard<mutex> lck(host_tracker_lock);
    HostApplication* ha = find_and_add_service_no_lock(port, proto, lseen, is_new, appid);
    return *ha;
}

void HostTracker::update_service(const HostApplication& ha)
{
    host_tracker_stats.service_finds++;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& s : services )
    {
        if ( s.port == ha.port and s.proto == ha.proto )
        {
            s.hits = ha.hits;
            s.last_seen = ha.last_seen;
            return;
        }
    }
}

void HostTracker::update_service_port(HostApplication& app, Port port)
{
    lock_guard<mutex> lck(host_tracker_lock);
    app.port = port;
}

void HostTracker::update_service_proto(HostApplication& app, IpProtocol proto)
{
    lock_guard<mutex> lck(host_tracker_lock);
    app.proto = proto;
}

void HostTracker::update_ha_no_lock(HostApplication& dst, HostApplication& src)
{
    if ( dst.appid == APP_ID_NONE )
        dst.appid = src.appid;
    else
        src.appid = dst.appid;

    for ( auto& i: src.info )
        if ( i.visibility == true )
            dst.info.emplace_back(i.version, i.vendor);

    dst.hits = src.hits;
}

bool HostTracker::update_service_info(HostApplication& ha, const char* vendor,
    const char* version, uint16_t max_info)
{
    host_tracker_stats.service_finds++;
    lock_guard<mutex> lck(host_tracker_lock);

    for ( auto& s : services )
    {
        if ( s.port == ha.port and s.proto == ha.proto )
        {
            if ( s.visibility == false )
                return false;

            if ( !version and !vendor )
                return true;

            HostApplicationInfo* available = nullptr;
            for ( auto& i : s.info )
            {
                if ( (version and !strncmp(version, i.version, INFO_SIZE-1)) and
                    (vendor and !strncmp(vendor, i.vendor, INFO_SIZE-1)) )
                {
                    if ( i.visibility == false )
                    {
                        i.visibility = true;  // rediscover it
                        update_ha_no_lock(ha, s);
                        return true;
                    }
                    return false;
                }
                else if ( !available and !i.visibility )
                    available = &i;
            }

            if ( available and (version or vendor) )
            {
                if ( version )
                {
                    strncpy(available->version, version, INFO_SIZE);
                    available->version[INFO_SIZE-1]='\0';
                }

                if ( vendor )
                {
                    strncpy(available->vendor, vendor, INFO_SIZE);
                    available->vendor[INFO_SIZE-1]='\0';
                }

                available->visibility = true;
            }
            else if ( s.info.size() < max_info )
                s.info.emplace_back(version, vendor);
            else
                return false;

            update_ha_no_lock(ha, s);
            return true;
        }
    }
    return false;
}

bool HostTracker::update_service_banner(Port port, IpProtocol proto)
{
    host_tracker_stats.service_finds++;
    lock_guard<mutex> lck(host_tracker_lock);
    for ( auto& s : services )
    {
        if ( s.port == port and s.proto == proto )
        {
            if ( !s.visibility or s.banner_updated )
                return false;

            s.banner_updated = true;
            return true;
        }
    }
    return false;
}

bool HostTracker::update_service_user(Port port, IpProtocol proto, const char* user,
    uint32_t lseen, uint16_t max_services, bool success)
{
    host_tracker_stats.service_finds++;
    bool is_new = false;
    lock_guard<mutex> lck(host_tracker_lock);

    // Appid notifies user events before service events, so use find or add service function.
    HostApplication* ha = find_and_add_service_no_lock(port, proto, lseen, is_new, 0,
        max_services);
    if ( !ha or ha->visibility == false )
        return false;

    if ( user and strncmp(user, ha->user, INFO_SIZE-1) )
    {
        strncpy(ha->user, user, INFO_SIZE);
        ha->user[INFO_SIZE-1] = '\0';
        ha->user_login = success ? USER_LOGIN_SUCCESS : USER_LOGIN_FAILURE;
        return true;
    }

    if ( success )
    {
        if ( ha->user_login & USER_LOGIN_SUCCESS )
            return false;
        ha->user_login |= USER_LOGIN_SUCCESS;
        return true;
    }
    else
    {
        if ( ha->user_login & USER_LOGIN_FAILURE )
            return false;
        ha->user_login |= USER_LOGIN_FAILURE;
        return true;
    }
}

void HostTracker::remove_inferred_services()
{
    lock_guard<mutex> lck(host_tracker_lock);
    for ( auto s = services.begin(); s != services.end(); )
    {
        if (s->inferred_appid)
            s = services.erase(s);
        else
            s++;
    }
}

bool HostTracker::add_tcp_fingerprint(uint32_t fpid)
{
    lock_guard<mutex> lck(host_tracker_lock);
    auto result = tcp_fpids.emplace(fpid);
    return result.second;
}

bool HostTracker::add_udp_fingerprint(uint32_t fpid)
{
    lock_guard<mutex> lck(host_tracker_lock);
    auto result = udp_fpids.emplace(fpid);
    return result.second;
}

bool HostTracker::set_visibility(bool v)
{
    // get_valid_id may use its own lock, so get this outside our lock
    size_t container_id = host_cache.get_valid_id();

    std::lock_guard<std::mutex> lck(host_tracker_lock);
    size_t old_visibility = visibility;

    visibility = v ? container_id : HostCacheIp::invalid_id;

    if (visibility == HostCacheIp::invalid_id)
    {
        for (auto& proto : network_protos)
            proto.second = false;

        for (auto& proto : xport_protos)
            proto.second = false;

        for (auto& mac_t : macs)
            mac_t.visibility = false;

        num_visible_macs = 0;

        for (auto& s : services)
        {
            s.visibility = false;
            for (auto& info : s.info)
                info.visibility = false;
            s.user[0] = '\0';
            set_payload_visibility_no_lock(s.payloads, false, s.num_visible_payloads);
        }
        num_visible_services = 0;

        for ( auto& c : clients )
        {
            c.visibility = false;
            set_payload_visibility_no_lock(c.payloads, false, c.num_visible_payloads);
        }
        num_visible_clients = 0;

        tcp_fpids.clear();
        ua_fps.clear();
    }

    return old_visibility == visibility;
}

bool HostTracker::is_visible() const
{
    std::lock_guard<std::mutex> lck(host_tracker_lock);
    return visibility == host_cache.get_valid_id();
}


bool HostTracker::set_network_proto_visibility(uint16_t proto, bool v)
{
    std::lock_guard<std::mutex> lck(host_tracker_lock);
    for ( auto& pp : network_protos )
    {
        if ( pp.first == proto )
        {
            pp.second = v;
            return true;
        }
    }
    return false;
}

bool HostTracker::set_xproto_visibility(uint8_t proto, bool v)
{
    std::lock_guard<std::mutex> lck(host_tracker_lock);
    for ( auto& pp : xport_protos )
    {
        if ( pp.first == proto )
        {
            pp.second = v;
            return true;
        }
    }
    return false;
}

void HostTracker::set_payload_visibility_no_lock(PayloadVector& pv, bool v, size_t& num_vis)
{
    for ( auto& p : pv )
    {
        if ( p.second != v )
        {
            p.second = v;
            if ( v )
                num_vis++;
            else
                num_vis--;
        }
    }
}

bool HostTracker::set_service_visibility(Port port, IpProtocol proto, bool v)
{
    std::lock_guard<std::mutex> lck(host_tracker_lock);
    for ( auto& s : services )
    {
        if ( s.port == port and s.proto == proto )
        {
            if ( s.visibility == true and v == false )
            {
                assert(num_visible_services > 0);
                num_visible_services--;
            }
            else if ( s.visibility == false and v == true )
                num_visible_services++;

            s.visibility = v;
            if ( s.visibility == false )
            {
                for ( auto& info : s.info )
                    info.visibility = false;
                s.user[0] = '\0';
                s.banner_updated = false;
            }

            set_payload_visibility_no_lock(s.payloads, v, s.num_visible_payloads);
            return true;
        }
    }
    return false;
}

bool HostTracker::set_client_visibility(const HostClient& hc, bool v)
{
    std::lock_guard<std::mutex> lck(host_tracker_lock);
    bool deleted = false;
    for ( auto& c : clients )
    {
        if ( c == hc )
        {
            if ( c.visibility == true and v == false )
            {
                assert(num_visible_clients > 0 );
                num_visible_clients--;
            }
            else if ( c.visibility == false and v == true )
                num_visible_clients++;

            c.visibility = v;
            set_payload_visibility_no_lock(c.payloads, v, c.num_visible_payloads);
            deleted = true;
        }
    }
    return deleted;
}

DeviceFingerprint::DeviceFingerprint(uint32_t id, uint32_t type, bool jb, const char* dev) :
    fpid(id), fp_type(type), jail_broken(jb)
{
    if ( dev )
    {
        strncpy(device, dev, INFO_SIZE);
        device[INFO_SIZE-1] = '\0';
    }
}

bool HostTracker::add_ua_fingerprint(uint32_t fpid, uint32_t fp_type, bool jail_broken,
    const char* device, uint8_t max_devices)
{
    lock_guard<mutex> lck(host_tracker_lock);

    int count = 0;
    for ( const auto& fp : ua_fps )
    {
        if ( fpid != fp.fpid or fp_type != fp.fp_type )
            continue;
        ++count; // only count same fpid with different device information
        if ( count >= max_devices )
            return false;
        if ( jail_broken == fp.jail_broken and ( ( !device and fp.device[0] == '\0') or
            ( device and strncmp(fp.device, device, INFO_SIZE) == 0) ) )
            return false;
    }

    ua_fps.emplace_back(fpid, fp_type, jail_broken, device);
    return true;
}

size_t HostTracker::get_client_count()
{
    lock_guard<mutex> lck(host_tracker_lock);
    return num_visible_clients;
}

HostClient::HostClient(AppId clientid, const char *ver, AppId ser) :
    id(clientid), service(ser)
{
    if ( ver )
    {
        strncpy(version, ver, INFO_SIZE);
        version[INFO_SIZE-1] = '\0';
    }
}

HostClient HostTracker::find_or_add_client(AppId id, const char* version, AppId service,
    bool& is_new)
{
    lock_guard<mutex> lck(host_tracker_lock);
    HostClient* available = nullptr;
    for ( auto& c : clients )
    {
        if ( c.id != APP_ID_NONE and c.id == id and c.service == service
            and ((c.version[0] == '\0' and !version) or
            (version and strncmp(c.version, version, INFO_SIZE-1) == 0)) )
        {
            if ( c.visibility == false )
            {
                is_new = true;
                c.visibility = true;
                num_visible_clients++;
            }

            return c;
        }
        else if ( !available and !c.visibility )
            available = &c;
    }

    is_new = true;
    num_visible_clients++;
    if ( available )
    {
        available->id = id;
        available->service = service;
        available->visibility = true;
        if ( version )
        {
            strncpy(available->version, version, INFO_SIZE);
            available->version[INFO_SIZE-1] = '\0';
        }
        return *available;
    }

    clients.emplace_back(id, version, service);
    return clients.back();
}

HostApplicationInfo::HostApplicationInfo(const char *ver, const char *ven)
{
    if ( ver )
    {
        strncpy(version, ver, INFO_SIZE);
        version[INFO_SIZE-1] = '\0';
    }
    if ( ven )
    {
        strncpy(vendor, ven, INFO_SIZE);
        vendor[INFO_SIZE-1] = '\0';
    }
}

static inline string to_time_string(uint32_t p_time)
{
    time_t raw_time = (time_t) p_time;
    struct tm* timeinfo = gmtime(&raw_time);
    char buffer[30];
    strftime(buffer, 30, "%F %T", timeinfo);
    return buffer;
}

static inline string to_mac_string(const uint8_t* mac)
{
    char mac_addr[18];
    snprintf(mac_addr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return mac_addr;
}

static std::vector<std::string> host_types = { "Host", "Router", "Bridge", "NAT", "Load Balancer" };

static inline string& to_host_type_string(HostType type)
{
    return host_types[type];
}

void HostTracker::stringify(string& str)
{
    lock_guard<mutex> lck(host_tracker_lock);

    str += "\n    type: " + to_host_type_string(host_type) + ", ttl: " + to_string(ip_ttl)
        + ", hops: " + to_string(hops) + ", time: " + to_time_string(last_seen);

    if ( !macs.empty() )
    {
        str += "\nmacs size: " + to_string(num_visible_macs);
        for ( const auto& m : macs )
        {
            if ( m.visibility )
            {
                str += "\n    mac: " + to_mac_string(m.mac)
                    + ", ttl: " + to_string(m.ttl)
                    + ", primary: " + to_string(m.primary)
                    + ", time: " + to_time_string(m.last_seen);
            }
        }
    }

    if ( num_visible_services > 0 )
    {
        str += "\nservices size: " + to_string(num_visible_services);

        for ( const auto& s : services )
        {
            if (s.visibility == false)
                continue;

            str += "\n    port: " + to_string(s.port)
                + ", proto: " + to_string((uint8_t) s.proto);
            if ( s.appid != APP_ID_NONE )
            {
                str += ", appid: " + to_string(s.appid);
                if ( s.inferred_appid )
                    str += ", inferred";
            }

            if ( !s.info.empty() )
                for ( const auto& i : s.info )
                {
                    if (i.visibility == false)
                        continue;

                    if ( i.vendor[0] != '\0' )
                        str += ", vendor: " + string(i.vendor);
                    if ( i.version[0] != '\0' )
                        str += ", version: " + string(i.version);
                }

            auto vis_payloads = s.num_visible_payloads;
            if ( vis_payloads > 0 )
            {
                str += ", payload";
                str += (vis_payloads > 1) ? "s: " : ": ";
                for ( const auto& pld : s.payloads )
                {
                    if ( pld.second )
                        str += to_string(pld.first) + (--vis_payloads ? ", " : "");
                }
            }
            if ( *s.user )
                str += ", user: " + string(s.user);
        }
    }

    if ( num_visible_clients > 0 )
    {
        str += "\nclients size: " + to_string(num_visible_clients);
        for ( const auto& c : clients )
        {
            if ( c.visibility == false )
                continue;

            str += "\n    id: " + to_string(c.id)
                + ", service: " + to_string(c.service);
            if ( c.version[0] != '\0' )
                str += ", version: " + string(c.version);

            auto vis_payloads = c.num_visible_payloads;
            if ( vis_payloads )
            {
                str += ", payload";
                str += (vis_payloads > 1) ? "s: " : ": ";
                for ( const auto& pld : c.payloads )
                {
                    if ( pld.second )
                        str += to_string(pld.first) + (--vis_payloads ? ", " : "");
                }
            }
        }
    }

    if ( any_of(network_protos.begin(), network_protos.end(),
        [] (const NetProto_t& proto) { return proto.second; }) )
    {
        str += "\nnetwork proto: ";
        auto total = network_protos.size();
        while ( total-- )
        {
            const auto& proto = network_protos[total];
            if ( proto.second == true )
                str += to_string(proto.first) + (total? ", " : "");
        }
    }

    if ( any_of(xport_protos.begin(), xport_protos.end(),
        [] (const XProto_t& proto) { return proto.second; }) )
    {
        str += "\ntransport proto: ";
        auto total = xport_protos.size();
        while ( total-- )
        {
            const auto& proto = xport_protos[total];
            if ( proto.second == true )
                str += to_string(proto.first) + (total? ", " : "");
        }
    }

    auto total = tcp_fpids.size();
    if ( total )
    {
        str += "\ntcp fingerprint: ";
        for ( const auto& fpid : tcp_fpids )
            str += to_string(fpid) + (--total ? ", " : "");
    }

    total = ua_fps.size();
    if ( total )
    {
        str += "\nua fingerprint: ";
        for ( const auto& fp : ua_fps )
        {
            str += to_string(fp.fpid) + " (type: " + to_string(fp.fp_type);
            if ( fp.jail_broken )
                str += ", jail-broken";
            if ( fp.device[0] != '\0' )
                str += ", device: " + string(fp.device);
            str += string(")") + (--total ? ", " : "");
        }
    }

    total = udp_fpids.size();
    if ( total )
    {
        str += "\nudp fingerprint: ";
        for ( const auto& fpid : udp_fpids )
            str += to_string(fpid) + (--total ? ", " : "");
    }
}
