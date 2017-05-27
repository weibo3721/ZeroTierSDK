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

#include <stdint.h>

#include <algorithm>

#include "NetworkConfig.hpp"

namespace ZeroTier {

bool NetworkConfig::toDictionary(Dictionary<ZT_NETWORKCONFIG_DICT_CAPACITY> &d,bool includeLegacy) const
{
	Buffer<ZT_NETWORKCONFIG_DICT_CAPACITY> *tmp = new Buffer<ZT_NETWORKCONFIG_DICT_CAPACITY>();

	try {
		d.clear();

		// Try to put the more human-readable fields first

		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_VERSION,(uint64_t)ZT_NETWORKCONFIG_VERSION)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_NETWORK_ID,this->networkId)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_TIMESTAMP,this->timestamp)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_CREDENTIAL_TIME_MAX_DELTA,this->credentialTimeMaxDelta)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_REVISION,this->revision)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ISSUED_TO,this->issuedTo)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_FLAGS,this->flags)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_MULTICAST_LIMIT,(uint64_t)this->multicastLimit)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_TYPE,(uint64_t)this->type)) return false;
		if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_NAME,this->name)) return false;

#ifdef ZT_SUPPORT_OLD_STYLE_NETCONF
		if (includeLegacy) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ALLOW_PASSIVE_BRIDGING_OLD,this->allowPassiveBridging())) return false;
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ENABLE_BROADCAST_OLD,this->enableBroadcast())) return false;
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_PRIVATE_OLD,this->isPrivate())) return false;

			std::string v4s;
			for(unsigned int i=0;i<staticIpCount;++i) {
				if (this->staticIps[i].ss_family == AF_INET) {
					if (v4s.length() > 0)
						v4s.push_back(',');
					v4s.append(this->staticIps[i].toString());
				}
			}
			if (v4s.length() > 0) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_IPV4_STATIC_OLD,v4s.c_str())) return false;
			}
			std::string v6s;
			for(unsigned int i=0;i<staticIpCount;++i) {
				if (this->staticIps[i].ss_family == AF_INET6) {
					if (v6s.length() > 0)
						v6s.push_back(',');
					v6s.append(this->staticIps[i].toString());
				}
			}
			if (v6s.length() > 0) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_IPV6_STATIC_OLD,v6s.c_str())) return false;
			}

			std::string ets;
			unsigned int et = 0;
			ZT_VirtualNetworkRuleType lastrt = ZT_NETWORK_RULE_ACTION_ACCEPT;
			for(unsigned int i=0;i<ruleCount;++i) {
				ZT_VirtualNetworkRuleType rt = (ZT_VirtualNetworkRuleType)(rules[i].t & 0x7f);
				if (rt == ZT_NETWORK_RULE_MATCH_ETHERTYPE) {
					et = rules[i].v.etherType;
				} else if (rt == ZT_NETWORK_RULE_ACTION_ACCEPT) {
					if (((int)lastrt < 32)||(lastrt == ZT_NETWORK_RULE_MATCH_ETHERTYPE)) {
						if (ets.length() > 0)
							ets.push_back(',');
						char tmp2[16];
						Utils::snprintf(tmp2,sizeof(tmp2),"%x",et);
						ets.append(tmp2);
					}
					et = 0;
				}
				lastrt = rt;
			}
			if (ets.length() > 0) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ALLOWED_ETHERNET_TYPES_OLD,ets.c_str())) return false;
			}

			if (this->com) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATE_OF_MEMBERSHIP_OLD,this->com.toString().c_str())) return false;
			}

			std::string ab;
			for(unsigned int i=0;i<this->specialistCount;++i) {
				if ((this->specialists[i] & ZT_NETWORKCONFIG_SPECIALIST_TYPE_ACTIVE_BRIDGE) != 0) {
					if (ab.length() > 0)
						ab.push_back(',');
					ab.append(Address(this->specialists[i]).toString().c_str());
				}
			}
			if (ab.length() > 0) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ACTIVE_BRIDGES_OLD,ab.c_str())) return false;
			}
		}
