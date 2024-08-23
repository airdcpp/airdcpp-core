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
#include "UploadBundleInfoReceiver.h"

#include "ConnectionManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "Upload.h"
#include "UploadBundle.h"
#include "UploadBundleInfo.h"
#include "UploadManager.h"
#include "UserConnection.h"

#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>


namespace dcpp {

const auto ENABLE_DEBUG = false;

void UploadBundleInfoReceiver::dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	if (ENABLE_DEBUG) {
		LogManager::getInstance()->message(aMsg, aSeverity, "UBN (receiver)");
		dcdebug("UBN (receiver): %s\n", aMsg.c_str());
	} else if (aSeverity == LogMessage::SEV_WARNING || aSeverity == LogMessage::SEV_ERROR) {
#ifdef _DEBUG
		LogManager::getInstance()->message(aMsg, aSeverity, "UBN (receiver)");
		dcdebug("UBN (receiver): %s\n", aMsg.c_str());
#endif
	}
}

UploadBundleInfoReceiver::UploadBundleInfoReceiver() noexcept {
	TimerManager::getInstance()->addListener(this);
	UploadManager::getInstance()->addListener(this);
	ProtocolCommandManager::getInstance()->addListener(this);
}

UploadBundleInfoReceiver::~UploadBundleInfoReceiver() {
	TimerManager::getInstance()->removeListener(this);
	UploadManager::getInstance()->removeListener(this);
	ProtocolCommandManager::getInstance()->removeListener(this);
}

void UploadBundleInfoReceiver::onUBN(const AdcCommand& cmd) {
	string bundleToken;
	float percent = -1;
	string speedStr;

	for (const auto& str: cmd.getParameters()) {
		if (str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "DS") == 0) {
			speedStr = str.substr(2);
		} else if (str.compare(0, 2, "PE") == 0) {
			percent = Util::toFloat(str.substr(2));
		} else {
			// dbgMsg("unknown UBN param " + str + " received", LogMessage::SEV_WARNING);
		}
	}

	if ((percent < 0.00 && speedStr.empty()) || bundleToken.empty()) {
		return;
	}

	auto bundle = findByBundleToken(bundleToken);
	if (bundle) {
		if (bundle->getSingleUser()) {
			dbgMsg("UBN command ignored, bundle" + bundleToken + " is in single user mode", LogMessage::SEV_WARNING);
			return;
		}

		if (!speedStr.empty() && speedStr.length() > 2) {
			auto length = speedStr.length();
			double downloaded = Util::toDouble(speedStr.substr(0, length-1));
			if (downloaded > 0) {
				double speed = 0.0;
				if (speedStr[length-1] == 'k') {
					speed = downloaded*1024.0;
				} else if (speedStr[length-1] == 'm') {
					speed = downloaded*1048576.0;
				} else if (speedStr[length-1] == 'b') {
					speed = downloaded;
				}

				if (speed > 0) {
					bundle->setTotalSpeed(static_cast<int64_t>(speed));
				}
			}
		}

		if (percent >= 0.00 && percent <= 100.00) {
			bundle->setUploadedSegments(static_cast<int64_t>(bundle->getSize() * (percent / 100.00000)));
		}
	} else {
		dbgMsg("UBN command received, bundle" + bundleToken + " doesn't exist", LogMessage::SEV_WARNING);
	}
}

