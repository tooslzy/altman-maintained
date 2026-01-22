#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "../data.h"
#include "network/roblox.h"
#include "network/roblox/hba.h"

namespace FriendsActions {
	void RefreshFullFriendsList(
		int accountId,
		const std::string &userId,
		const Roblox::HBA::AuthCredentials &creds,
		std::vector<FriendInfo> &outFriendsList,
		std::atomic<bool> &loadingFlag
	);

	void FetchFriendDetails(
		const std::string &friendId,
		const Roblox::HBA::AuthCredentials &creds,
		Roblox::FriendDetail &outFriendDetail,
		std::atomic<bool> &loadingFlag
	);
} // namespace FriendsActions
