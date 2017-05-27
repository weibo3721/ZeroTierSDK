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

#include "Tag.hpp"
#include "RuntimeEnvironment.hpp"
#include "Identity.hpp"
#include "Topology.hpp"
#include "Switch.hpp"
#include "Network.hpp"

namespace ZeroTier {

int Tag::verify(const RuntimeEnvironment *RR) const
{
	if ((!_signedBy)||(_signedBy != Network::controllerFor(_networkId)))
		return -1;
	const Identity id(RR->topology->getIdentity(_signedBy));
	if (!id) {
		RR->sw->requestWhois(_signedBy);
		return 1;
	}
	try {
		Buffer<(sizeof(Tag) * 2)> tmp;
		this->serialize(tmp,true);
		return (id.verify(tmp.data(),tmp.size(),_signature) ? 0 : -1);
	} catch ( ... ) {
		return -1;
	}
}

} // namespace ZeroTier