void UploadBundleInfoReceiver::createBundle(const AdcCommand& cmd) {
	string name, token, bundleToken;
	int64_t size = 0, downloaded = 0;
	bool singleUser = false;

	for (const auto& str: cmd.getParameters()) {
		if (str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else if(str.compare(0, 2, "SI") == 0) {
			size = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "NA") == 0) {
			name = str.substr(2);
		} else if (str.compare(0, 2, "DL") == 0) {
			downloaded = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "SU") == 0) {
			singleUser = true;
		} else {
			// dbgMsg("unknown create param " + str + " received", LogMessage::SEV_WARNING);
		}
	}
	
	if (bundleToken.empty() || name.empty() || size <= 0 || token.empty()) {
		dbgMsg("invalid create command received", LogMessage::SEV_WARNING);
		dcassert(0);
		return;
	}

	auto bundle = findByBundleToken(bundleToken);
	if (bundle) {
		dbgMsg("create command received for an existing bundle " + bundle->getName(), LogMessage::SEV_VERBOSE);
		changeBundle(cmd);
		return;
	}

	if (!ConnectionManager::getInstance()->tokens.addToken(bundleToken, CONNECTION_TYPE_DOWNLOAD)) {
		dbgMsg("create, duplicate bundle token " + bundleToken, LogMessage::SEV_WARNING);
		dcassert(0);
		return;
	}

	bundle = UploadBundlePtr(new UploadBundle(name, bundleToken, size, singleUser, downloaded));
	dbgMsg("create command received, created new bundle " + bundle->getName() + " with token " + bundleToken + ", downloaded " + Util::formatBytes(downloaded), LogMessage::SEV_VERBOSE);

	{
		WLock l (cs);
		bundles[bundle->getToken()] = bundle;
	}

	addBundleConnection(token, bundle);
}

void UploadBundleInfoReceiver::updateBundleInfo(const AdcCommand& cmd) {
	string name, bundleToken;
	int64_t size = 0, downloaded = 0;
	bool singleUser = false, multiUser = false;

	for (const auto& str: cmd.getParameters()) {
		if (str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "SI") == 0) {
			size = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "NA") == 0) {
			name = str.substr(2);
		}  else if (str.compare(0, 2, "SU") == 0) {
			singleUser = true;
		} else if (str.compare(0, 2, "MU") == 0) {
			multiUser = true;
		} else if (str.compare(0, 2, "DL") == 0) {
			downloaded = Util::toInt64(str.substr(2));
		} else {
			// dbgMsg("unknown update param " + str + " received", LogMessage::SEV_WARNING);
		}
	}

	if (bundleToken.empty()) {
		dbgMsg("invalid update command received", LogMessage::SEV_WARNING);
		return;
	}

	auto bundle = findByBundleToken(bundleToken);
	if (bundle) {
		if (multiUser) {
			dbgMsg("update command received, disabling single user mode for bundle " + bundleToken, LogMessage::SEV_VERBOSE);
			bundle->setSingleUser(false);
		} else if (singleUser) {
			bundle->setSingleUser(true, downloaded);
			dbgMsg("update command received, enabling single user mode for bundle " + bundleToken + ", downloaded " + Util::formatBytes(downloaded), LogMessage::SEV_VERBOSE);
		} else {
			if (size > 0) {
				dbgMsg("update command received, updating size for bundle " + bundleToken, LogMessage::SEV_VERBOSE);
				bundle->setSize(size);
			}

			/*if (!name.empty()) {
				bundle->findBundlePath(name);
			}*/
			fire(UploadBundleInfoReceiverListener::BundleSizeName(), bundle->getToken(), bundle->getTarget(), bundle->getSize());
		}
	} else {
		dbgMsg("update command received, bundle" + bundleToken + " doesn't exist", LogMessage::SEV_WARNING);
	}
}

void UploadBundleInfoReceiver::changeBundle(const AdcCommand& cmd) {
	string bundleToken;
	string token;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else {
			// dbgMsg("unknown change param " + str + " received", LogMessage::SEV_WARNING);
		}
	}
	
	if (bundleToken.empty() || token.empty()) {
		dbgMsg("invalid change command received", LogMessage::SEV_WARNING);
		return;
	}

	auto b = findByBundleToken(bundleToken);
	dcassert(b);
	if (!b) {
		dbgMsg("change command received, bundle " + bundleToken + " doesn't exist", LogMessage::SEV_WARNING);
		return;
	}

	addBundleConnection(token, b);
}

void UploadBundleInfoReceiver::removeBundleConnection(const AdcCommand& cmd) {
	string token;

	for (const auto& str : cmd.getParameters()) {
		if (str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		}
	}

	if (token.empty()) {
		dbgMsg("invalid remove command received", LogMessage::SEV_WARNING);
		return;
	}

	auto bundle = findByUploadToken(token);
	if (!bundle) {
		return;
	}

	dbgMsg("connection removal request received", LogMessage::SEV_WARNING);
	removeBundleConnection(token, bundle);
}

