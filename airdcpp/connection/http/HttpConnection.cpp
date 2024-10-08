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

#include "stdinc.h"
#include <airdcpp/connection/http/HttpConnection.h>

#include <airdcpp/connection/socket/BufferedSocket.h>
#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/core/header/format.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/core/version.h>

#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/header/format.h>

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

HttpConnection::HttpConnection(bool aIsUnique, const HttpOptions& aOptions) :
port("80"),
isUnique(aIsUnique),
options(aOptions)
{
}

HttpConnection::~HttpConnection() {
	if (socket) {
		abortRequest(true);
	}
}

/**
 * Downloads a file and returns it as a string
 * @todo Report exceptions
 * @todo Abort download
 * @param aUrl Full URL of file
 * @return A string with the content, or empty if download failed
 */
void HttpConnection::downloadFile(const string& aFile) {
	currentUrl = aFile;
	prepareRequest(TYPE_GET);
}

/**
 * Initiates a basic urlencoded form submission
 * @param aFile Fully qualified file URL
 * @param aData StringMap with the args and values
 */
void HttpConnection::postData(const string& aUrl, const StringMap& aData) {
	currentUrl = aUrl;
	requestBody.clear();

	for (const auto& [name, value] : aData)
		requestBody += "&" + LinkUtil::encodeURI(name) + "=" + LinkUtil::encodeURI(value);

	if (!requestBody.empty()) requestBody = requestBody.substr(1);
	prepareRequest(TYPE_POST);
}

void HttpConnection::prepareRequest(RequestType type) {
	dcassert(Util::findSubString(currentUrl, "http://") == 0 || Util::findSubString(currentUrl, "https://") == 0);
	LinkUtil::sanitizeUrl(currentUrl);

	size = -1;
	done = 0;
	connState = CONN_UNKNOWN;
	connType = type;

	// method selection
	method = (connType == TYPE_POST) ? "POST" : "GET";

	// set download type
	if (Util::stricmp(currentUrl.substr(currentUrl.size() - 4).c_str(), ".bz2") == 0) {
		mimeType = "application/x-bzip2";
	}
	else mimeType.clear();

	string proto, fragment;
	if (SETTING(HTTP_PROXY).empty()) {
		LinkUtil::decodeUrl(currentUrl, proto, server, port, file, query, fragment);
		if (file.empty())
			file = "/";
	}
	else {
		LinkUtil::decodeUrl(SETTING(HTTP_PROXY), proto, server, port, file, query, fragment);
		file = currentUrl;
	}

	if (!query.empty())
		file += '?' + query;

	if (port.empty())
		port = "80";

	if (!socket)
		socket = BufferedSocket::getSocket(0x0a, options.getV4Only());


	socket->addListener(this);
	try {
		SocketConnectOptions socketOptions(port, (proto == "https"));
		socket->connect(
			AddressInfo(server, AddressInfo::TYPE_URL), 
			socketOptions,
			true, 
			false
		);
	}
	catch (const Exception& e) {
		fire(HttpConnectionListener::Failed(), this, e.getError() + " (" + currentUrl + ")");
		connState = CONN_FAILED;
		if (isUnique) delete this;
	}
}

void HttpConnection::abortRequest(bool disconnect) {
	dcassert(socket);

	socket->removeListener(this);
	if (disconnect) socket->disconnect();

	BufferedSocket::putSocket(socket);
	socket = nullptr;
}

void HttpConnection::on(BufferedSocketListener::Connected) noexcept {
	dcassert(socket);
	socket->write("GET " + file + " HTTP/1.1\r\n");

	const auto addHeader = [&](const string& aKey, const string& aValue) {
		const auto value = aKey + ": " + aValue + "\r\n";
		socket->write(aKey + ": " + aValue + "\r\n");
	};

	addHeader("User-Agent", "Airdcpp/" + VERSIONSTRING + " " + SystemUtil::getOsVersion(true));

	string sRemoteServer = server;
	if(!SETTING(HTTP_PROXY).empty())
	{
		string tfile, tport, proto, queryTmp, fragment;
		LinkUtil::decodeUrl(file, proto, sRemoteServer, tport, tfile, queryTmp, fragment);
	}

	addHeader("Host", sRemoteServer);
	addHeader("Connection", "close"); // we'll only be doing one request

	for (const auto& [name, value] : options.getHeaders()) {
		addHeader(name, value);
	}

	addHeader("Cache-Control", "no-cache");
	socket->write("\r\n");
	if (connType == TYPE_POST) socket->write(requestBody);
}

