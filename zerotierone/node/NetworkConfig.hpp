/*
 * ZeroTier One - Network Virtualization Everywhere
 * Copyright (C) 2011-2016  ZeroTier, Inc.  https://www.zerotier.com/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZT_NETWORKCONFIG_HPP
#define ZT_NETWORKCONFIG_HPP

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <vector>
#include <stdexcept>
#include <algorithm>

#include "../include/ZeroTierOne.h"

#include "Constants.hpp"
#include "Buffer.hpp"
#include "InetAddress.hpp"
#include "MulticastGroup.hpp"
#include "Address.hpp"
#include "CertificateOfMembership.hpp"
#include "CertificateOfOwnership.hpp"
#include "Capability.hpp"
#include "Tag.hpp"
#include "Dictionary.hpp"
#include "Identity.hpp"
#include "Utils.hpp"

/**
 * Default maximum time delta for COMs, tags, and capabilities
 *
 * The current value is two hours, providing ample time for a controller to
 * experience fail-over, etc.
 */
#define ZT_NETWORKCONFIG_DEFAULT_CREDENTIAL_TIME_MAX_MAX_DELTA 7200000ULL

/**
 * Default minimum credential TTL and maxDelta for COM timestamps
 *
 * This is just slightly over three minutes and provides three retries for
 * all currently online members to refresh.
 */
#define ZT_NETWORKCONFIG_DEFAULT_CREDENTIAL_TIME_MIN_MAX_DELTA 185000ULL

/**
 * Flag: allow passive bridging (experimental)
 */
#define ZT_NETWORKCONFIG_FLAG_ALLOW_PASSIVE_BRIDGING 0x0000000000000001ULL

/**
 * Flag: enable broadcast
 */
#define ZT_NETWORKCONFIG_FLAG_ENABLE_BROADCAST 0x0000000000000002ULL

/**
 * Flag: enable IPv6 NDP emulation for certain V6 address patterns
 */
#define ZT_NETWORKCONFIG_FLAG_ENABLE_IPV6_NDP_EMULATION 0x0000000000000004ULL

/**
 * Flag: result of unrecognized MATCH entries in a rules table: match if set, no-match if clear
 */
#define ZT_NETWORKCONFIG_FLAG_RULES_RESULT_OF_UNSUPPORTED_MATCH 0x0000000000000008ULL

/**
 * Flag: disable frame compression
 */
#define ZT_NETWORKCONFIG_FLAG_DISABLE_COMPRESSION 0x0000000000000010ULL

/**
 * Device is an active bridge
 */
#define ZT_NETWORKCONFIG_SPECIALIST_TYPE_ACTIVE_BRIDGE 0x0000020000000000ULL

/**
 * Anchors are stable devices on this network that can cache multicast info, etc.
 */
#define ZT_NETWORKCONFIG_SPECIALIST_TYPE_ANCHOR 0x0000040000000000ULL

/**
 * Device can send CIRCUIT_TESTs for this network
 */
#define ZT_NETWORKCONFIG_SPECIALIST_TYPE_CIRCUIT_TESTER 0x0000080000000000ULL

