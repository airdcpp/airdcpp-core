/*
 * Copyright (C) 2013-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DIRECTORY_MONITOR
#define DCPLUSPLUS_DIRECTORY_MONITOR

#include <airdcpp/typedefs.h>

#include "DirectoryMonitorListener.h"

#include <airdcpp/DispatcherQueue.h>
#include <airdcpp/Exception.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Thread.h>
#include <airdcpp/Util.h>

using std::string;

namespace dcpp {

typedef std::function<void ()> AsyncF;

class Monitor;
class DirectoryMonitor : public Speaker<DirectoryMonitorListener> {
public:
	DirectoryMonitor(int numThreads, bool useDispatcherThread);
	~DirectoryMonitor();

	// Throws MonitorException
	bool addDirectory(const string& aPath);
	bool removeDirectory(const string& aPath);
	size_t clear();

	// returns the paths that were restored successfully
	set<string> restoreFailedPaths();
	size_t getFailedCount();
	void deviceRemoved(const string& aDrive) { server->deviceRemoved(aDrive); }

	void stopMonitoring();

	// Throws MonitorException
	void init();

	// returns true as long as there are messages queued
	bool dispatch();
	void callAsync(DispatcherQueue::Callback&& aF);
	string getStats() const {
		return server->getStats();
	}
	bool hasDirectories() const {
		return server->hasDirectories();
	}
private:
	friend class Monitor;
	class Server : public Thread {
	public:
		Server(DirectoryMonitor* aBase, int numThreads);
		~Server();
		bool addDirectory(const string& aPath);
		bool removeDirectory(const string& aPath);
		size_t clear();

		void stop();

		DirectoryMonitor* base;
		virtual int run();

		// Throws MonitorException
		void init();

		string getStats() const;
		bool hasDirectories() const;
		static string getErrorStr(int error);
		set<string> restoreFailedPaths();
		size_t getFailedCount();
		void deviceRemoved(const string& aDrive);

		// ReadDirectoryChangesW doesn't notice if the path gets removed. This function should be
		// polled periodically in order to remove missing paths from monitoring.
		//void validatePathExistance();  // replaced by deviceRemoved for now.. 
	private:
		typedef std::unordered_map<string, Monitor*, noCaseStringHash, noCaseStringEq> MonitorMap;
		mutable SharedMutex cs;
		MonitorMap monitors;

		int read();

		bool m_bTerminate;
		atomic_flag threadRunning;

		// finally removes the directory from the list (monitoring needs to be stopped first)
		// also closes m_hIOCP if the list is empty
		// must be called from inside WLock
		void deleteDirectory(MonitorMap::iterator mon);

		// must be called from inside WLock
		void failDirectory(const string& path, const string& aReason);
#ifdef WIN32
		HANDLE m_hIOCP;
#else
		int efd = -1;
		int fd = -1;
#endif
		int	m_nThreads;
		set<string> failedDirectories;
	};

	Server* server;

	void processNotification(const string& aPath, const ByteVector& aBuf);
	DispatcherQueue dispatcher;
};

class Monitor : boost::noncopyable {
public:
	static int lastKey;
	friend class DirectoryMonitor;

#ifdef WIN32
	Monitor(const string& aPath, DirectoryMonitor::Server* aParent, int monitorFlags, size_t bufferSize, bool recursive);
	~Monitor();

	void openDirectory(HANDLE iocp);
	void beginRead();
#else
	//int addWatch(const string& aPath);

	Monitor(const string& aPath, DirectoryMonitor::Server* aParent, int monitorFlags, size_t bufferSize);
	~Monitor();
#endif

	void stopMonitoring();

	void queueNotificationTask(int dwSize);
	DirectoryMonitor::Server* server;
private:
	uint64_t changes;
#ifdef WIN32
	// Parameters from the caller for ReadDirectoryChangesW().
	int				m_dwFlags;
	int				m_bChildren;
	const string	path;

	// Result of calling CreateFile().
	HANDLE		m_hDirectory;

	// Required parameter for ReadDirectoryChangesW().
	OVERLAPPED	m_Overlapped;

	// Data buffer for the request.
	// Since the memory is allocated by malloc, it will always
	// be aligned as required by ReadDirectoryChangesW().
	ByteVector m_Buffer;
	int errorCount;
	int key;
#else

#endif
};

} //dcpp

#endif // DCPLUSPLUS_DIRECTORY_MONITOR