#endif // ZT_SUPPORT_OLD_STYLE_NETCONF

		// Then add binary blobs

		if (this->com) {
			tmp->clear();
			this->com.serialize(*tmp);
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_COM,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->capabilityCount;++i)
			this->capabilities[i].serialize(*tmp);
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_CAPABILITIES,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->tagCount;++i)
			this->tags[i].serialize(*tmp);
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_TAGS,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->certificateOfOwnershipCount;++i)
			this->certificatesOfOwnership[i].serialize(*tmp);
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATES_OF_OWNERSHIP,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->specialistCount;++i)
			tmp->append((uint64_t)this->specialists[i]);
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_SPECIALISTS,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->routeCount;++i) {
			reinterpret_cast<const InetAddress *>(&(this->routes[i].target))->serialize(*tmp);
			reinterpret_cast<const InetAddress *>(&(this->routes[i].via))->serialize(*tmp);
			tmp->append((uint16_t)this->routes[i].flags);
			tmp->append((uint16_t)this->routes[i].metric);
		}
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_ROUTES,*tmp)) return false;
		}

		tmp->clear();
		for(unsigned int i=0;i<this->staticIpCount;++i)
			this->staticIps[i].serialize(*tmp);
		if (tmp->size()) {
			if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_STATIC_IPS,*tmp)) return false;
		}

		if (this->ruleCount) {
			tmp->clear();
			Capability::serializeRules(*tmp,rules,ruleCount);
			if (tmp->size()) {
				if (!d.add(ZT_NETWORKCONFIG_DICT_KEY_RULES,*tmp)) return false;
			}
		}

		delete tmp;
	} catch ( ... ) {
		delete tmp;
		throw;
	}

	return true;
}