namespace ZeroTier {

// Dictionary capacity needed for max size network config
#define ZT_NETWORKCONFIG_DICT_CAPACITY (1024 + (sizeof(ZT_VirtualNetworkRule) * ZT_MAX_NETWORK_RULES) + (sizeof(Capability) * ZT_MAX_NETWORK_CAPABILITIES) + (sizeof(Tag) * ZT_MAX_NETWORK_TAGS) + (sizeof(CertificateOfOwnership) * ZT_MAX_CERTIFICATES_OF_OWNERSHIP))

// Dictionary capacity needed for max size network meta-data
#define ZT_NETWORKCONFIG_METADATA_DICT_CAPACITY 1024

// Network config version
#define ZT_NETWORKCONFIG_VERSION 7

// Fields for meta-data sent with network config requests

// Network config version
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_VERSION "v"
// Protocol version (see Packet.hpp)
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_PROTOCOL_VERSION "pv"
// Software vendor
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_NODE_VENDOR "vend"
// Software major version
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_NODE_MAJOR_VERSION "majv"
// Software minor version
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_NODE_MINOR_VERSION "minv"
// Software revision
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_NODE_REVISION "revv"
// Rules engine revision
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_RULES_ENGINE_REV "revr"
// Maximum number of rules per network this node can accept
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_MAX_NETWORK_RULES "mr"
// Maximum number of capabilities this node can accept
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_MAX_NETWORK_CAPABILITIES "mc"
// Maximum number of rules per capability this node can accept
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_MAX_CAPABILITY_RULES "mcr"
// Maximum number of tags this node can accept
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_MAX_NETWORK_TAGS "mt"
// Network join authorization token (if any)
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_AUTH "a"
// Network configuration meta-data flags
#define ZT_NETWORKCONFIG_REQUEST_METADATA_KEY_FLAGS "f"

// These dictionary keys are short so they don't take up much room.
// By convention we use upper case for binary blobs, but it doesn't really matter.

// network config version
#define ZT_NETWORKCONFIG_DICT_KEY_VERSION "v"
// network ID
#define ZT_NETWORKCONFIG_DICT_KEY_NETWORK_ID "nwid"
// integer(hex)
#define ZT_NETWORKCONFIG_DICT_KEY_TIMESTAMP "ts"
// integer(hex)
#define ZT_NETWORKCONFIG_DICT_KEY_REVISION "r"
// address of member
#define ZT_NETWORKCONFIG_DICT_KEY_ISSUED_TO "id"
// flags(hex)
#define ZT_NETWORKCONFIG_DICT_KEY_FLAGS "f"
// integer(hex)
#define ZT_NETWORKCONFIG_DICT_KEY_MULTICAST_LIMIT "ml"
// network type (hex)
#define ZT_NETWORKCONFIG_DICT_KEY_TYPE "t"
// text
#define ZT_NETWORKCONFIG_DICT_KEY_NAME "n"
// credential time max delta in ms
#define ZT_NETWORKCONFIG_DICT_KEY_CREDENTIAL_TIME_MAX_DELTA "ctmd"
// binary serialized certificate of membership
#define ZT_NETWORKCONFIG_DICT_KEY_COM "C"
// specialists (binary array of uint64_t)
#define ZT_NETWORKCONFIG_DICT_KEY_SPECIALISTS "S"
// routes (binary blob)
#define ZT_NETWORKCONFIG_DICT_KEY_ROUTES "RT"
// static IPs (binary blob)
#define ZT_NETWORKCONFIG_DICT_KEY_STATIC_IPS "I"
// rules (binary blob)
#define ZT_NETWORKCONFIG_DICT_KEY_RULES "R"
// capabilities (binary blobs)
#define ZT_NETWORKCONFIG_DICT_KEY_CAPABILITIES "CAP"
// tags (binary blobs)
#define ZT_NETWORKCONFIG_DICT_KEY_TAGS "TAG"
// tags (binary blobs)
#define ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATES_OF_OWNERSHIP "COO"
// curve25519 signature
#define ZT_NETWORKCONFIG_DICT_KEY_SIGNATURE "C25519"

// Legacy fields -- these are obsoleted but are included when older clients query

// boolean (now a flag)
#define ZT_NETWORKCONFIG_DICT_KEY_ALLOW_PASSIVE_BRIDGING_OLD "pb"
// boolean (now a flag)
#define ZT_NETWORKCONFIG_DICT_KEY_ENABLE_BROADCAST_OLD "eb"
// IP/bits[,IP/bits,...]
// Note that IPs that end in all zeroes are routes with no assignment in them.
#define ZT_NETWORKCONFIG_DICT_KEY_IPV4_STATIC_OLD "v4s"
// IP/bits[,IP/bits,...]
// Note that IPs that end in all zeroes are routes with no assignment in them.
#define ZT_NETWORKCONFIG_DICT_KEY_IPV6_STATIC_OLD "v6s"
// 0/1
#define ZT_NETWORKCONFIG_DICT_KEY_PRIVATE_OLD "p"
// integer(hex)[,integer(hex),...]
#define ZT_NETWORKCONFIG_DICT_KEY_ALLOWED_ETHERNET_TYPES_OLD "et"
// string-serialized CertificateOfMembership
#define ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATE_OF_MEMBERSHIP_OLD "com"
// node[,node,...]
#define ZT_NETWORKCONFIG_DICT_KEY_ACTIVE_BRIDGES_OLD "ab"
// node;IP/port[,node;IP/port]
#define ZT_NETWORKCONFIG_DICT_KEY_RELAYS_OLD "rl"

// End legacy fields

/**
 * Network configuration received from network controller nodes
 *
 * This is a memcpy()'able structure and is safe (in a crash sense) to modify
 * without locks.
 */
class NetworkConfig
{
public:
	NetworkConfig()
	{
		memset(this,0,sizeof(NetworkConfig));
	}

	NetworkConfig(const NetworkConfig &nc)
	{
		memcpy(this,&nc,sizeof(NetworkConfig));
	}

	inline NetworkConfig &operator=(const NetworkConfig &nc)
	{
		memcpy(this,&nc,sizeof(NetworkConfig));
		return *this;
	}

	/**
	 * Write this network config to a dictionary for transport
	 *
	 * @param d Dictionary
	 * @param includeLegacy If true, include legacy fields for old node versions
	 * @return True if dictionary was successfully created, false if e.g. overflow
	 */
	bool toDictionary(Dictionary<ZT_NETWORKCONFIG_DICT_CAPACITY> &d,bool includeLegacy) const;

