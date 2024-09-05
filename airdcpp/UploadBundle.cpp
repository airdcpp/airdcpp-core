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

#include "stdinc.h"
#include "UploadBundle.h"

#include "ConnectionManager.h"
#include "PathUtil.h"
#include "Util.h"

namespace dcpp {

UploadBundle::UploadBundle(const string& aTarget, const string& aToken, int64_t aSize, bool aSingleUser, int64_t aUploaded) : target(aTarget), token(aToken), size(aSize),
	singleUser(aSingleUser), uploadedSegments(aUploaded) { 

	if (uploadedSegments > size)
		uploadedSegments = size;
}

UploadBundle::~UploadBundle() {
	dcdebug("Removing upload bundle %s", getName().c_str());
	ConnectionManager::getInstance()->tokens.removeToken(getToken());
}

void UploadBundle::addUploadedSegment(int64_t aSize) noexcept {
	dcassert(aSize + uploadedSegments <= size);
	if (singleUser && aSize + uploadedSegments <= size) {
		// countSpeed();
		uploadedSegments += aSize;
		currentUploaded -= aSize;
	} else {
		dcassert(0);
	}
}

void UploadBundle::setSingleUser(bool aSingleUser, int64_t aUploadedSegments) noexcept {
	if (aSingleUser) {
		singleUser = true;
		totalSpeed = 0;
		if (aUploadedSegments <= size) {
			uploadedSegments = aUploadedSegments;
		}
	} else {
		singleUser = false;
		currentUploaded = 0;
	}
}

uint64_t UploadBundle::getSecondsLeft() const noexcept {
	auto avg = totalSpeed > 0 ? totalSpeed : speed;
	int64_t bytesLeft =  getSize() - getUploaded();
	return (avg > 0) ? static_cast<int64_t>(bytesLeft / avg) : 0;
}

string UploadBundle::getName() const noexcept {
	if (target.back() == PATH_SEPARATOR) {
		return PathUtil::getLastDir(target);
	} else {
		return PathUtil::getFilePath(target);
	}
}

void UploadBundle::addUpload(const Upload* u) noexcept {
	// dcassert(uploads.find(u->getToken()) == uploads.end());
	uploads.insert(u->getToken());

	if (uploads.size() == 1) {
		findBundlePath(target, u);
		delayTime = 0;
	}
}

const UploadBundle::BundleUploadList& UploadBundle::getUploads() const noexcept {
	return uploads; 
}

int UploadBundle::getConnectionCount() const noexcept {
	return (int)uploads.size(); 
}

bool UploadBundle::removeUpload(const Upload* u) noexcept {
	auto s = uploads.find(u->getToken());
	dcassert(s != uploads.end());
	if (s != uploads.end()) {
		addUploadedSegment(u->getPos());
		uploads.erase(s);

		auto isEmpty = uploads.empty();
		return isEmpty;
	}

	return uploads.empty();
}

uint64_t UploadBundle::getUploaded() const noexcept {
	return currentUploaded + uploadedSegments; 
}

uint64_t UploadBundle::countSpeed(const UploadList& aUploads) noexcept {
	double bundleRatio = 0;
	int64_t ownBundleSpeed = 0, bundlePos = 0;
	int up = 0;
	for (auto u: aUploads) {
		if (u->getStart() > 0) {
			ownBundleSpeed += u->getAverageSpeed();

			up++;
			int64_t pos = u->getPos();
			bundleRatio += pos > 0 ? (double)u->getActual() / (double)pos : 1.00;
			bundlePos += pos;
		}
	}

	if (ownBundleSpeed > 0) {
		if (singleUser) {
			currentUploaded = bundlePos;
		}

		speed = ownBundleSpeed;
		bundleRatio = bundleRatio / up;
		actual = ((int64_t)((double)getUploaded() * (bundleRatio == 0 ? 1.00 : bundleRatio)));
	}

	return ownBundleSpeed;
}

void UploadBundle::findBundlePath(const string& aName, const Upload* aUpload) noexcept {
	const auto& uploadPath = aUpload->getPath();
	auto pos = uploadPath.rfind(aName);
	if (pos != string::npos) {
		if (pos + aName.length() == uploadPath.length()) {
			// File bundle
			target = uploadPath;
		} else {
			// Directory bundle
			target = uploadPath.substr(0, pos + aName.length());
		}
	}
}

}