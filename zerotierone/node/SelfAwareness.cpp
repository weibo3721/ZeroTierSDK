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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <set>
#include <vector>

#include "Constants.hpp"
#include "SelfAwareness.hpp"
#include "RuntimeEnvironment.hpp"
#include "Node.hpp"
#include "Topology.hpp"
#include "Packet.hpp"
#include "Peer.hpp"
#include "Switch.hpp"

// Entry timeout -- make it fairly long since this is just to prevent stale buildup
#define ZT_SELFAWARENESS_ENTRY_TIMEOUT 600000

namespace ZeroTier {

class _ResetWithinScope
{
public:
	_ResetWithinScope(uint64_t now,int inetAddressFamily,InetAddress::IpScope scope) :
		_now(now),
		_family(inetAddressFamily),
		_scope(scope) {}

	inline void operator()(Topology &t,const SharedPtr<Peer> &p) { p->resetWithinScope(_scope,_family,_now); }

private:
	uint64_t _now;
	int _family;
	InetAddress::IpScope _scope;
};

SelfAwareness::SelfAwareness(const RuntimeEnvironment *renv) :
	RR(renv),
	_phy(128)
{
}

void SelfAwareness::iam(const Address &reporter,const InetAddress &receivedOnLocalAddress,const InetAddress &reporterPhysicalAddress,const InetAddress &myPhysicalAddress,bool trusted,uint64_t now)
{
	const InetAddress::IpScope scope = myPhysicalAddress.ipScope();

	if ((scope != reporterPhysicalAddress.ipScope())||(scope == InetAddress::IP_SCOPE_NONE)||(scope == InetAddress::IP_SCOPE_LOOPBACK)||(scope == InetAddress::IP_SCOPE_MULTICAST))
		return;

	Mutex::Lock _l(_phy_m);
	PhySurfaceEntry &entry = _phy[PhySurfaceKey(reporter,receivedOnLocalAddress,reporterPhysicalAddress,scope)];

	if ( (trusted) && ((now - entry.ts) < ZT_SELFAWARENESS_ENTRY_TIMEOUT) && (!entry.mySurface.ipsEqual(myPhysicalAddress)) ) {
		// Changes to external surface reported by trusted peers causes path reset in this scope
		TRACE("physical address %s for scope %u as seen from %s(%s) differs from %s, resetting paths in scope",myPhysicalAddress.toString().c_str(),(unsigned int)scope,reporter.toString().c_str(),reporterPhysicalAddress.toString().c_str(),entry.mySurface.toString().c_str());

		entry.mySurface = myPhysicalAddress;
		entry.ts = now;
		entry.trusted = trusted;

		// Erase all entries in this scope that were not reported from this remote address to prevent 'thrashing'
		// due to multiple reports of endpoint change.
		// Don't use 'entry' after this since hash table gets modified.
		{
			Hashtable< PhySurfaceKey,PhySurfaceEntry >::Iterator i(_phy);
			PhySurfaceKey *k = (PhySurfaceKey *)0;
			PhySurfaceEntry *e = (PhySurfaceEntry *)0;
			while (i.next(k,e)) {
				if ((k->reporterPhysicalAddress != reporterPhysicalAddress)&&(k->scope == scope))
					_phy.erase(*k);
			}
		}

		// Reset all paths within this scope and address family
		_ResetWithinScope rset(now,myPhysicalAddress.ss_family,(InetAddress::IpScope)scope);
		RR->topology->eachPeer<_ResetWithinScope &>(rset);
	} else {
		// Otherwise just update DB to use to determine external surface info
		entry.mySurface = myPhysicalAddress;
		entry.ts = now;
		entry.trusted = trusted;
	}
}

void SelfAwareness::clean(uint64_t now)
{
	Mutex::Lock _l(_phy_m);
	Hashtable< PhySurfaceKey,PhySurfaceEntry >::Iterator i(_phy);
	PhySurfaceKey *k = (PhySurfaceKey *)0;
	PhySurfaceEntry *e = (PhySurfaceEntry *)0;
	while (i.next(k,e)) {
		if ((now - e->ts) >= ZT_SELFAWARENESS_ENTRY_TIMEOUT)
			_phy.erase(*k);
	}
}

std::vector<InetAddress> SelfAwareness::getSymmetricNatPredictions()
{
	/* This is based on ideas and strategies found here:
	 * https://tools.ietf.org/html/draft-takeda-symmetric-nat-traversal-00
	 *
	 * For each IP address reported by a trusted (upstream) peer, we find
	 * the external port most recently reported by ANY peer for that IP.
	 *
	 * We only do any of this for global IPv4 addresses since private IPs
	 * and IPv6 are not going to have symmetric NAT.
	 *
	 * SECURITY NOTE:
	 *
	 * We never use IPs reported by non-trusted peers, since this could lead
	 * to a minor vulnerability whereby a peer could poison our cache with
	 * bad external surface reports via OK(HELLO) and then possibly coax us
	 * into suggesting their IP to other peers via PUSH_DIRECT_PATHS. This
	 * in turn could allow them to MITM flows.
	 *
	 * Since flows are encrypted and authenticated they could not actually
	 * read or modify traffic, but they could gather meta-data for forensics
	 * purpsoes or use this as a DOS attack vector. */

	std::map< uint32_t,std::pair<uint64_t,unsigned int> > maxPortByIp;
	InetAddress theOneTrueSurface;
	bool symmetric = false;
	{
		Mutex::Lock _l(_phy_m);

		{	// First get IPs from only trusted peers, and perform basic NAT type characterization
			Hashtable< PhySurfaceKey,PhySurfaceEntry >::Iterator i(_phy);
			PhySurfaceKey *k = (PhySurfaceKey *)0;
			PhySurfaceEntry *e = (PhySurfaceEntry *)0;
			while (i.next(k,e)) {
				if ((e->trusted)&&(e->mySurface.ss_family == AF_INET)&&(e->mySurface.ipScope() == InetAddress::IP_SCOPE_GLOBAL)) {
					if (!theOneTrueSurface)
						theOneTrueSurface = e->mySurface;
					else if (theOneTrueSurface != e->mySurface)
						symmetric = true;
					maxPortByIp[reinterpret_cast<const struct sockaddr_in *>(&(e->mySurface))->sin_addr.s_addr] = std::pair<uint64_t,unsigned int>(e->ts,e->mySurface.port());
				}
			}
		}

		{	// Then find max port per IP from a trusted peer
			Hashtable< PhySurfaceKey,PhySurfaceEntry >::Iterator i(_phy);
			PhySurfaceKey *k = (PhySurfaceKey *)0;
			PhySurfaceEntry *e = (PhySurfaceEntry *)0;
			while (i.next(k,e)) {
				if ((e->mySurface.ss_family == AF_INET)&&(e->mySurface.ipScope() == InetAddress::IP_SCOPE_GLOBAL)) {
					std::map< uint32_t,std::pair<uint64_t,unsigned int> >::iterator mp(maxPortByIp.find(reinterpret_cast<const struct sockaddr_in *>(&(e->mySurface))->sin_addr.s_addr));
					if ((mp != maxPortByIp.end())&&(mp->second.first < e->ts)) {
						mp->second.first = e->ts;
						mp->second.second = e->mySurface.port();
					}
				}
			}
		}
	}

	if (symmetric) {
		std::vector<InetAddress> r;
		for(unsigned int k=1;k<=3;++k) {
			for(std::map< uint32_t,std::pair<uint64_t,unsigned int> >::iterator i(maxPortByIp.begin());i!=maxPortByIp.end();++i) {
				unsigned int p = i->second.second + k;
				if (p > 65535) p -= 64511;
				InetAddress pred(&(i->first),4,p);
				if (std::find(r.begin(),r.end(),pred) == r.end())
					r.push_back(pred);
			}
		}
		return r;
	}

	return std::vector<InetAddress>();
}

} // namespace ZeroTier