void UploadBundleInfoReceiver::finishBundle(const AdcCommand& cmd) {
	string bundleToken;

	for (const auto& str: cmd.getParameters()) {
		if (str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
			break;
		}
	}
	
	if (bundleToken.empty()) {
		dbgMsg("invalid finish command received", LogMessage::SEV_WARNING);
		return;
	}

	auto bundle = findByBundleToken(bundleToken);
	if (!bundle) {
		dbgMsg("finish command received, bundle " + bundleToken + " doesn't exist", LogMessage::SEV_WARNING);
		return;
	}

	dbgMsg("finishing bundle " + bundle->getName(), LogMessage::SEV_VERBOSE);
	fire(UploadBundleInfoReceiverListener::BundleComplete(), bundle->getToken(), bundle->getName());
}

void UploadBundleInfoReceiver::addBundleConnection(const string& aUploadToken, const UploadBundlePtr& aBundle) noexcept {
	auto oldBundle = findByUploadToken(aUploadToken);
	if (oldBundle && oldBundle != aBundle) {
		dbgMsg("add connection, removing connection " + aUploadToken + " from the previous bundle " + oldBundle->getName(), LogMessage::SEV_VERBOSE);
		removeBundleConnection(aUploadToken, aBundle);
	} else if (oldBundle == aBundle) {
		dbgMsg("add connection, connection " + aUploadToken + " exist in " + aBundle->getName(), LogMessage::SEV_VERBOSE);
	}

	auto found = UploadManager::getInstance()->callAsync(aUploadToken, [aBundle, this](const Upload* aUpload) {
		dbgMsg("add connection, upload " + aUpload->getToken() + " for bundle " + aBundle->getName(), LogMessage::SEV_VERBOSE);

		WLock l(cs);
		addBundleConnectionUnsafe(aUpload, aBundle);
	});

	if (!found) {
		dbgMsg("add connection, upload " + aUploadToken + " doesn't exist for bundle " + aBundle->getName() + " (saving info for possible incoming connections)", LogMessage::SEV_WARNING);

		WLock l(cs);
		uploadMap[aUploadToken] = aBundle;
	}
}

void UploadBundleInfoReceiver::removeBundleConnection(const string& aUploadToken, const UploadBundlePtr& aBundle) noexcept {
	auto found = UploadManager::getInstance()->callAsync(aUploadToken, [aBundle, this](const Upload* aUpload) {
		WLock l(cs);
		removeBundleConnectionUnsafe(aUpload, aBundle);
	});

	if (!found) {
		dbgMsg("remove connection " + aUploadToken + " for bundle " + aBundle->getName() + ", upload doesn't exist", LogMessage::SEV_WARNING);
	}
}

void UploadBundleInfoReceiver::addBundleConnectionUnsafe(const Upload* aUpload, const UploadBundlePtr& aBundle) noexcept {
	aBundle->addUpload(aUpload);

	uploadMap[aUpload->getToken()] = aBundle;
}

void UploadBundleInfoReceiver::removeBundleConnectionUnsafe(const Upload* aUpload, const UploadBundlePtr& aBundle) noexcept {
	if (aBundle->removeUpload(aUpload)) {
		dbgMsg("remove connection " + aUpload->getToken() + ", bundle " + aBundle->getName() + " empty (removal delayed), completed segments " + Util::formatBytes(aBundle->getUploadedSegments()), LogMessage::SEV_VERBOSE);
	} else {
		dbgMsg("remove connection " + aUpload->getToken() + ", keeping bundle " + aBundle->getName() + " (uploads remain), completed segments " + Util::formatBytes(aBundle->getUploadedSegments()), LogMessage::SEV_VERBOSE);
	}
}

void UploadBundleInfoReceiver::onUBD(const AdcCommand& cmd) {

	if (cmd.hasFlag("AD", 1)) {
		createBundle(cmd);
	} else if (cmd.hasFlag("CH", 1)) {
		changeBundle(cmd);
	} else if (cmd.hasFlag("UD", 1)) {
		updateBundleInfo(cmd);
	} else if (cmd.hasFlag("FI", 1)) {
		finishBundle(cmd);
	} else if (cmd.hasFlag("RM", 1)) {
		removeBundleConnection(cmd);
	} else {
		//LogManager::getInstance()->message("NO FLAG");
	}
}

