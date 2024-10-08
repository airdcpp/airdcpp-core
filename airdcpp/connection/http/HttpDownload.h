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

#ifndef DCPLUSPLUS_DCPP_HTTP_DOWNLOAD_H
#define DCPLUSPLUS_DCPP_HTTP_DOWNLOAD_H

#include <airdcpp/connection/http/HttpConnection.h>

namespace dcpp {

using std::string;

/** Helper struct to manage a single HTTP download. Calls a completion function when finished. */
struct HttpDownload : private HttpConnectionListener, private boost::noncopyable {
	HttpConnection* c;
	string buf;
	string status;
	StringMap headers;
	using CompletionF = std::function<void ()>;
	CompletionF f;

	explicit HttpDownload(const string& address, CompletionF&& f, const HttpOptions& aOptions = HttpOptions());
	~HttpDownload() override;

	// HttpConnectionListener
	void on(HttpConnectionListener::Data, HttpConnection*, const uint8_t* buf_, size_t len) noexcept override;
	void on(HttpConnectionListener::Failed, HttpConnection*, const string& status_) noexcept override;
	void on(HttpConnectionListener::Complete, HttpConnection*, const string& status_) noexcept override;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_HTTP_DOWNLOAD_H)