	/**
	 * Read this network config from a dictionary
	 *
	 * @param d Dictionary (non-const since it might be modified during parse, should not be used after call)
	 * @return True if dictionary was valid and network config successfully initialized
	 */
	bool fromDictionary(const Dictionary<ZT_NETWORKCONFIG_DICT_CAPACITY> &d);

	/**
	 * @return True if passive bridging is allowed (experimental)
	 */
	inline bool allowPassiveBridging() const throw() { return ((this->flags & ZT_NETWORKCONFIG_FLAG_ALLOW_PASSIVE_BRIDGING) != 0); }

	/**
	 * @return True if broadcast (ff:ff:ff:ff:ff:ff) address should work on this network
	 */
	inline bool enableBroadcast() const throw() { return ((this->flags & ZT_NETWORKCONFIG_FLAG_ENABLE_BROADCAST) != 0); }

	/**
	 * @return True if IPv6 NDP emulation should be allowed for certain "magic" IPv6 address patterns
	 */
	inline bool ndpEmulation() const throw() { return ((this->flags & ZT_NETWORKCONFIG_FLAG_ENABLE_IPV6_NDP_EMULATION) != 0); }

	/**
	 * @return True if frames should not be compressed
	 */
	inline bool disableCompression() const throw() { return ((this->flags & ZT_NETWORKCONFIG_FLAG_DISABLE_COMPRESSION) != 0); }

	/**
	 * @return Network type is public (no access control)
	 */
	inline bool isPublic() const throw() { return (this->type == ZT_NETWORK_TYPE_PUBLIC); }

	/**
	 * @return Network type is private (certificate access control)
	 */
	inline bool isPrivate() const throw() { return (this->type == ZT_NETWORK_TYPE_PRIVATE); }

