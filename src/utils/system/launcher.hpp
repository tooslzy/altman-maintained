#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "../../components/data.h"
#include "core/app_state.h"
#include "core/logging.hpp"
#include "network/roblox/auth.h"
#include "network/roblox/hba.h"
#include "roblox_control.h"
#include "ui/notifications.h"

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

enum class RobloxJoinKind {
	Place,
	Instance,
	UserResolved,
	UserFollow,
	Link,
	PrivateServer,
	ShareLink,
};

struct RobloxLaunchRequest {
		RobloxJoinKind kind = RobloxJoinKind::Place;
		uint64_t placeId = 0;
		std::string jobId;
		uint64_t userId = 0;
		std::string username;
		std::string rawLink;
		std::string accessCode;
		std::string linkCode;
		std::string shareCode;
		std::string shareType;
};

inline std::string trimCopy(std::string s) {
	auto l = s.find_first_not_of(" \t\n\r");
	auto r = s.find_last_not_of(" \t\n\r");
	if (l == std::string::npos) { return {}; }
	return s.substr(l, r - l + 1);
}

inline std::string urlDecode(const std::string &s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			auto hexValue = [](char c) -> int {
				if (c >= '0' && c <= '9') { return c - '0'; }
				if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
				if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
				return -1;
			};
			int hi = hexValue(s[i + 1]);
			int lo = hexValue(s[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(s[i] == '+' ? ' ' : s[i]);
	}
	return out;
}

inline std::unordered_map<std::string, std::string> parseQueryString(const std::string &query) {
	std::unordered_map<std::string, std::string> values;
	size_t pos = 0;
	while (pos <= query.size()) {
		size_t amp = query.find('&', pos);
		std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
		if (!part.empty()) {
			size_t eq = part.find('=');
			std::string key = urlDecode(eq == std::string::npos ? part : part.substr(0, eq));
			std::string value = eq == std::string::npos ? std::string() : urlDecode(part.substr(eq + 1));
			values[key] = value;
		}
		if (amp == std::string::npos) { break; }
		pos = amp + 1;
	}
	return values;
}

inline bool parseUnsignedId(const std::string &s, uint64_t &out) {
	std::string trimmed = trimCopy(s);
	if (trimmed.empty()) { return false; }
	uint64_t value = 0;
	for (char c : trimmed) {
		if (c < '0' || c > '9') { return false; }
		uint64_t next = value * 10 + static_cast<uint64_t>(c - '0');
		if (next < value) { return false; }
		value = next;
	}
	out = value;
	return true;
}

inline std::string buildPlaceLauncherUrl(const RobloxLaunchRequest &req, const std::string &browserTrackerId) {
	switch (req.kind) {
	case RobloxJoinKind::Place:
		return "https://assetgame.roblox.com/game/PlaceLauncher.ashx?request=RequestGame"
			 + std::string("&browserTrackerId=") + browserTrackerId + "&placeId=" + std::to_string(req.placeId)
			 + "&isPlayTogetherGame=false";

	case RobloxJoinKind::Instance:
	case RobloxJoinKind::UserResolved:
		return "https://assetgame.roblox.com/game/PlaceLauncher.ashx?request=RequestGameJob"
			 + std::string("&browserTrackerId=") + browserTrackerId + "&placeId=" + std::to_string(req.placeId)
			 + "&gameId=" + urlEncode(req.jobId) + "&isPlayTogetherGame=false";

	case RobloxJoinKind::UserFollow:
		return "https://assetgame.roblox.com/game/PlaceLauncher.ashx?request=RequestFollowUser&userId="
			 + std::to_string(req.userId);

	case RobloxJoinKind::PrivateServer:
		return "https://assetgame.roblox.com/game/PlaceLauncher.ashx?request=RequestPrivateGame"
			 + std::string("&placeId=") + std::to_string(req.placeId) + "&accessCode=" + urlEncode(req.accessCode)
			 + "&linkCode=" + urlEncode(req.linkCode);

	case RobloxJoinKind::ShareLink:
	case RobloxJoinKind::Link: return std::string();
	}

	return std::string();
}

inline bool validateLaunchRequest(const RobloxLaunchRequest &req, std::string *error = nullptr) {
	auto fail = [&](const char *message) {
		if (error) { *error = message; }
		return false;
	};

	switch (req.kind) {
	case RobloxJoinKind::Place: return req.placeId != 0 || fail("Place launch requires a placeId.");
	case RobloxJoinKind::Instance:
	case RobloxJoinKind::UserResolved:
		if (req.placeId == 0) { return fail("Instance launch requires a placeId."); }
		if (req.jobId.empty()) { return fail("Instance launch requires a jobId."); }
		return true;
	case RobloxJoinKind::UserFollow: return req.userId != 0 || fail("Follow-user launch requires a userId.");
	case RobloxJoinKind::PrivateServer:
		if (req.placeId == 0) { return fail("Private server launch requires a placeId."); }
		if (req.accessCode.empty()) { return fail("Private server launch requires an accessCode."); }
		if (req.linkCode.empty()) { return fail("Private server launch requires a linkCode."); }
		return true;
	case RobloxJoinKind::ShareLink:
		if (req.shareCode.empty()) { return fail("Share-link launch requires a shareCode."); }
		if (req.shareType.empty()) { return fail("Share-link launch requires a share type."); }
		return true;
	case RobloxJoinKind::Link: return fail("Link inputs must be resolved before launching.");
	}

	return fail("Unknown Roblox launch request.");
}

inline std::string buildRobloxPlayerUri(const RobloxLaunchRequest &req, const std::string &ticket) {
	auto nowMs
		= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
			  .count();
	std::ostringstream ts;
	ts << nowMs;

	std::string browserTrackerId = "1";
	std::string placeLauncherUrl = buildPlaceLauncherUrl(req, browserTrackerId);
	if (placeLauncherUrl.empty()) { return {}; }

	return "roblox-player:1+launchmode:play" + std::string("+gameinfo:") + ticket + "+launchtime:" + ts.str()
		 + "+browsertrackerid:" + browserTrackerId + "+placelauncherurl:" + urlEncode(placeLauncherUrl)
		 + "+robloxLocale:en_us+gameLocale:en_us+channel:";
}

inline std::string describeLaunchRequest(const RobloxLaunchRequest &req) {
	switch (req.kind) {
	case RobloxJoinKind::Place: return "place ID: " + std::to_string(req.placeId);
	case RobloxJoinKind::Instance: return "place ID: " + std::to_string(req.placeId) + " with Job ID: " + req.jobId;
	case RobloxJoinKind::UserResolved:
		return "resolved user target place ID: " + std::to_string(req.placeId) + " with Job ID: " + req.jobId;
	case RobloxJoinKind::UserFollow: return "follow user ID: " + std::to_string(req.userId);
	case RobloxJoinKind::PrivateServer: return "private server place ID: " + std::to_string(req.placeId);
	case RobloxJoinKind::ShareLink: return "share link code: " + req.shareCode + " type: " + req.shareType;
	case RobloxJoinKind::Link: return "unresolved link";
	}

	return "unknown target";
}

inline RobloxLaunchRequest makePlaceLaunchRequest(uint64_t placeId) {
	RobloxLaunchRequest req {};
	req.kind = RobloxJoinKind::Place;
	req.placeId = placeId;
	return req;
}

inline RobloxLaunchRequest makeInstanceLaunchRequest(uint64_t placeId, const std::string &jobId) {
	RobloxLaunchRequest req {};
	req.kind = RobloxJoinKind::Instance;
	req.placeId = placeId;
	req.jobId = jobId;
	return req;
}

inline RobloxLaunchRequest makePlaceOrInstanceLaunchRequest(uint64_t placeId, const std::string &jobId) {
	return jobId.empty() ? makePlaceLaunchRequest(placeId) : makeInstanceLaunchRequest(placeId, jobId);
}

inline bool extractPrivateServerAccessCode(const std::string &html, std::string &accessCode) {
	size_t launchPos = html.find("joinPrivateGame");
	if (launchPos == std::string::npos) { return false; }

	size_t accessPos = html.find("AccessCode", launchPos);
	if (accessPos != std::string::npos) {
		size_t quote = html.find_first_of("\"'", accessPos + 10);
		if (quote != std::string::npos) {
			char quoteChar = html[quote];
			size_t end = html.find(quoteChar, quote + 1);
			if (end != std::string::npos && end > quote + 1) {
				accessCode = html.substr(quote + 1, end - quote - 1);
				return true;
			}
		}
	}

	size_t openParen = html.find('(', launchPos);
	size_t closeParen = openParen == std::string::npos ? std::string::npos : html.find(')', openParen);
	if (openParen == std::string::npos || closeParen == std::string::npos) { return false; }
	size_t quote = html.find_first_of("\"'", openParen);
	if (quote == std::string::npos || quote > closeParen) { return false; }
	char quoteChar = html[quote];
	size_t end = html.find(quoteChar, quote + 1);
	if (end != std::string::npos && end <= closeParen && end > quote + 1) {
		accessCode = html.substr(quote + 1, end - quote - 1);
		return true;
	}

	return false;
}

inline bool extractPrivateServerLinkFromHtml(const std::string &html, uint64_t &placeId, std::string &linkCode) {
	size_t codePos = html.find("privateServerLinkCode=");
	if (codePos == std::string::npos) { return false; }

	size_t valueStart = codePos + std::string("privateServerLinkCode=").size();
	size_t valueEnd = valueStart;
	while (valueEnd < html.size()) {
		char c = html[valueEnd];
		if (c == '&' || c == '"' || c == '\'' || c == '<' || c == '>' || std::isspace(static_cast<unsigned char>(c))) {
			break;
		}
		++valueEnd;
	}
	if (valueEnd <= valueStart) { return false; }

	size_t gamesPos = html.rfind("/games/", codePos);
	if (gamesPos == std::string::npos) { return false; }
	size_t idStart = gamesPos + std::string("/games/").size();
	size_t idEnd = idStart;
	while (idEnd < html.size() && html[idEnd] >= '0' && html[idEnd] <= '9') { ++idEnd; }
	if (idEnd <= idStart) { return false; }

	if (!parseUnsignedId(html.substr(idStart, idEnd - idStart), placeId)) { return false; }
	linkCode = urlDecode(html.substr(valueStart, valueEnd - valueStart));
	return !linkCode.empty();
}

inline bool resolvePrivateServerLinkCode(
	uint64_t placeId,
	const std::string &linkCode,
	const Roblox::HBA::AuthCredentials &creds,
	RobloxLaunchRequest &out,
	std::string *error = nullptr
) {
	std::string url
		= "https://www.roblox.com/games/" + std::to_string(placeId) + "?privateServerLinkCode=" + urlEncode(linkCode);
	auto response = Roblox::AuthenticatedHttp::get(url, creds.toAuthConfig());
	if (response.status_code < 200 || response.status_code >= 300) {
		if (error) { *error = "Failed to resolve private server link."; }
		return false;
	}

	std::string accessCode;
	if (!extractPrivateServerAccessCode(response.text, accessCode)) {
		if (error) { *error = "Private server access code was not found in Roblox response."; }
		return false;
	}

	out.kind = RobloxJoinKind::PrivateServer;
	out.placeId = placeId;
	out.linkCode = linkCode;
	out.accessCode = accessCode;
	return true;
}

inline bool resolveShareCode(
	const std::string &shareCode,
	const std::string &shareType,
	const Roblox::HBA::AuthCredentials &creds,
	RobloxLaunchRequest &out,
	std::string *error = nullptr
) {
	if (shareType != "Server") {
		out.kind = RobloxJoinKind::ShareLink;
		out.shareCode = shareCode;
		out.shareType = shareType;
		return true;
	}

	std::string url = "https://www.roblox.com/share?code=" + urlEncode(shareCode) + "&type=" + urlEncode(shareType);
	auto response = Roblox::AuthenticatedHttp::get(url, creds.toAuthConfig());
	if (response.status_code < 200 || response.status_code >= 300) {
		out.kind = RobloxJoinKind::ShareLink;
		out.shareCode = shareCode;
		out.shareType = shareType;
		return true;
	}

	uint64_t placeId = 0;
	std::string linkCode;
	if (extractPrivateServerLinkFromHtml(response.text, placeId, linkCode)) {
		return resolvePrivateServerLinkCode(placeId, linkCode, creds, out, error);
	}

	std::string accessCode;
	if (extractPrivateServerAccessCode(response.text, accessCode)) {
		out.kind = RobloxJoinKind::ShareLink;
		out.shareCode = shareCode;
		out.shareType = shareType;
		return true;
	}

	out.kind = RobloxJoinKind::ShareLink;
	out.shareCode = shareCode;
	out.shareType = shareType;
	return true;
}

inline bool parseRobloxLaunchLink(const std::string &rawInput, RobloxLaunchRequest &out, std::string *error = nullptr) {
	std::string input = trimCopy(rawInput);
	if (input.empty()) {
		if (error) { *error = "Enter a Roblox link."; }
		return false;
	}

	out = {};
	out.kind = RobloxJoinKind::Link;
	out.rawLink = input;

	auto fail = [&](const char *message) {
		if (error) { *error = message; }
		return false;
	};

	if (input.rfind("roblox://", 0) == 0) {
		std::string protocolBody = input.substr(std::string("roblox://").size());
		if (protocolBody.rfind("navigation/share_links", 0) == 0) {
			size_t queryStart = protocolBody.find('?');
			if (queryStart == std::string::npos) { return fail("Share deep link must include code and type."); }
			auto values = parseQueryString(protocolBody.substr(queryStart + 1));
			auto codeIt = values.find("code");
			auto typeIt = values.find("type");
			if (codeIt == values.end() || codeIt->second.empty()) { return fail("Share deep link must include code."); }
			if (typeIt == values.end() || typeIt->second.empty()) { return fail("Share deep link must include type."); }
			out.kind = RobloxJoinKind::Link;
			out.rawLink = input;
			out.shareCode = codeIt->second;
			out.shareType = typeIt->second;
			return true;
		}

		std::string query = protocolBody;
		auto values = parseQueryString(query);
		uint64_t placeId = 0;
		auto placeIt = values.find("placeId");
		if (placeIt == values.end() || !parseUnsignedId(placeIt->second, placeId)) {
			return fail("roblox:// link must include placeId.");
		}
		auto instanceIt = values.find("gameInstanceId");
		out = instanceIt == values.end() || instanceIt->second.empty()
				? makePlaceLaunchRequest(placeId)
				: makeInstanceLaunchRequest(placeId, instanceIt->second);
		out.rawLink = input;
		return true;
	}

	std::string lower = input;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	if (lower.rfind("https://www.roblox.com/", 0) != 0 && lower.rfind("http://www.roblox.com/", 0) != 0
		&& lower.rfind("https://roblox.com/", 0) != 0 && lower.rfind("http://roblox.com/", 0) != 0) {
		return fail("Link must be a supported Roblox URL.");
	}

	size_t schemeEnd = input.find("://");
	size_t pathStart = input.find('/', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
	if (pathStart == std::string::npos) { return fail("Roblox URL must include a path."); }
	size_t queryStart = input.find('?', pathStart);
	std::string path
		= input.substr(pathStart, queryStart == std::string::npos ? std::string::npos : queryStart - pathStart);
	std::string query = queryStart == std::string::npos ? std::string() : input.substr(queryStart + 1);
	auto values = parseQueryString(query);

	if (path == "/games/start") {
		uint64_t placeId = 0;
		auto placeIt = values.find("placeId");
		if (placeIt == values.end() || !parseUnsignedId(placeIt->second, placeId)) {
			return fail("games/start URL must include placeId.");
		}
		auto instanceIt = values.find("gameInstanceId");
		out = instanceIt == values.end() || instanceIt->second.empty()
				? makePlaceLaunchRequest(placeId)
				: makeInstanceLaunchRequest(placeId, instanceIt->second);
		out.rawLink = input;
		return true;
	}

	if (path == "/share" || path == "/share-links") {
		auto codeIt = values.find("code");
		auto typeIt = values.find("type");
		if (codeIt == values.end() || codeIt->second.empty()) { return fail("Share URL must include code."); }
		if (typeIt == values.end() || typeIt->second.empty()) { return fail("Share URL must include type."); }
		out.kind = RobloxJoinKind::Link;
		out.rawLink = input;
		out.shareCode = codeIt->second;
		out.shareType = typeIt->second;
		return true;
	}

	if (path.rfind("/games/", 0) == 0) {
		uint64_t placeId = 0;
		size_t idStart = std::string("/games/").size();
		size_t idEnd = idStart;
		while (idEnd < path.size() && path[idEnd] >= '0' && path[idEnd] <= '9') { ++idEnd; }
		if (idEnd <= idStart || !parseUnsignedId(path.substr(idStart, idEnd - idStart), placeId)) {
			return fail("Game URL has an invalid placeId.");
		}
		auto privateIt = values.find("privateServerLinkCode");
		if (privateIt != values.end() && !privateIt->second.empty()) {
			out.kind = RobloxJoinKind::PrivateServer;
			out.placeId = placeId;
			out.linkCode = privateIt->second;
			out.rawLink = input;
			return true;
		}
		out = makePlaceLaunchRequest(placeId);
		out.rawLink = input;
		return true;
	}

	return fail("Unsupported Roblox link format.");
}

inline bool resolveRobloxLaunchLink(
	const std::string &rawInput,
	const Roblox::HBA::AuthCredentials &creds,
	RobloxLaunchRequest &out,
	std::string *error = nullptr
) {
	RobloxLaunchRequest parsed {};
	if (!parseRobloxLaunchLink(rawInput, parsed, error)) { return false; }

	if (parsed.kind == RobloxJoinKind::PrivateServer && !parsed.linkCode.empty() && parsed.accessCode.empty()) {
		return resolvePrivateServerLinkCode(parsed.placeId, parsed.linkCode, creds, out, error);
	}

	if (parsed.kind == RobloxJoinKind::Link && !parsed.shareCode.empty()) {
		return resolveShareCode(parsed.shareCode, parsed.shareType, creds, out, error);
	}

	out = parsed;
	return validateLaunchRequest(out, error);
}

/**
 * Launch Roblox for a specific normalized target with HBA-enabled authentication
 * @param req The normalized launch request
 * @param creds Authentication credentials with HBA support
 * @return Process handle, or nullptr on failure
 */
inline HANDLE startRoblox(const RobloxLaunchRequest &req, const Roblox::HBA::AuthCredentials &creds) {
	std::string validationError;
	if (!validateLaunchRequest(req, &validationError)) {
		LOG_ERROR("Invalid Roblox launch request: " + validationError);
		std::cerr << validationError << "\n";
		return nullptr;
	}

	LOG_INFO("Fetching authentication ticket (HBA-enabled)");

	// Use the HBA-enabled fetchAuthTicket which handles CSRF and BAT
	std::string ticket = Roblox::fetchAuthTicket(creds.toAuthConfig());

	if (ticket.empty()) {
		LOG_ERROR("Failed to get authentication ticket");
		std::cerr << "failed to get authentication ticket\n";
		return nullptr;
	}

	std::string browserTrackerId = "1";
	if (req.kind == RobloxJoinKind::ShareLink) {
		std::string appAuthCommand = "roblox-player:1+launchmode:app" + std::string("+gameinfo:") + ticket
								   + "+browsertrackerid:" + browserTrackerId
								   + "+robloxLocale:en_us+gameLocale:en_us+channel:";

		SHELLEXECUTEINFOA authExecutionInfo {sizeof(authExecutionInfo)};
		authExecutionInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		authExecutionInfo.lpVerb = "open";
		authExecutionInfo.lpFile = appAuthCommand.c_str();
		authExecutionInfo.nShow = SW_SHOWNORMAL;

		if (!ShellExecuteExA(&authExecutionInfo)) {
			LOG_ERROR("ShellExecuteExA failed for Roblox app auth. Error: " + std::to_string(GetLastError()));
			return nullptr;
		}

		if (authExecutionInfo.hProcess) {
			WaitForInputIdle(authExecutionInfo.hProcess, 3000);
			CloseHandle(authExecutionInfo.hProcess);
		}

		Sleep(750);

		std::string shareDeepLink
			= "roblox://navigation/share_links?code=" + urlEncode(req.shareCode) + "&type=" + urlEncode(req.shareType);
		SHELLEXECUTEINFOA shareExecutionInfo {sizeof(shareExecutionInfo)};
		shareExecutionInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		shareExecutionInfo.lpVerb = "open";
		shareExecutionInfo.lpFile = shareDeepLink.c_str();
		shareExecutionInfo.nShow = SW_SHOWNORMAL;

		if (!ShellExecuteExA(&shareExecutionInfo)) {
			LOG_ERROR("ShellExecuteExA failed for Roblox share link. Error: " + std::to_string(GetLastError()));
			return nullptr;
		}

		LOG_INFO(
			"Roblox share-link launch started successfully for code: " + req.shareCode + " type: " + req.shareType
		);
		return shareExecutionInfo.hProcess;
	}

	std::string protocolLaunchCommand = buildRobloxPlayerUri(req, ticket);
	if (protocolLaunchCommand.empty()) {
		LOG_ERROR("Failed to build Roblox launch URI");
		return nullptr;
	}

	std::string logMessage = "Attempting to launch Roblox for " + describeLaunchRequest(req);
	LOG_INFO(logMessage);

	std::wstring notificationTitle = L"Launching";
	std::wostringstream notificationMessageStream;
	notificationMessageStream << L"Attempting to launch Roblox.";
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

	LOG_INFO("Roblox process started successfully for " + describeLaunchRequest(req));
	return executionInfo.hProcess;
}

inline HANDLE startRoblox(uint64_t placeId, const std::string &jobId, const Roblox::HBA::AuthCredentials &creds) {
	return startRoblox(makePlaceOrInstanceLaunchRequest(placeId, jobId), creds);
}

/**
 * Launch Roblox sequentially for multiple accounts with HBA support
 * @param req The normalized launch request
 * @param accounts Vector of authentication credentials for each account
 */
inline void
	launchRobloxSequential(const RobloxLaunchRequest &req, const std::vector<Roblox::HBA::AuthCredentials> &accounts) {
	std::string validationError;
	if (!validateLaunchRequest(req, &validationError)) {
		LOG_ERROR("Invalid Roblox launch request: " + validationError);
		return;
	}

	if (!g_multiRobloxEnabled) {
		if (g_killRobloxOnLaunch) { RobloxControl::KillRobloxProcesses(); }
		if (g_clearCacheOnLaunch) { RobloxControl::ClearRobloxCache(); }
	}

	for (const auto &creds : accounts) {
		LOG_INFO(
			"Launching Roblox for account ID: " + std::to_string(creds.accountId)
			+ " Target: " + describeLaunchRequest(req)
		);
		HANDLE proc = startRoblox(req, creds);
		if (proc) {
			WaitForInputIdle(proc, INFINITE);
			CloseHandle(proc);
			LOG_INFO("Roblox launched successfully for account ID: " + std::to_string(creds.accountId));
		} else {
			LOG_ERROR("Failed to start Roblox for account ID: " + std::to_string(creds.accountId));
		}
	}
}

inline void launchRobloxSequential(
	uint64_t placeId,
	const std::string &jobId,
	const std::vector<Roblox::HBA::AuthCredentials> &accounts
) {
	launchRobloxSequential(makePlaceOrInstanceLaunchRequest(placeId, jobId), accounts);
}
