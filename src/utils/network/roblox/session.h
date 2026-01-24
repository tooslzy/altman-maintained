#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "auth.h"
#include "authenticated_http.h"
#include "core/logging.hpp"
#include "hba.h"
#include "http.hpp"
#include "status.h"

namespace Roblox {
	static std::string getPresence(const std::string &cookie, uint64_t userId) {
		BanCheckResult status = cachedBanStatus(cookie);
		if (status == BanCheckResult::InvalidCookie) { return "InvalidCookie"; }
		if (!canUseCookie(cookie)) { return "Banned"; }

		LOG_INFO("Fetching user presence");
		nlohmann::json payload = {
			{"userIds", {userId}}
		};
		HttpClient::Response response = HttpClient::post(
			"https://presence.roproxy.com/v1/presence/users",
			{
				{"Cookie", ".ROBLOSECURITY=" + cookie}
		},
			payload.dump()
		);
		if (response.status_code < 200 || response.status_code >= 300) {
			LOG_ERROR("Presence lookup failed: HTTP " + std::to_string(response.status_code));

			if (response.status_code == 403) { return "Banned"; }

			return "Offline";
		}

		OutputDebugStringA(("Raw response: " + response.text).c_str());
		LOG_INFO("Raw response body: " + response.text);

		auto json = HttpClient::decode(response);

		OutputDebugStringA(("Parsed JSON: " + json.dump()).c_str());
		LOG_INFO("Parsed JSON: " + json.dump());

		if (json.contains("userPresences") && json["userPresences"].is_array() && !json["userPresences"].empty()) {
			const auto &jsonData = json["userPresences"][0];
			int typeInt = jsonData.value("userPresenceType", 0);
			std::string presenceStatus = presenceTypeToString(typeInt);

			LOG_INFO("Got user presence for " + std::to_string(userId));
			return presenceStatus;
		}
		return "Offline";
	}

	/**
	 * Get user presence with HBA support
	 */
	static std::string getPresence(const HBA::AuthConfig &config, uint64_t userId) {
		BanCheckResult status = cachedBanStatus(config.cookie);
		if (status == BanCheckResult::InvalidCookie) { return "InvalidCookie"; }
		if (!canUseCookie(config.cookie)) { return "Banned"; }

		LOG_INFO("Fetching user presence (HBA-enabled)");
		nlohmann::json payload = {
			{"userIds", {userId}}
		};
		std::string payloadStr = payload.dump();
		HttpClient::Response response
			= AuthenticatedHttp::post("https://presence.roblox.com/v1/presence/users", config, payloadStr);
		if (response.status_code < 200 || response.status_code >= 300) {
			LOG_ERROR("Presence lookup failed: HTTP " + std::to_string(response.status_code));
			if (response.status_code == 403) { return "Banned"; }
			return "Offline";
		}

		auto json = HttpClient::decode(response);
		if (json.contains("userPresences") && json["userPresences"].is_array() && !json["userPresences"].empty()) {
			const auto &jsonData = json["userPresences"][0];
			int typeInt = jsonData.value("userPresenceType", 0);
			std::string presenceStatus = presenceTypeToString(typeInt);
			LOG_INFO("Got user presence for " + std::to_string(userId));
			return presenceStatus;
		}
		return "Offline";
	}

	struct VoiceSettings {
			std::string status;
			time_t bannedUntil = 0;
	};

	static VoiceSettings getVoiceChatStatus(const std::string &cookie) {
		// First check if account is banned/warned/terminated
		BanCheckResult status = cachedBanStatus(cookie);
		if (status == BanCheckResult::Banned || status == BanCheckResult::Warned || status == BanCheckResult::Terminated
			|| status == BanCheckResult::InvalidCookie) {
			return {"N/A", 0};
		}

		LOG_INFO("Fetching voice chat settings");
		auto resp = HttpClient::get(
			"https://voice.roblox.com/v1/settings",
			{
				{"Cookie", ".ROBLOSECURITY=" + cookie}
		}
		);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_INFO("Failed to fetch voice settings: HTTP " + std::to_string(resp.status_code));
			// Any API error for a non-banned account should return Unknown
			return {"Unknown", 0};
		}