void HttpConnection::on(BufferedSocketListener::Line, const string& aLine) noexcept {
	if(connState == CONN_CHUNKED && aLine.size() > 1) {
		string::size_type i;
		string chunkSizeStr;
		if((i = aLine.find(';')) == string::npos) {
			chunkSizeStr = aLine.substr(0, aLine.length() - 1);
		} else chunkSizeStr = aLine.substr(0, i);

		unsigned long chunkSize = strtoul(chunkSizeStr.c_str(), NULL, 16);
		if(chunkSize == 0 || chunkSize == ULONG_MAX) {
			abortRequest(true);

			if(chunkSize == 0) {
				fire(HttpConnectionListener::Complete(), this, currentUrl);
				connState = CONN_OK;
			} else {
				fire(HttpConnectionListener::Failed(), this, "Transfer-encoding error (" + currentUrl + ")");
				connState = CONN_FAILED;
			}

			if (isUnique) { delete this; return; }
		} else socket->setDataMode(chunkSize);
	} else if(connState == CONN_UNKNOWN) {
		if(aLine.find("200") != string::npos) {
			connState = CONN_OK;
		} else if(aLine.find("301") != string::npos || aLine.find("302") != string::npos) {
			connState = CONN_MOVED; 
		} else {
			abortRequest(true);
		
			auto error = aLine;
			if (error.length() > 1 && error.back() == '\r') {
				error.pop_back(); // These would cause issues in HTTP messages
			}

			fire(HttpConnectionListener::Failed(), this, str(boost::format("%1% (%2%)") % error % currentUrl));
			if (isUnique) { delete this; return; }
			connState = CONN_FAILED;
		}
	} else if(connState == CONN_MOVED && Util::findSubString(aLine, "Location") != string::npos) {
		abortRequest(true);

		string location = aLine.substr(10, aLine.length() - 10);
		LinkUtil::sanitizeUrl(location);

		// make sure we can also handle redirects with relative paths
		if(location.find("://") == string::npos) {
			if(location[0] == '/') {
				//302 doesn't contain the query, use temp one
				string proto, queryTmp, fragment;
				LinkUtil::decodeUrl(currentUrl, proto, server, port, file, queryTmp, fragment);
				string tmp = proto + "://" + server;
				if(port != "80" || port != "443")
					tmp += ':' + port;
				location = tmp + location;
			} else {
				string::size_type i = currentUrl.rfind('/');
				dcassert(i != string::npos);
				location = currentUrl.substr(0, i + 1) + location;
			}
		}

		if(location == currentUrl) {
			connState = CONN_FAILED;
			fire(HttpConnectionListener::Failed(), this, STRING_F(ENDLESS_REDIRECTION_LOOP, currentUrl));
			if (isUnique) delete this;
			return;
		}

		if (!query.empty())
			location += "?" + query;
		fire(HttpConnectionListener::Redirected(), this, location);

		downloadFile(location);
	} else if(aLine[0] == 0x0d) {
		if(size != -1) {
			socket->setDataMode(size);
		} else connState = CONN_CHUNKED;
	} else if (aLine.length() > 2) {
		// Header
		auto separator = aLine.find_first_of(':');
		if (separator <= 1) {
			return;
		}

		auto name = boost::algorithm::trim_copy(aLine.substr(0, separator));
		auto value = boost::algorithm::trim_copy(aLine.substr(separator + 1));

		if (name == "Content-Length") {
			size = Util::toInt(value);
		} else if (mimeType.empty()) {
			if (name == "Content-Encoding") {
				if (value == "x-bzip2")
					mimeType = "application/x-bzip2";
			} else if (name == "Content-Type") {
				mimeType = value;
			}
		}

		headers.try_emplace(name, value);
	}
}

void HttpConnection::on(BufferedSocketListener::Failed, const string& aLine) noexcept {
	abortRequest(false);

	connState = CONN_FAILED;
	fire(HttpConnectionListener::Failed(), this, str(boost::format("%1% (%2%)") % aLine % currentUrl));
	if (isUnique) delete this;
}

void HttpConnection::on(BufferedSocketListener::ModeChange) noexcept {
	if(connState != CONN_CHUNKED) {
		abortRequest(true);

		fire(HttpConnectionListener::Complete(), this, currentUrl);
		if (isUnique) { delete this; return; }
	}
}
void HttpConnection::on(BufferedSocketListener::Data, uint8_t* aBuf, size_t aLen) noexcept {
	if(size != -1 && static_cast<size_t>(size - done)  < aLen) {
		abortRequest(true);

		connState = CONN_FAILED;
		fire(HttpConnectionListener::Failed(), this, "Too much data in response body (" + currentUrl + ")");
		if (isUnique) delete this;
		return;
	}

	fire(HttpConnectionListener::Data(), this, aBuf, aLen);
	done += aLen;
}

} // namespace dcpp