bool NetworkConfig::fromDictionary(const Dictionary<ZT_NETWORKCONFIG_DICT_CAPACITY> &d)
{
	Buffer<ZT_NETWORKCONFIG_DICT_CAPACITY> *tmp = new Buffer<ZT_NETWORKCONFIG_DICT_CAPACITY>();

	try {
		memset(this,0,sizeof(NetworkConfig));

		// Fields that are always present, new or old
		this->networkId = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_NETWORK_ID,0);
		if (!this->networkId) {
			delete tmp;
			return false;
		}
		this->timestamp = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_TIMESTAMP,0);
		this->credentialTimeMaxDelta = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_CREDENTIAL_TIME_MAX_DELTA,0);
		this->revision = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_REVISION,0);
		this->issuedTo = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_ISSUED_TO,0);
		if (!this->issuedTo) {
			delete tmp;
			return false;
		}
		this->multicastLimit = (unsigned int)d.getUI(ZT_NETWORKCONFIG_DICT_KEY_MULTICAST_LIMIT,0);
		d.get(ZT_NETWORKCONFIG_DICT_KEY_NAME,this->name,sizeof(this->name));

		if (d.getUI(ZT_NETWORKCONFIG_DICT_KEY_VERSION,0) < 6) {
	#ifdef ZT_SUPPORT_OLD_STYLE_NETCONF
			char tmp2[1024];

			// Decode legacy fields if version is old
			if (d.getB(ZT_NETWORKCONFIG_DICT_KEY_ALLOW_PASSIVE_BRIDGING_OLD))
				this->flags |= ZT_NETWORKCONFIG_FLAG_ALLOW_PASSIVE_BRIDGING;
			if (d.getB(ZT_NETWORKCONFIG_DICT_KEY_ENABLE_BROADCAST_OLD))
				this->flags |= ZT_NETWORKCONFIG_FLAG_ENABLE_BROADCAST;
			this->flags |= ZT_NETWORKCONFIG_FLAG_ENABLE_IPV6_NDP_EMULATION; // always enable for old-style netconf
			this->type = (d.getB(ZT_NETWORKCONFIG_DICT_KEY_PRIVATE_OLD,true)) ? ZT_NETWORK_TYPE_PRIVATE : ZT_NETWORK_TYPE_PUBLIC;

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_IPV4_STATIC_OLD,tmp2,sizeof(tmp2)) > 0) {
				char *saveptr = (char *)0;
				for(char *f=Utils::stok(tmp2,",",&saveptr);(f);f=Utils::stok((char *)0,",",&saveptr)) {
					if (this->staticIpCount >= ZT_MAX_ZT_ASSIGNED_ADDRESSES) break;
					InetAddress ip(f);
					if (!ip.isNetwork())
						this->staticIps[this->staticIpCount++] = ip;
				}
			}
			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_IPV6_STATIC_OLD,tmp2,sizeof(tmp2)) > 0) {
				char *saveptr = (char *)0;
				for(char *f=Utils::stok(tmp2,",",&saveptr);(f);f=Utils::stok((char *)0,",",&saveptr)) {
					if (this->staticIpCount >= ZT_MAX_ZT_ASSIGNED_ADDRESSES) break;
					InetAddress ip(f);
					if (!ip.isNetwork())
						this->staticIps[this->staticIpCount++] = ip;
				}
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATE_OF_MEMBERSHIP_OLD,tmp2,sizeof(tmp2)) > 0) {
				this->com.fromString(tmp2);
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_ALLOWED_ETHERNET_TYPES_OLD,tmp2,sizeof(tmp2)) > 0) {
				char *saveptr = (char *)0;
				for(char *f=Utils::stok(tmp2,",",&saveptr);(f);f=Utils::stok((char *)0,",",&saveptr)) {
					unsigned int et = Utils::hexStrToUInt(f) & 0xffff;
					if ((this->ruleCount + 2) > ZT_MAX_NETWORK_RULES) break;
					if (et > 0) {
						this->rules[this->ruleCount].t = (uint8_t)ZT_NETWORK_RULE_MATCH_ETHERTYPE;
						this->rules[this->ruleCount].v.etherType = (uint16_t)et;
						++this->ruleCount;
					}
					this->rules[this->ruleCount++].t = (uint8_t)ZT_NETWORK_RULE_ACTION_ACCEPT;
				}
			} else {
				this->rules[0].t = ZT_NETWORK_RULE_ACTION_ACCEPT;
				this->ruleCount = 1;
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_ACTIVE_BRIDGES_OLD,tmp2,sizeof(tmp2)) > 0) {
				char *saveptr = (char *)0;
				for(char *f=Utils::stok(tmp2,",",&saveptr);(f);f=Utils::stok((char *)0,",",&saveptr)) {
					this->addSpecialist(Address(Utils::hexStrToU64(f)),ZT_NETWORKCONFIG_SPECIALIST_TYPE_ACTIVE_BRIDGE);
				}
			}
	#else
			delete tmp;
			return false;
	#endif // ZT_SUPPORT_OLD_STYLE_NETCONF
		} else {
			// Otherwise we can use the new fields
			this->flags = d.getUI(ZT_NETWORKCONFIG_DICT_KEY_FLAGS,0);
			this->type = (ZT_VirtualNetworkType)d.getUI(ZT_NETWORKCONFIG_DICT_KEY_TYPE,(uint64_t)ZT_NETWORK_TYPE_PRIVATE);

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_COM,*tmp))
				this->com.deserialize(*tmp,0);

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_CAPABILITIES,*tmp)) {
				try {
					unsigned int p = 0;
					while (p < tmp->size()) {
						Capability cap;
						p += cap.deserialize(*tmp,p);
						this->capabilities[this->capabilityCount++] = cap;
					}
				} catch ( ... ) {}
				std::sort(&(this->capabilities[0]),&(this->capabilities[this->capabilityCount]));
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_TAGS,*tmp)) {
				try {
					unsigned int p = 0;
					while (p < tmp->size()) {
						Tag tag;
						p += tag.deserialize(*tmp,p);
						this->tags[this->tagCount++] = tag;
					}
				} catch ( ... ) {}
				std::sort(&(this->tags[0]),&(this->tags[this->tagCount]));
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_CERTIFICATES_OF_OWNERSHIP,*tmp)) {
				unsigned int p = 0;
				while (p < tmp->size()) {
					if (certificateOfOwnershipCount < ZT_MAX_CERTIFICATES_OF_OWNERSHIP)
						p += certificatesOfOwnership[certificateOfOwnershipCount++].deserialize(*tmp,p);
					else {
						CertificateOfOwnership foo;
						p += foo.deserialize(*tmp,p);
					}
				}
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_SPECIALISTS,*tmp)) {
				unsigned int p = 0;
				while ((p + 8) <= tmp->size()) {
					if (specialistCount < ZT_MAX_NETWORK_SPECIALISTS)
						this->specialists[this->specialistCount++] = tmp->at<uint64_t>(p);
					p += 8;
				}
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_ROUTES,*tmp)) {
				unsigned int p = 0;
				while ((p < tmp->size())&&(routeCount < ZT_MAX_NETWORK_ROUTES)) {
					p += reinterpret_cast<InetAddress *>(&(this->routes[this->routeCount].target))->deserialize(*tmp,p);
					p += reinterpret_cast<InetAddress *>(&(this->routes[this->routeCount].via))->deserialize(*tmp,p);
					this->routes[this->routeCount].flags = tmp->at<uint16_t>(p); p += 2;
					this->routes[this->routeCount].metric = tmp->at<uint16_t>(p); p += 2;
					++this->routeCount;
				}
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_STATIC_IPS,*tmp)) {
				unsigned int p = 0;
				while ((p < tmp->size())&&(staticIpCount < ZT_MAX_ZT_ASSIGNED_ADDRESSES)) {
					p += this->staticIps[this->staticIpCount++].deserialize(*tmp,p);
				}
			}

			if (d.get(ZT_NETWORKCONFIG_DICT_KEY_RULES,*tmp)) {
				this->ruleCount = 0;
				unsigned int p = 0;
				Capability::deserializeRules(*tmp,p,this->rules,this->ruleCount,ZT_MAX_NETWORK_RULES);
			}
		}

		//printf("~~~\n%s\n~~~\n",d.data());
		//dump();
		//printf("~~~\n");

		delete tmp;
		return true;
	} catch ( ... ) {
		delete tmp;
		return false;
	}
}

} // namespace ZeroTier
