#include "accounts_join_ui.h"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../ui.h"
#include "../../utils/core/account_utils.h"
#include "../data.h"
#include "core/app_state.h"
#include "core/logging.hpp"
#include "core/status.h"
#include "roblox.h"
#include "system/launcher.hpp"
#include "threading.h"
#include "ui/confirm.h"
#include "ui/modal_popup.h"

#ifdef _WIN32
#	include <windows.h>
#endif

#include "system/roblox_control.h"

using namespace ImGui;
using std::all_of;
using std::exception;
using std::find_if;
using std::move;
using std::pair;
using std::string;
using std::to_string;
using std::vector;

static void HelpMarker(const char *desc) {
	TextDisabled("(i)");
	if (IsItemHovered()) {
		BeginTooltip();
		PushTextWrapPos(GetFontSize() * 35.0f);
		TextUnformatted(desc);
		PopTextWrapPos();
		EndTooltip();
	}
}

static const char *join_types_local[] = {
	"Game",
	"Instance",
	"User",
	"Link",
};

enum LocalJoinType {
	JoinType_Game = 0,
	JoinType_Instance = 1,
	JoinType_User = 2,
	JoinType_Link = 3,
};

static bool join_user_follow_enabled = true;

static const char *GetJoinHintLocal(int idx) {
	switch (idx) {
	case JoinType_Game: return "placeId";
	case JoinType_User: return "username or userId (id=000)";
	case JoinType_Link: return "private server or share link";
	default: return "";
	}
}

static string TrimJoinInput(string s) {
	auto l = s.find_first_not_of(" \t\n\r");
	auto r = s.find_last_not_of(" \t\n\r");
	if (l == string::npos) { return {}; }
	return s.substr(l, r - l + 1);
}