UploadBundlePtr UploadBundleInfoReceiver::findByUploadToken(const string& aUploadToken) const noexcept {
	RLock l(cs);
	auto s = uploadMap.find(aUploadToken);
	if (s != uploadMap.end()) {
		return s->second;
	}

	return nullptr;
}

UploadBundlePtr UploadBundleInfoReceiver::findByBundleToken(const string& aBundleToken) const noexcept {
	RLock l(cs);
	auto s = bundles.find(aBundleToken);
	if (s != bundles.end()) {
		return s->second;
	}

	return nullptr;
}

#define BUNDLE_DELAY_SECONDS 60

// TimerManagerListener
void UploadBundleInfoReceiver::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	vector<pair<UploadBundlePtr, UploadBundle::BundleUploadList>> bundleUploads;

	{
		RLock l(cs);
		for (const auto& b: bundles) {
			if (!b.second->getUploads().empty()) {
				bundleUploads.emplace_back(b.second, b.second->getUploads());
			}
		}
	}


	TickUploadBundleList tickBundles;

	{
		RLock l(UploadManager::getInstance()->getCS());
		for (auto& b: bundleUploads) {
			UploadList uploads;
			OrderedStringSet flags;

			for (const auto& token: b.second) {
				auto u = UploadManager::getInstance()->findUploadUnsafe(token);
				if (!u) {
					continue;
				}

				u->appendFlags(flags);
				uploads.push_back(u);
			}

			if (b.first->countSpeed(uploads) > 0) {
				tickBundles.emplace_back(b.first, flags);
			}
		}
	}

	if (!tickBundles.empty()) {
		RLock l(cs);
		fire(UploadBundleInfoReceiverListener::BundleTick(), tickBundles);
	}

	removeIdleBundles();
}

void UploadBundleInfoReceiver::removeIdleBundles() noexcept {
	WLock l(cs);
	for (auto bundleIter = bundles.begin(); bundleIter != bundles.end();) {
		auto ub = bundleIter->second;
		if (ub->getUploads().empty() && ++ub->delayTime > BUNDLE_DELAY_SECONDS) {
			dbgMsg("removing an idle bundle " + bundleIter->second->getName() + ", token " + bundleIter->first, LogMessage::SEV_VERBOSE);

			for (auto upIter = uploadMap.begin(); upIter != uploadMap.end();) {
				if (upIter->second == ub) {
					dbgMsg("removing an idle token " + upIter->first, LogMessage::SEV_VERBOSE);
					upIter = uploadMap.erase(upIter);
				}
				else {
					upIter++;
				}
			}

			bundleIter = bundles.erase(bundleIter);
		} else {
			bundleIter++;
		}
	}
}

size_t UploadBundleInfoReceiver::getRunningBundleCount() const noexcept {
	RLock l(cs);
	auto ret = accumulate(bundles | boost::adaptors::map_values, (size_t)0, [&](size_t old, const UploadBundlePtr& b) {
		if (b->getSpeed() == 0) {
			return old;
		}

		return old + 1;
	});

	return ret;
}

void UploadBundleInfoReceiver::on(UploadManagerListener::Created, Upload* aUpload) noexcept {
	auto b = findByUploadToken(aUpload->getToken());
	if (!b) {
		return;
	}

	dbgMsg("upload " + aUpload->getToken() + " created, bundle " + b->getName(), LogMessage::SEV_VERBOSE);

	addBundleConnection(aUpload->getToken(), b);
}

void UploadBundleInfoReceiver::on(UploadManagerListener::Removed, const Upload* aUpload) noexcept {
	auto b = findByUploadToken(aUpload->getToken());
	if (!b) {
		return;
	}

	dbgMsg("upload " + aUpload->getToken() + " removed, bundle " + b->getName(), LogMessage::SEV_VERBOSE);

	WLock l(cs);
	removeBundleConnectionUnsafe(aUpload, b);
}

void UploadBundleInfoReceiver::on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand& aCmd, const string&) noexcept {
	if (aCmd.getCommand() == UploadBundleInfo::CMD_UBN) {
		onUBN(aCmd);
	} else if (aCmd.getCommand() == UploadBundleInfo::CMD_UBD) {
		onUBD(aCmd);
	}
}


} // namespace dcpp