	/**
	 * @return ZeroTier addresses of devices on this network designated as active bridges
	 */
	inline std::vector<Address> activeBridges() const
	{
		std::vector<Address> r;
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_ACTIVE_BRIDGE) != 0)
				r.push_back(Address(specialists[i]));
		}
		return r;
	}

	/**
	 * @return ZeroTier addresses of "anchor" devices on this network
	 */
	inline std::vector<Address> anchors() const
	{
		std::vector<Address> r;
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_ANCHOR) != 0)
				r.push_back(Address(specialists[i]));
		}
		return r;
	}

	/**
	 * @param a Address to check
	 * @return True if address is an anchor
	 */
	inline bool isAnchor(const Address &a) const
	{
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((a == specialists[i])&&((specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_ANCHOR) != 0))
				return true;
		}
		return false;
	}

	/**
	 * @param fromPeer Peer attempting to bridge other Ethernet peers onto network
	 * @return True if this network allows bridging
	 */
	inline bool permitsBridging(const Address &fromPeer) const
	{
		if ((flags & ZT_NETWORKCONFIG_FLAG_ALLOW_PASSIVE_BRIDGING) != 0)
			return true;
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((fromPeer == specialists[i])&&((specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_ACTIVE_BRIDGE) != 0))
				return true;
		}
		return false;
	}

	/**
	 * @param byPeer Address to check
	 * @return True if this peer is allowed to do circuit tests on this network (controller is always true)
	 */
	inline bool circuitTestingAllowed(const Address &byPeer) const
	{
		if (byPeer.toInt() == ((networkId >> 24) & 0xffffffffffULL))
			return true;
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((byPeer == specialists[i])&&((specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_CIRCUIT_TESTER) != 0))
				return true;
		}
		return false;
	}

	/**
	 * @return True if this network config is non-NULL
	 */
	inline operator bool() const throw() { return (networkId != 0); }

	inline bool operator==(const NetworkConfig &nc) const { return (memcmp(this,&nc,sizeof(NetworkConfig)) == 0); }
	inline bool operator!=(const NetworkConfig &nc) const { return (!(*this == nc)); }

	/**
	 * Add a specialist or mask flags if already present
	 *
	 * This masks the existing flags if the specialist is already here or adds
	 * it otherwise.
	 *
	 * @param a Address of specialist
	 * @param f Flags (OR of specialist role/type flags)
	 * @return True if successfully masked or added
	 */
	inline bool addSpecialist(const Address &a,const uint64_t f)
	{
		const uint64_t aint = a.toInt();
		for(unsigned int i=0;i<specialistCount;++i) {
			if ((specialists[i] & 0xffffffffffULL) == aint) {
				specialists[i] |= f;
				return true;
			}
		}
		if (specialistCount < ZT_MAX_NETWORK_SPECIALISTS) {
			specialists[specialistCount++] = f | aint;
			return true;
		}
		return false;
	}

	const Capability *capability(const uint32_t id) const
	{
		for(unsigned int i=0;i<capabilityCount;++i) {
			if (capabilities[i].id() == id)
				return &(capabilities[i]);
		}
		return (Capability *)0;
	}

	const Tag *tag(const uint32_t id) const
	{
		for(unsigned int i=0;i<tagCount;++i) {
			if (tags[i].id() == id)
				return &(tags[i]);
		}
		return (Tag *)0;
	}

	/*
	inline void dump() const
	{
		printf("networkId==%.16llx\n",networkId);
		printf("timestamp==%llu\n",timestamp);
		printf("credentialTimeMaxDelta==%llu\n",credentialTimeMaxDelta);
		printf("revision==%llu\n",revision);
		printf("issuedTo==%.10llx\n",issuedTo.toInt());
		printf("multicastLimit==%u\n",multicastLimit);
		printf("flags=%.8lx\n",(unsigned long)flags);
		printf("specialistCount==%u\n",specialistCount);
		for(unsigned int i=0;i<specialistCount;++i)
			printf("  specialists[%u]==%.16llx\n",i,specialists[i]);
		printf("routeCount==%u\n",routeCount);
		for(unsigned int i=0;i<routeCount;++i) {
			printf("  routes[i].target==%s\n",reinterpret_cast<const InetAddress *>(&(routes[i].target))->toString().c_str());
			printf("  routes[i].via==%s\n",reinterpret_cast<const InetAddress *>(&(routes[i].via))->toIpString().c_str());
			printf("  routes[i].flags==%.4x\n",(unsigned int)routes[i].flags);
			printf("  routes[i].metric==%u\n",(unsigned int)routes[i].metric);
		}
		printf("staticIpCount==%u\n",staticIpCount);
		for(unsigned int i=0;i<staticIpCount;++i)
			printf("  staticIps[i]==%s\n",staticIps[i].toString().c_str());
		printf("ruleCount==%u\n",ruleCount);
		printf("name==%s\n",name);
		printf("com==%s\n",com.toString().c_str());
	}
	*/

	/**
	 * Network ID that this configuration applies to
	 */
	uint64_t networkId;

	/**
	 * Controller-side time of config generation/issue
	 */
	uint64_t timestamp;

	/**
	 * Max difference between timestamp and tag/capability timestamp
	 */
	uint64_t credentialTimeMaxDelta;

	/**
	 * Controller-side revision counter for this configuration
	 */
	uint64_t revision;

	/**
	 * Address of device to which this config is issued
	 */
	Address issuedTo;

	/**
	 * Flags (64-bit)
	 */
	uint64_t flags;

	/**
	 * Maximum number of recipients per multicast (not including active bridges)
	 */
	unsigned int multicastLimit;

	/**
	 * Number of specialists
	 */
	unsigned int specialistCount;

	/**
	 * Number of routes
	 */
	unsigned int routeCount;

	/**
	 * Number of ZT-managed static IP assignments
	 */
	unsigned int staticIpCount;

	/**
	 * Number of rule table entries
	 */
	unsigned int ruleCount;

	/**
	 * Number of capabilities
	 */
	unsigned int capabilityCount;

	/**
	 * Number of tags
	 */
	unsigned int tagCount;

	/**
	 * Number of certificates of ownership
	 */
	unsigned int certificateOfOwnershipCount;

	/**
	 * Specialist devices
	 *
	 * For each entry the least significant 40 bits are the device's ZeroTier
	 * address and the most significant 24 bits are flags indicating its role.
	 */
	uint64_t specialists[ZT_MAX_NETWORK_SPECIALISTS];

	/**
	 * Statically defined "pushed" routes (including default gateways)
	 */
	ZT_VirtualNetworkRoute routes[ZT_MAX_NETWORK_ROUTES];

	/**
	 * Static IP assignments
	 */
	InetAddress staticIps[ZT_MAX_ZT_ASSIGNED_ADDRESSES];

	/**
	 * Base network rules
	 */
	ZT_VirtualNetworkRule rules[ZT_MAX_NETWORK_RULES];

	/**
	 * Capabilities for this node on this network, in ascending order of capability ID
	 */
	Capability capabilities[ZT_MAX_NETWORK_CAPABILITIES];

	/**
	 * Tags for this node on this network, in ascending order of tag ID
	 */
	Tag tags[ZT_MAX_NETWORK_TAGS];

	/**
	 * Certificates of ownership for this network member
	 */
	CertificateOfOwnership certificatesOfOwnership[ZT_MAX_CERTIFICATES_OF_OWNERSHIP];

	/**
	 * Network type (currently just public or private)
	 */
	ZT_VirtualNetworkType type;

	/**
	 * Network short name or empty string if not defined
	 */
	char name[ZT_MAX_NETWORK_SHORT_NAME_LENGTH + 1];

	/**
	 * Certficiate of membership (for private networks)
	 */
	CertificateOfMembership com;
};

} // namespace ZeroTier

#endif