static bool IsUnsignedIntegerString(const string &s) {
	if (s.empty()) { return false; }
	return all_of(s.begin(), s.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}

static bool IsJobIdString(const string &s) {
	if (s.empty()) { return false; }
	auto isHex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
	const int parts[5] = {8, 4, 4, 4, 12};
	size_t pos = 0;
	for (int p = 0; p < 5; ++p) {
		for (int k = 0; k < parts[p]; ++k) {
			if (pos >= s.size() || !isHex(s[pos++])) { return false; }
		}
		if (p < 4) {
			if (pos >= s.size() || s[pos++] != '-') { return false; }
		}
	}
	return pos == s.size();
}

void FillJoinOptions(uint64_t placeId, const std::string &jobId) {
	snprintf(join_value_buf, sizeof(join_value_buf), "%llu", (unsigned long long)placeId);
	if (jobId.empty()) {
		join_jobid_buf[0] = '\0';
		join_type_combo_index = 0;
	} else {
		snprintf(join_jobid_buf, sizeof(join_jobid_buf), "%s", jobId.c_str());
		join_type_combo_index = 1;
	}
	g_activeTab = Tab_Accounts;
}

void RenderJoinOptions() {
	Spacing();
	Text("Join Options");
	SameLine();
	HelpMarker(
		"Join Options:\n"
		"- Game: joins a game with its placeId\n"
		"- Instance: joins the instance of a game with its placeId & jobId\n"
		"- User: accepts username or userId (formatted as id=000)\n"
		"\t- Follow checked: lets Roblox resolve the user's live destination\n"
		"\t- Follow unchecked: manually resolves the user to placeId and jobId\n"
		"- Link: accepts supported Roblox game, start, private server, and share links"
	);
	Spacing();
	Combo(" Join Type", &join_type_combo_index, join_types_local, IM_ARRAYSIZE(join_types_local));

	auto tryMatchFavorite = [&]() {
		if (join_type_combo_index != JoinType_Game) { return; }
		std::string input = join_value_buf;
		auto l = input.find_first_not_of(" \t\n\r");
		auto r = input.find_last_not_of(" \t\n\r");
		if (l == std::string::npos) {
			input.clear();
		} else {
			input = input.substr(l, r - l + 1);
		}
		if (input.empty()) { return; }
		bool numeric = std::all_of(input.begin(), input.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
		if (numeric) { return; }

		for (const auto &game : g_favorites) {
			const std::string &name = game.name;
			if (name.size() < input.size()) { continue; }
			bool match = std::equal(input.begin(), input.end(), name.begin(), [](unsigned char a, unsigned char b) {
				return std::tolower(a) == std::tolower(b);
			});
			if (match) {
				snprintf(join_value_buf, sizeof(join_value_buf), "%llu", (unsigned long long)game.placeId);
				break;
			}
		}
	};

	if (join_type_combo_index == JoinType_Instance) {
		float w = GetContentRegionAvail().x;
		float minField = GetFontSize() * 6.25f; // ~100px
		float minWide = GetFontSize() * 26.25f; // ~420px
		if (w < minField) { w = minField; }
		if (w < minWide) { w = minWide; }
		PushItemWidth(w);

		bool placeErr = false;
		{
			std::string s = join_value_buf;
			auto l = s.find_first_not_of(" \t\n\r");
			auto r = s.find_last_not_of(" \t\n\r");
			s = TrimJoinInput(s);
			if (!s.empty() && !IsUnsignedIntegerString(s)) { placeErr = true; }
		}
		if (placeErr) {
			PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
		}
		bool enterPressed = InputTextWithHint(
			"##JoinPlaceId",
			"placeId",
			join_value_buf,
			IM_ARRAYSIZE(join_value_buf),
			ImGuiInputTextFlags_EnterReturnsTrue
		);
		if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) { tryMatchFavorite(); }
		if (placeErr) {
			PopStyleColor();
			PopStyleVar();
		}
		PopItemWidth();
		PushItemWidth(w);
		bool jobErr = false;
		{
			std::string s = join_jobid_buf;
			auto l = s.find_first_not_of(" \t\n\r");
			auto r = s.find_last_not_of(" \t\n\r");
			s = TrimJoinInput(s);
			if (!s.empty() && !IsJobIdString(s)) { jobErr = true; }
		}
		if (jobErr) {
			PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
		}
		InputTextWithHint("##JoinJobId", "jobId", join_jobid_buf, IM_ARRAYSIZE(join_jobid_buf));
		if (jobErr) {
			PopStyleColor();
			PopStyleVar();
		}
		PopItemWidth();
	} else {
		float w = GetContentRegionAvail().x;
		float minField = GetFontSize() * 6.25f; // ~100px
		float minWide = GetFontSize() * 26.25f; // ~420px
		if (w < minField) { w = minField; }
		if (w < minWide) { w = minWide; }
		PushItemWidth(w);
		bool showError = false;
		if (join_type_combo_index == JoinType_User) {
			std::string preview = join_value_buf;
			UserSpecifier tmp {};
			std::string trimmed = TrimJoinInput(preview);
			if (!trimmed.empty() && !parseUserSpecifier(trimmed, tmp)) { showError = true; }
		} else if (join_type_combo_index == JoinType_Game) {
			std::string s = join_value_buf;
			s = TrimJoinInput(s);
			if (!s.empty() && !IsUnsignedIntegerString(s)) { showError = true; }
		} else if (join_type_combo_index == JoinType_Link) {
			std::string s = TrimJoinInput(join_value_buf);
			RobloxLaunchRequest tmp {};
			if (!s.empty() && !parseRobloxLaunchLink(s, tmp)) { showError = true; }
		}
		if (showError) {
			PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
		}
		bool enterPressed = InputTextWithHint(
			"##JoinValue",
			GetJoinHintLocal(join_type_combo_index),
			join_value_buf,
			IM_ARRAYSIZE(join_value_buf),
			ImGuiInputTextFlags_EnterReturnsTrue
		);
		if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) { tryMatchFavorite(); }
		if (showError) {
			PopStyleColor();
			PopStyleVar();
		}
		PopItemWidth();

		if (join_type_combo_index == JoinType_User) { Checkbox("Follow", &join_user_follow_enabled); }
	}

	Separator();
	bool allowJoin = true;
	if (join_type_combo_index == JoinType_User) {
		std::string s = TrimJoinInput(join_value_buf);
		UserSpecifier tmp {};
		if (s.empty() || !parseUserSpecifier(s, tmp)) { allowJoin = false; }
	} else if (join_type_combo_index == JoinType_Instance) {
		std::string pid = TrimJoinInput(join_value_buf);
		std::string jid = TrimJoinInput(join_jobid_buf);
		if (!IsUnsignedIntegerString(pid) || !IsJobIdString(jid)) { allowJoin = false; }
	} else if (join_type_combo_index == JoinType_Game) {
		std::string pid = TrimJoinInput(join_value_buf);
		if (!IsUnsignedIntegerString(pid)) { allowJoin = false; }
	} else if (join_type_combo_index == JoinType_Link) {
		std::string link = TrimJoinInput(join_value_buf);
		RobloxLaunchRequest tmp {};
		if (link.empty() || !parseRobloxLaunchLink(link, tmp)) { allowJoin = false; }
	}
	BeginDisabled(!allowJoin);
	if (Button(" \xEF\x8B\xB6  Launch ")) {
		auto doJoin = [&]() {
			if (g_selectedAccountIds.empty()) {
				ModalPopup::Add("Select an account first.");
				return;
			}

			std::vector<Roblox::HBA::AuthCredentials> accounts;
			for (int id : g_selectedAccountIds) {
				auto it = std::find_if(g_accounts.begin(), g_accounts.end(), [id](auto &a) { return a.id == id; });
				if (it != g_accounts.end() && AccountFilters::IsAccountUsable(*it)) {
					accounts.push_back(AccountUtils::credentialsFromAccount(*it));
				}
			}
			if (accounts.empty()) { return; }

			int joinType = join_type_combo_index;
			bool followUser = join_user_follow_enabled;
			string valueInput = TrimJoinInput(join_value_buf);
			string jobInput = TrimJoinInput(join_jobid_buf);

			Threading::newThread([joinType, followUser, valueInput, jobInput, accounts]() {
				try {
					RobloxLaunchRequest req {};

					if (joinType == JoinType_User) {
						UserSpecifier spec {};
						if (!parseUserSpecifier(valueInput, spec)) {
							Status::Error("Enter username or userId (id=000)");
							return;
						}
						uint64_t uid = spec.isId ? spec.id : Roblox::getUserIdFromUsername(spec.username);

						if (followUser) {
							req.kind = RobloxJoinKind::UserFollow;
							req.userId = uid;
							req.username = spec.username;
							launchRobloxSequential(req, accounts);
							return;
						}

						auto pres = Roblox::getPresences({uid}, accounts.front().toAuthConfig());
						auto it = pres.find(uid);
						if (it == pres.end() || it->second.presence != "InGame" || it->second.placeId == 0
							|| it->second.jobId.empty()) {
							Status::Error("User is not joinable");
							return;
						}

						req.kind = RobloxJoinKind::UserResolved;
						req.placeId = it->second.placeId;
						req.jobId = it->second.jobId;
						req.userId = uid;
						req.username = spec.username;
						launchRobloxSequential(req, accounts);
						return;
					}

					if (joinType == JoinType_Link) {
						std::string error;
						if (!resolveRobloxLaunchLink(valueInput, accounts.front(), req, &error)) {
							Status::Error(error.empty() ? "Unsupported Roblox link" : error);
							return;
						}
						launchRobloxSequential(req, accounts);
						return;
					}

					uint64_t placeId = std::stoull(valueInput);
					req = joinType == JoinType_Instance ? makeInstanceLaunchRequest(placeId, jobInput)
														: makePlaceLaunchRequest(placeId);
					launchRobloxSequential(req, accounts);
				} catch (const std::exception &e) {
					LOG_ERROR(std::string("Join failed: ") + e.what());
					Status::Error("Failed to launch Roblox");
				}
			});
		};
#ifdef _WIN32
		if (!g_multiRobloxEnabled && RobloxControl::IsRobloxRunning()) {
			ConfirmPopup::Add("Roblox is already running. Launch anyway?", doJoin);
		} else {
			doJoin();
		}
#else
		doJoin();
#endif
	}
	EndDisabled();

	SameLine(0, 10);
	if (Button(" \xEF\x87\xB8  Clear Join Options ")) {
		join_value_buf[0] = '\0';
		join_jobid_buf[0] = '\0';
		join_type_combo_index = 0;
		join_user_follow_enabled = true;
	}
}
