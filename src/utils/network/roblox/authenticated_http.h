#pragma once

#include "core/logging.hpp"
#include "hba.h"
#include "hba_client.h"
#include "http.hpp"
#include <map>
#include <string>

namespace Roblox { namespace AuthenticatedHttp {

	inline std::map<std::string, std::string> buildAuthHeaders(
		const HBA::AuthConfig &config,
		const std::string &url,
		const std::string &method,
		const std::string &body,
		const std::map<std::string, std::string> &additionalHeaders = {},
		const std::string &csrfToken = ""
	) {
		std::map<std::string, std::string> headers;

		// Add cookie
		if (!config.cookie.empty()) {
			std::string cookieHeader = ".ROBLOSECURITY=" + config.cookie + "; RBXEventTrackerV2=browserid=1";
			headers["Cookie"] = cookieHeader;
		}

		// Add additional headers
		for (const auto &[key, value] : additionalHeaders) { headers[key] = value; }

		// Add CSRF + origin/referer if provided
		if (!csrfToken.empty()) {
			headers["X-CSRF-TOKEN"] = csrfToken;
			headers["Origin"] = "https://www.roblox.com";
			headers["Referer"] = "https://www.roblox.com/";
		}

		// Generate and add BAT header if applicable
		if (config.hasHBA()) {
			auto batHeaders = HBA::getClient().generateBATHeaders(config, url, method, body);
			for (const auto &[key, value] : batHeaders) { headers[key] = value; }
		}

		return headers;
	}

	/**
	 * Make an authenticated GET request with optional HBA support
	 * @param url The request URL
	 * @param config HBA authentication configuration
	 * @param additionalHeaders Additional headers to include
	 * @param params URL parameters
	 * @return Response
	 */
	inline HttpClient::Response
		get(const std::string &url,
			const HBA::AuthConfig &config,
			const std::map<std::string, std::string> &additionalHeaders = {},
			cpr::Parameters params = {}) {
		auto headers = buildAuthHeaders(config, url, "GET", "", additionalHeaders);
		return HttpClient::getWithHeaders(url, headers, params);
	}

	/**
	 * Make an authenticated POST request with optional HBA support
	 * @param url The request URL
	 * @param config HBA authentication configuration
	 * @param jsonBody JSON body string
	 * @param additionalHeaders Additional headers to include
	 * @return Response
	 */
	inline HttpClient::Response post(
		const std::string &url,
		const HBA::AuthConfig &config,
		const std::string &jsonBody = "",
		const std::map<std::string, std::string> &additionalHeaders = {}
	) {
		auto headers = buildAuthHeaders(config, url, "POST", jsonBody, additionalHeaders);
		return HttpClient::postWithHeaders(url, headers, jsonBody);
	}

	/**
	 * Fetch CSRF token from Roblox endpoint
	 * @param url The URL to fetch CSRF from (typically same as request URL)
	 * @param config Authentication configuration
	 * @return CSRF token string, or empty on failure
	 */
	inline std::string fetchCSRFToken(const std::string &url, const HBA::AuthConfig &config) {
		auto response = post(url, config);
		auto it = response.headers.find("x-csrf-token");
		if (it != response.headers.end()) { return it->second; }
		return "";
	}

	/**
	 * Make an authenticated POST request with automatic CSRF token handling and HBA support
	 * @param url The request URL
	 * @param config HBA authentication configuration
	 * @param jsonBody JSON body string
	 * @param additionalHeaders Additional headers to include
	 * @return Response
	 */
	inline HttpClient::Response postWithCSRF(
		const std::string &url,
		const HBA::AuthConfig &config,
		const std::string &jsonBody = "",
		const std::map<std::string, std::string> &additionalHeaders = {}
	) {
		// First, fetch CSRF token
		std::string csrfToken = fetchCSRFToken(url, config);
		if (csrfToken.empty()) {
			LOG_ERROR("Failed to fetch CSRF token for: " + url);
			return {403, "Failed to fetch CSRF token", {}};
		}

		auto headers = buildAuthHeaders(config, url, "POST", jsonBody, additionalHeaders, csrfToken);
		return HttpClient::postWithHeaders(url, headers, jsonBody);
	}

	/**
	 * Make an authenticated POST request that retries with CSRF when required.
	 * If the initial request returns x-csrf-token, it will automatically retry.
	 */
	inline HttpClient::Response postWithAutoCSRF(
		const std::string &url,
		const HBA::AuthConfig &config,
		const std::string &jsonBody = "",
		const std::map<std::string, std::string> &additionalHeaders = {}
	) {
		auto resp = post(url, config, jsonBody, additionalHeaders);
		auto it = resp.headers.find("x-csrf-token");
		if (resp.status_code == 403 && it != resp.headers.end()) {
			auto headers = buildAuthHeaders(config, url, "POST", jsonBody, additionalHeaders, it->second);
			return HttpClient::postWithHeaders(url, headers, jsonBody);
		}
		return resp;
	}

	/**
	 * Create an AuthConfig from account credentials
	 * @param cookie The .ROBLOSECURITY cookie
	 * @param hbaPrivateKey PEM-encoded private key (empty to disable HBA)
	 * @param hbaEnabled Whether HBA is enabled for this account
	 * @return AuthConfig structure
	 */
	inline HBA::AuthConfig
		makeConfig(const std::string &cookie, const std::string &hbaPrivateKey = "", bool hbaEnabled = true) {
		return HBA::AuthConfig {
			.cookie = cookie,
			.hbaPrivateKey = hbaPrivateKey,
			.hbaEnabled = hbaEnabled && !hbaPrivateKey.empty()
		};
	}

}} // namespace Roblox::AuthenticatedHttp
