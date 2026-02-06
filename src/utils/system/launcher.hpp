#pragma once

#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>
#include <windows.h>

#include "../../components/data.h"
#include "core/logging.hpp"
#include "network/roblox/auth.h"
#include "network/roblox/hba.h"
#include "roblox_control.h"
#include "ui/notifications.h"

// Extracts the browserid numeric value from an RBXEventTrackerV2 cookie value.
// Used only for the legacy launch URI which requires digits.
static std::string ExtractBrowserIdFromTrackerCookie(const std::string &trackerCookie) {
	auto isDigit = [](unsigned char c) { return c >= '0' && c <= '9'; };

	const std::string key = "browserid=";
	size_t pos = trackerCookie.find(key);
	if (pos == std::string::npos) { return {}; }
	pos += key.size();

	// Skip optional whitespace
	while (pos < trackerCookie.size() && (trackerCookie[pos] == ' ' || trackerCookie[pos] == '\t')) { ++pos; }

	size_t start = pos;
	while (pos < trackerCookie.size() && isDigit(static_cast<unsigned char>(trackerCookie[pos]))) { ++pos; }

	if (pos == start) { return {}; }
	return trackerCookie.substr(start, pos - start);
}

// Generate a random browser tracker ID as a fallback
static std::string GenerateRandomBrowserTrackerId() {
	thread_local std::mt19937_64 rng {std::random_device {}()};
	static std::uniform_int_distribution<int> d1(100000, 130000), d2(100000, 900000);
	return std::to_string(d1(rng)) + std::to_string(d2(rng));
}

static std::string urlEncode(const std::string &s) {
	std::ostringstream out;
	out << std::hex << std::uppercase;
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out << c;
		} else {
			out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
		}
	}
	return out.str();
}

/**
 * Launch Roblox for a specific place/server with HBA-enabled authentication
 * @param placeId The place ID to join
 * @param jobId The job ID (server instance), can be empty for random server
 * @param creds Authentication credentials with HBA support
 * @return Process handle, or nullptr on failure
 */
inline HANDLE startRoblox(uint64_t placeId, const std::string &jobId, const Roblox::HBA::AuthCredentials &creds) {
	LOG_INFO("Fetching authentication ticket (HBA-enabled)");

	// Use the HBA-enabled fetchAuthTicket which handles CSRF and BAT
	std::string ticket = Roblox::fetchAuthTicket(creds.toAuthConfig());

	if (ticket.empty()) {
		LOG_ERROR("Failed to get authentication ticket");
		std::cerr << "failed to get authentication ticket\n";
		return nullptr;
	}

	auto nowMs
		= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
			  .count();
	std::ostringstream ts;
	ts << nowMs;

	// Extract browserid digits from RBXEventTrackerV2 cookie, or fall back to random
	std::string browserTrackerId = ExtractBrowserIdFromTrackerCookie(creds.rbxEventTrackerCookie);
	if (browserTrackerId.empty()) { browserTrackerId = GenerateRandomBrowserTrackerId(); }

	std::string placeLauncherUrl = "https://assetgame.roblox.com/game/PlaceLauncher.ashx?"
								   "request=RequestGameJob"
								   "&browserTrackerId="
								 + browserTrackerId + "&placeId=" + std::to_string(placeId) + "&gameId=" + jobId
								 + "&isPlayTogetherGame=false";

	std::string protocolLaunchCommand = "roblox-player:1+launchmode:play"
										"+gameinfo:"
									  + ticket + "+launchtime:" + ts.str() + "+browsertrackerid:" + browserTrackerId
									  + "+placelauncherurl:" + urlEncode(placeLauncherUrl)
									  + "+robloxLocale:en_us+gameLocale:en_us+channel:";

	std::string logMessage = "Attempting to launch Roblox for place ID: " + std::to_string(placeId)
						   + (jobId.empty() ? "" : " with Job ID: " + jobId);
	LOG_INFO(logMessage);

	std::wstring notificationTitle = L"Launching";
	std::wostringstream notificationMessageStream;
	notificationMessageStream << L"Attempting to launch Roblox for place ID: " << placeId;
	if (!jobId.empty()) { notificationMessageStream << L" with Job ID: " << jobId.c_str(); }
	Notifications::showNotification(notificationTitle.c_str(), notificationMessageStream.str().c_str());

	SHELLEXECUTEINFOA executionInfo {sizeof(executionInfo)};
	executionInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	executionInfo.lpVerb = "open";
	executionInfo.lpFile = protocolLaunchCommand.c_str();
	executionInfo.nShow = SW_SHOWNORMAL;

	if (!ShellExecuteExA(&executionInfo)) {
		LOG_ERROR("ShellExecuteExA failed for Roblox launch. Error: " + std::to_string(GetLastError()));
		std::cerr << "ShellExecuteEx failed: " << GetLastError() << "\n";
		return nullptr;
	}

	LOG_INFO("Roblox process started successfully for place ID: " + std::to_string(placeId));
	return executionInfo.hProcess;
}

/**
 * Launch Roblox sequentially for multiple accounts with HBA support
 * @param placeId The place ID to join
 * @param jobId The job ID (server instance), can be empty for random server
 * @param accounts Vector of authentication credentials for each account
 */
inline void launchRobloxSequential(
	uint64_t placeId,
	const std::string &jobId,
	const std::vector<Roblox::HBA::AuthCredentials> &accounts
) {
	if (g_killRobloxOnLaunch) { RobloxControl::KillRobloxProcesses(); }

	if (g_clearCacheOnLaunch) { RobloxControl::ClearRobloxCache(); }

	for (const auto &creds : accounts) {
		LOG_INFO(
			"Launching Roblox for account ID: " + std::to_string(creds.accountId)
			+ " PlaceID: " + std::to_string(placeId) + (jobId.empty() ? "" : " JobID: " + jobId)
		);
		HANDLE proc = startRoblox(placeId, jobId, creds);
		if (proc) {
			WaitForInputIdle(proc, INFINITE);
			CloseHandle(proc);
			LOG_INFO("Roblox launched successfully for account ID: " + std::to_string(creds.accountId));
		} else {
			LOG_ERROR("Failed to start Roblox for account ID: " + std::to_string(creds.accountId));
		}
	}
}
