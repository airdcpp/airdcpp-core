/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_HASH_MANAGER_H
#define DCPLUSPLUS_DCPP_HASH_MANAGER_H

#include <functional>
#include "typedefs.h"

#include "DbHandler.h"
#include "HashedFile.h"
#include "HashManagerListener.h"
#include "MerkleTree.h"
#include "Semaphore.h"
#include "SFVReader.h"
#include "Singleton.h"
#include "SortedVector.h"
#include "Speaker.h"
#include "Thread.h"

namespace dcpp {

class File;
class HashLoader;
class FileException;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener> {

public:

	/** We don't keep leaves for blocks smaller than this... */
	static const int64_t MIN_BLOCK_SIZE;

	HashManager();
	~HashManager();

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& fileLower, const string& aFileName, HashedFile& fi_);

	void stopHashing(const string& baseDir) noexcept;
	void setPriority(Thread::Priority p) noexcept;

	// @return HashedFileInfo
	// Throws HashException
	void getFileInfo(const string& fileLower, const string& aFileName, HashedFile& aFileInfo);

	bool getTree(const TTHValue& root, TigerTree& tt) noexcept;

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& root) noexcept;

	// Throws HashException
	void addTree(const TigerTree& tree) { store.addTree(tree); }

	void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed, int& hashers) const noexcept;

	// Get TTH for a file synchronously (and optionally stores the hash information)
	// Throws HashException/FileException
	void getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t /*timeLeft*/, const string& /*fileName*/)> updateF = nullptr);

	/**
	 * Rebuild hash data file
	 */
	void startMaintenance(bool verify);

	// Throws Exception in case of fatal errors
	void startup(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF);
	void stop() noexcept;
	void shutdown(ProgressFunction progressF) noexcept;

	struct HashPauser {
		HashPauser();
		~HashPauser();
	};
	
	/// @return whether hashing was already paused
	bool pauseHashing() noexcept;
	void resumeHashing(bool forced = false);	
	bool isHashingPaused(bool lock = true) const noexcept;

	string getDbStats() { return store.getDbStats(); }
	void compact() noexcept { store.compact(); }

	void closeDB() { store.closeDb(); }
	void onScheduleRepair(bool schedule) noexcept { store.onScheduleRepair(schedule); }
	bool isRepairScheduled() const noexcept { return store.isRepairScheduled(); }
	void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept { return store.getDbSizes(fileDbSize_, hashDbSize_); }
	bool maintenanceRunning() const noexcept { return optimizer.isRunning(); }

	// Throws HashException
	bool addFile(const string& aFilePathLower, const HashedFile& fi_);
private:
	typedef int64_t devid;

	int pausers = 0;
	class Hasher : public Thread {
	public:
		Hasher(bool isPaused, int aHasherID);

		bool hashFile(const string& filePath, const string& filePathLower, int64_t size, devid aDeviceId) noexcept;

		/// @return whether hashing was already paused
		bool pause() noexcept;
		void resume();
		bool isPaused() const noexcept;
		
		void clear() noexcept;

		void stopHashing(const string& baseDir) noexcept;
		int run();
		void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) const noexcept;
		void shutdown();

		bool hasFile(const string& aPath) const noexcept;
		bool hasDevice(int64_t aDeviceId) const noexcept { return devices.find(aDeviceId) != devices.end(); }
		bool hasDevices() const noexcept { return !devices.empty(); }
		int64_t getTimeLeft() const noexcept;

		int64_t getBytesLeft() const noexcept { return totalBytesLeft; }
		static SharedMutex hcs;

		const int hasherID;
	private:
		class WorkItem {
		public:
			WorkItem(const string& aFilePathLower, const string& aFilePath, int64_t aSize, devid aDeviceId) noexcept
				: filePath(aFilePath), fileSize(aSize), deviceId(aDeviceId), filePathLower(aFilePathLower) { }
			WorkItem(WorkItem&& rhs) = default;
			WorkItem& operator=(WorkItem&&) = default;
			WorkItem(const WorkItem&) = delete;
			WorkItem& operator=(const WorkItem&) = delete;

			string filePath;
			int64_t fileSize;
			devid deviceId;
			string filePathLower;

			struct NameLower {
				const string& operator()(const WorkItem& a) const { return a.filePathLower; }
			};
		};

		SortedVector<WorkItem, std::deque, string, Util::PathSortOrderInt, WorkItem::NameLower> w;

		Semaphore s;
		void removeDevice(devid aDevice) noexcept;

		bool closing = false;
		bool running = false;
		bool paused;

		string currentFile;
		atomic<int64_t> totalBytesLeft;
		atomic<int64_t> lastSpeed;

		void instantPause();

		int64_t totalSizeHashed = 0;
		uint64_t totalHashTime = 0;
		int totalDirsHashed = 0;
		int totalFilesHashed = 0;

		int64_t dirSizeHashed = 0;
		uint64_t dirHashTime = 0;
		int dirFilesHashed = 0;
		string initialDir;

		DirSFVReader sfv;

		map<devid, int> devices;
	};

	friend class Hasher;
	void removeHasher(Hasher* aHasher);
	void log(const string& aMessage, int hasherID, bool isError, bool lock);

	void optimize(bool doVerify) noexcept { store.optimize(doVerify); }

	class HashStore {
	public:
		HashStore();
		~HashStore();

		void addHashedFile(const string& aFilePathLower, const TigerTree& tt, const HashedFile& fi_);
		void addFile(const string& aFilePathLower, const HashedFile& fi_);
		void removeFile(const string& aFilePathLower);
		void load(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF);

		void optimize(bool doVerify) noexcept;

		bool checkTTH(const string& aFileNameLower, HashedFile& fi_) noexcept;

		void addTree(const TigerTree& tt);
		bool getFileInfo(const string& aFileLower, HashedFile& aFile) noexcept;
		bool getTree(const TTHValue& root, TigerTree& tth);
		bool hasTree(const TTHValue& root);

		enum InfoType {
			TYPE_FILESIZE,
			TYPE_BLOCKSIZE
		};
		int64_t getRootInfo(const TTHValue& aRoot, InfoType aType) noexcept;

		string getDbStats() noexcept;

		void openDb(StepFunction stepF, MessageFunction messageF);
		void closeDb() noexcept;

		void onScheduleRepair(bool aSchedule);
		bool isRepairScheduled() const noexcept;

		void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept;
		void compact() noexcept;
	private:
		std::unique_ptr<DbHandler> fileDb;
		std::unique_ptr<DbHandler> hashDb;


		friend class HashLoader;

		/** FOR CONVERSION ONLY: Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		void loadLegacyTree(File& dataFile, int64_t aSize, int64_t aIndex, int64_t aBlockSize, size_t aDataLength, const TTHValue& root, TigerTree& tt_);



		static bool loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool aReportCorruption);

		static bool loadFileInfo(const void* src, size_t len, HashedFile& aFile);
		static void saveFileInfo(void *dest, const HashedFile& aTree);
		static uint32_t getFileInfoSize(const HashedFile& aTree);
	};

	friend class HashLoader;

	bool hashFile(const string& filePath, const string& pathLower, int64_t size);
	bool aShutdown = false;

	typedef vector<Hasher*> HasherList;
	HasherList hashers;

	HashStore store;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	void hashDone(const string& aFileName, const string& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID = 0) noexcept;

	class Optimizer : public Thread {
	public:
		Optimizer();
		~Optimizer();

		void startMaintenance(bool verify);
		bool isRunning() const noexcept { return running; }
	private:
		bool verify = true;
		atomic<bool> running = { false };
		virtual int run();
	};

	Optimizer optimizer;
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)