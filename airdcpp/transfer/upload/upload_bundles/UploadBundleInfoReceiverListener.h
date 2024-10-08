/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_BUNDLE_MANAGER_LISTENER_H_
#define DCPLUSPLUS_DCPP_UPLOAD_BUNDLE_MANAGER_LISTENER_H_

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/transfer/upload/upload_bundles/UploadBundle.h>

namespace dcpp {

class UploadBundleInfoReceiverListener {
public:
	virtual ~UploadBundleInfoReceiverListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<1> BundleComplete;
	typedef X<2> BundleSizeName;
	typedef X<3> BundleTick;

	virtual void on(BundleComplete, const string&, const string&) noexcept { }
	virtual void on(BundleSizeName, const string&, const string&, int64_t) noexcept { }
	virtual void on(BundleTick, const TickUploadBundleList&) noexcept { }
};

} // namespace dcpp

#endif /*DCPLUSPLUS_DCPP_UPLOAD_BUNDLE_MANAGER_LISTENER_H_*/
