/*
 * Copyright (C) 2012-2024 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DCPLUSPLUS_DCPP_MAGNET_H
#define DCPLUSPLUS_DCPP_MAGNET_H

#include <airdcpp/forward.h>

#include <airdcpp/core/types/DupeType.h>
#include <airdcpp/user/User.h>

#include <string>

namespace dcpp {

using std::string;

/** Struct for a magnet uri */
struct Magnet {
	static string makeMagnet(const TTHValue& aHash, const string& aFile, int64_t aSize) noexcept;
	static optional<Magnet> parseMagnet(const string& aLink, const UserPtr& aTo) noexcept;

	string fname, type, param, hash;
	int64_t fsize = -1;

	explicit Magnet(const string& aLink, const UserPtr& aTo = nullptr);

	DupeType getDupeType() const;
	TTHValue getTTH() const;

	UserPtr to;
};

}

#endif