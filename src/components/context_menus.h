#pragma once

#include <functional>
#include <imgui.h>
#include <string>

struct StandardJoinMenuParams {
		uint64_t placeId = 0;
		uint64_t universeId = 0;
		std::string jobId;
		bool enableLaunchGame = true;
		bool enableLaunchInstance = true; // only applies if jobId not empty
		std::function<void()> onLaunchGame; // optional
		std::function<void()> onLaunchInstance; // optional
		std::function<void()> onFillGame; // optional
		std::function<void()> onFillInstance; // optional
};

inline void RenderStandardJoinMenu(const StandardJoinMenuParams &p) {
	const bool hasGame = p.placeId != 0;
	const bool hasInstance = !p.jobId.empty();
	const bool hasInstanceContext = hasInstance && p.placeId != 0;

	const ImVec4 kLaunchColor = ImVec4(0.4f, 0.85f, 0.4f, 1.0f);

	ImGui::TextDisabled("Copy");
	if (ImGui::MenuItem("Game ID##Copy", nullptr, false, hasGame)) {
		char buf[64];
		_snprintf_s(buf, _TRUNCATE, "%llu", static_cast<unsigned long long>(p.placeId));
		ImGui::SetClipboardText(buf);
	}
	if (hasInstance && ImGui::MenuItem("Instance ID##Copy")) { ImGui::SetClipboardText(p.jobId.c_str()); }

	if (ImGui::BeginMenu("Launchers##Copy", hasGame)) {
		ImGui::TextDisabled("Game launcher");
		char buf[512];
		if (ImGui::MenuItem("Browser link##Game", nullptr, false, hasGame)) {
			_snprintf_s(
				buf,
				_TRUNCATE,
				"https://www.roblox.com/games/start?placeId=%llu",
				(unsigned long long)p.placeId
			);
			ImGui::SetClipboardText(buf);
		}
		if (ImGui::MenuItem("Deep link##Game", nullptr, false, hasGame)) {
			_snprintf_s(buf, _TRUNCATE, "roblox://placeId=%llu", (unsigned long long)p.placeId);
			ImGui::SetClipboardText(buf);
		}
		if (ImGui::MenuItem("JavaScript##Game", nullptr, false, hasGame)) {
			_snprintf_s(buf, _TRUNCATE, "Roblox.GameLauncher.joinGameInstance(%llu)", (unsigned long long)p.placeId);
			ImGui::SetClipboardText(buf);
		}
		if (ImGui::MenuItem("Roblox Luau##Game", nullptr, false, hasGame)) {
			_snprintf_s(
				buf,
				_TRUNCATE,
				"game:GetService(\"TeleportService\"):Teleport(%llu)",
				(unsigned long long)p.placeId
			);
			ImGui::SetClipboardText(buf);
		}

		if (hasInstanceContext) {
			ImGui::Separator();
			ImGui::TextDisabled("Instance launcher");
			if (ImGui::MenuItem("Browser link##Instance")) {
				std::string link = "https://www.roblox.com/games/start?placeId=" + std::to_string(p.placeId)
								 + "&gameInstanceId=" + p.jobId;
				ImGui::SetClipboardText(link.c_str());
			}
			if (ImGui::MenuItem("Deep link##Instance")) {
				_snprintf_s(
					buf,
					_TRUNCATE,
					"roblox://placeId=%llu&gameInstanceId=%s",
					(unsigned long long)p.placeId,
					p.jobId.c_str()
				);
				ImGui::SetClipboardText(buf);
			}
			if (ImGui::MenuItem("JavaScript##Instance")) {
				std::string js
					= "Roblox.GameLauncher.joinGameInstance(" + std::to_string(p.placeId) + ", \"" + p.jobId + "\")";
				ImGui::SetClipboardText(js.c_str());
			}
			if (ImGui::MenuItem("Roblox Luau##Instance")) {
				std::string luau = "game:GetService(\"TeleportService\"):TeleportToPlaceInstance("
								 + std::to_string(p.placeId) + ", \"" + p.jobId + "\")";
				ImGui::SetClipboardText(luau.c_str());
			}
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();
	ImGui::TextDisabled("Fill \"Join Options\"");
	if (ImGui::MenuItem("Game##Fill", nullptr, false, hasGame)) {
		if (p.onFillGame) { p.onFillGame(); }
	}
	if (hasInstanceContext && ImGui::MenuItem("Instance##Fill")) {
		if (p.onFillInstance) { p.onFillInstance(); }
	}

	ImGui::Separator();
	ImGui::TextDisabled("Launch options");
	ImGui::PushStyleColor(ImGuiCol_Text, kLaunchColor);
	if (ImGui::MenuItem("Game##Launch", nullptr, false, p.enableLaunchGame && hasGame)) {
		if (p.onLaunchGame) { p.onLaunchGame(); }
	}
	ImGui::PopStyleColor();
	if (hasInstanceContext) {
		ImGui::PushStyleColor(ImGuiCol_Text, kLaunchColor);
		if (ImGui::MenuItem("Instance##Launch", nullptr, false, p.enableLaunchInstance)) {
			if (p.onLaunchInstance) { p.onLaunchInstance(); }
		}
		ImGui::PopStyleColor();
	}
}