		auto j = HttpClient::decode(resp);
		bool banned = j.value("isBanned", false);
		bool enabled = j.value("isVoiceEnabled", false);
		bool eligible = j.value("isUserEligible", false);
		bool opted = j.value("isUserOptIn", false);
		time_t bannedUntil = 0;
		if (j.contains("bannedUntil") && !j["bannedUntil"].is_null()) {
			if (j["bannedUntil"].contains("Seconds")) { bannedUntil = j["bannedUntil"]["Seconds"].get<int64_t>(); }
		}

		if (banned) { return {"Banned", bannedUntil}; }
		if (enabled || opted) { return {"Enabled", 0}; }
		if (eligible) { return {"Disabled", 0}; }

		return {"Disabled", 0};
	}

	/**
	 * Get voice chat status with HBA support
	 */
	static VoiceSettings getVoiceChatStatus(const HBA::AuthConfig &config) {
		// First check if account is banned/warned/terminated
		BanCheckResult status = cachedBanStatus(config.cookie);
		if (status == BanCheckResult::Banned || status == BanCheckResult::Warned || status == BanCheckResult::Terminated
			|| status == BanCheckResult::InvalidCookie) {
			return {"N/A", 0};
		}

		LOG_INFO("Fetching voice chat settings (HBA-enabled)");
		auto resp = AuthenticatedHttp::get("https://voice.roblox.com/v1/settings", config);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_INFO("Failed to fetch voice settings: HTTP " + std::to_string(resp.status_code));
			return {"Unknown", 0};
		}

		auto j = HttpClient::decode(resp);
		bool banned = j.value("isBanned", false);
		bool enabled = j.value("isVoiceEnabled", false);
		bool eligible = j.value("isUserEligible", false);
		bool opted = j.value("isUserOptIn", false);
		time_t bannedUntil = 0;
		if (j.contains("bannedUntil") && !j["bannedUntil"].is_null()) {
			if (j["bannedUntil"].contains("Seconds")) { bannedUntil = j["bannedUntil"]["Seconds"].get<int64_t>(); }
		}

		if (banned) { return {"Banned", bannedUntil}; }
		if (enabled || opted) { return {"Enabled", 0}; }
		if (eligible) { return {"Disabled", 0}; }

		return {"Disabled", 0};
	}

	/**
	 * Convert age group translation key to display string
	 */
	static std::string ageGroupKeyToDisplay(const std::string &key) {
		if (key == "Label.AgeGroupUnder9") { return "<9"; }
		if (key == "Label.AgeGroup9To12") { return "9-12"; }
		if (key == "Label.AgeGroup13To15") { return "13-15"; }
		if (key == "Label.AgeGroup16To17") { return "16-17"; }
		if (key == "Label.AgeGroup18To20") { return "18-20"; }
		if (key == "Label.AgeGroupOver21") { return "21+"; }
		return "Unknown";
	}

	struct AgeGroupResult {
			std::string ageGroup; // Display string like "<9", "9-12", etc.
			bool success = false;
	};

	/**
	 * Get account age group from Roblox API
	 */
	static AgeGroupResult getAgeGroup(const std::string &cookie) {
		// First check if account is banned/warned/terminated
		BanCheckResult status = cachedBanStatus(cookie);
		if (status == BanCheckResult::Banned || status == BanCheckResult::Warned || status == BanCheckResult::Terminated
			|| status == BanCheckResult::InvalidCookie) {
			return {"N/A", false};
		}

		LOG_INFO("Fetching account age group");
		auto resp = HttpClient::get(
			"https://apis.roblox.com/user-settings-api/v1/account-insights/age-group",
			{
				{"Cookie", ".ROBLOSECURITY=" + cookie}
		}
		);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_INFO("Failed to fetch age group: HTTP " + std::to_string(resp.status_code));
			return {"Unknown", false};
		}

		try {
			auto j = HttpClient::decode(resp);
			std::string translationKey = j.value("ageGroupTranslationKey", "");
			if (translationKey.empty()) {
				LOG_INFO("Age group translation key is empty");
				return {"Unknown", false};
			}
			return {ageGroupKeyToDisplay(translationKey), true};
		} catch (const std::exception &e) {
			LOG_ERROR("Failed to parse age group response: " + std::string(e.what()));
			return {"Unknown", false};
		}
	}

	/**
	 * Get account age group with HBA support
	 */
	static AgeGroupResult getAgeGroup(const HBA::AuthConfig &config) {
		// First check if account is banned/warned/terminated
		BanCheckResult status = cachedBanStatus(config.cookie);
		if (status == BanCheckResult::Banned || status == BanCheckResult::Warned || status == BanCheckResult::Terminated
			|| status == BanCheckResult::InvalidCookie) {
			return {"N/A", false};
		}

		LOG_INFO("Fetching account age group (HBA-enabled)");
		auto resp
			= AuthenticatedHttp::get("https://apis.roblox.com/user-settings-api/v1/account-insights/age-group", config);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_INFO("Failed to fetch age group: HTTP " + std::to_string(resp.status_code));
			return {"Unknown", false};
		}

		try {
			auto j = HttpClient::decode(resp);
			std::string translationKey = j.value("ageGroupTranslationKey", "");
			if (translationKey.empty()) {
				LOG_INFO("Age group translation key is empty");
				return {"Unknown", false};
			}
			return {ageGroupKeyToDisplay(translationKey), true};
		} catch (const std::exception &e) {
			LOG_ERROR("Failed to parse age group response: " + std::string(e.what()));
			return {"Unknown", false};
		}
	}

	struct PresenceData {
			std::string presence;
			std::string lastLocation;
			uint64_t placeId = 0;
			std::string jobId;
	};

	static std::unordered_map<uint64_t, PresenceData>
		getPresences(const std::vector<uint64_t> &userIds, const std::string &cookie) {
		if (!canUseCookie(cookie)) { return {}; }

		nlohmann::json payload = {
			{"userIds", userIds}
		};

		auto resp = HttpClient::post(
			"https://presence.roblox.com/v1/presence/users",
			{
				{"Cookie", ".ROBLOSECURITY=" + cookie}
		},
			payload.dump()
		);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_ERROR("Batch presence failed: HTTP " + std::to_string(resp.status_code));
			return {};
		}

		nlohmann::json j = HttpClient::decode(resp);
		std::unordered_map<uint64_t, PresenceData> out;

		if (j.contains("userPresences") && j["userPresences"].is_array()) {
			for (auto &up : j["userPresences"]) {
				PresenceData d;
				d.presence = presenceTypeToString(up.value("userPresenceType", 0));
				d.lastLocation = up.value("lastLocation", "");
				if (up.contains("placeId") && up["placeId"].is_number_unsigned()) {
					d.placeId = up["placeId"].get<uint64_t>();
				}
				// API uses field name 'gameId' for jobId; we store it as jobId internally
				if (up.contains("gameId") && !up["gameId"].is_null()) { d.jobId = up["gameId"].get<std::string>(); }
				if (up.contains("userId")) { out[up["userId"].get<uint64_t>()] = std::move(d); }
			}
		}
		return out;
	}

	/**
	 * Get presences for multiple users with HBA support
	 */
	static std::unordered_map<uint64_t, PresenceData>
		getPresences(const std::vector<uint64_t> &userIds, const HBA::AuthConfig &config) {
		if (!canUseCookie(config.cookie)) { return {}; }

		nlohmann::json payload = {
			{"userIds", userIds}
		};
		std::string payloadStr = payload.dump();

		auto resp = AuthenticatedHttp::post("https://presence.roblox.com/v1/presence/users", config, payloadStr);

		if (resp.status_code < 200 || resp.status_code >= 300) {
			LOG_ERROR("Batch presence failed: HTTP " + std::to_string(resp.status_code));
			return {};
		}

		nlohmann::json j = HttpClient::decode(resp);
		std::unordered_map<uint64_t, PresenceData> out;

		if (j.contains("userPresences") && j["userPresences"].is_array()) {
			for (auto &up : j["userPresences"]) {
				PresenceData d;
				d.presence = presenceTypeToString(up.value("userPresenceType", 0));
				d.lastLocation = up.value("lastLocation", "");
				if (up.contains("placeId") && up["placeId"].is_number_unsigned()) {
					d.placeId = up["placeId"].get<uint64_t>();
				}
				if (up.contains("gameId") && !up["gameId"].is_null()) { d.jobId = up["gameId"].get<std::string>(); }
				if (up.contains("userId")) { out[up["userId"].get<uint64_t>()] = std::move(d); }
			}
		}
		return out;
	}
} // namespace Roblox
