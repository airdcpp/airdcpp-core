/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_DIRECT_SEARCH_H
#define DCPLUSPLUS_DCPP_DIRECT_SEARCH_H

#include <airdcpp/forward.h>

#include <airdcpp/hub/ClientManagerListener.h>
#include <airdcpp/search/SearchManagerListener.h>

#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/core/timer/TimerManager.h>

namespace dcpp {

	class DirectSearch : private SearchManagerListener,
		private ClientManagerListener
	{
	public:

		DirectSearch(const HintedUser& aUser, const SearchPtr& aSearch, uint64_t aNoResultTimeout = 5000);
		~DirectSearch() override;

		size_t getResultCount() const noexcept { return results.size(); }

		bool finished() noexcept;

		const SearchResultList& getResults() const noexcept {
			return results;
		}

		void getAdcPaths(OrderedStringSet& paths_, bool aParents) const noexcept;

		bool hasTimedOut() const noexcept {
			return timedOut;
		}
	private:
		void on(SearchManagerListener::SR, const SearchResultPtr& aSR) noexcept override;

		// ClientManagerListener
		void on(ClientManagerListener::DirectSearchEnd, const string& aToken, int aResultCount) noexcept override;

		void removeListeners() noexcept;

		SearchResultList results;

		int curResultCount = 0;
		int maxResultCount = -1;
		uint64_t noResultTimeout = 0;
		uint64_t lastResult = 0;
		uint64_t started = GET_TICK();
		string searchToken;

		HintedUser hintedUser;
		bool timedOut = false;
	};

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
