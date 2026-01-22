#pragma once

#include "../../components/data.h"
#include "logging.hpp"
#include "network/roblox/hba.h"
#include <optional>
#include <string_view>

namespace AccountFilters {

	inline bool IsBannedLikeStatus(std::string_view s) { return s == "Banned" || s == "Warned" || s == "Terminated"; }

	inline bool IsAccountUsable(const AccountData &a) { return !IsBannedLikeStatus(a.status); }

} // namespace AccountFilters

namespace AccountUtils {

	/**
	 * Generate a new HBA key pair for an account
	 * @param account The account to generate keys for
	 * @return true if keys were generated successfully
	 *
	 * Note: Implementation in account_utils.cpp to avoid OpenSSL/wincrypt header conflicts
	 */
	bool generateHBAKeys(AccountData &account);

	/**
	 * Ensure an account has HBA keys, generating them if missing
	 * @param account The account to check/update
	 * @return true if account has valid HBA keys (existing or newly generated)
	 */
	inline bool ensureHBAKeys(AccountData &account) {
		if (!account.hbaPrivateKey.empty()) {
			return true; // Already has keys
		}

		return generateHBAKeys(account);
	}

	/**
	 * Check if an account has valid HBA configuration
	 * @param account The account to check
	 * @return true if HBA is enabled and keys are present
	 */
	inline bool hasValidHBA(const AccountData &account) { return account.hbaEnabled && !account.hbaPrivateKey.empty(); }

	/**
	 * Migrate all accounts to have HBA keys (for upgrades)
	 * @param accounts Vector of accounts to migrate
	 * @return Number of accounts that had keys generated
	 */
	inline int migrateAccountsToHBA(std::vector<AccountData> &accounts) {
		int migrated = 0;
		for (auto &account : accounts) {
			if (account.hbaPrivateKey.empty() && account.hbaEnabled) {
				if (generateHBAKeys(account)) { migrated++; }
			}
		}
		if (migrated > 0) { LOG_INFO("Migrated " + std::to_string(migrated) + " accounts to HBA"); }
		return migrated;
	}

	/**
	 * Create AuthCredentials from an AccountData struct
	 * @param account The account to extract credentials from
	 * @return AuthCredentials struct with the account's auth info
	 */
	inline Roblox::HBA::AuthCredentials credentialsFromAccount(const AccountData &account) {
		return Roblox::HBA::AuthCredentials {
			.accountId = account.id,
			.cookie = account.cookie,
			.hbaPrivateKey = account.hbaPrivateKey,
			.hbaEnabled = account.hbaEnabled
		};
	}

	/**
	 * Get auth credentials for the default account (g_defaultAccountId or first usable)
	 * @return AuthCredentials if a usable account exists, nullopt otherwise
	 */
	inline std::optional<Roblox::HBA::AuthCredentials> getDefaultAuthCredentials() {
		// First try the default account if set
		if (g_defaultAccountId > 0) {
			for (const auto &account : g_accounts) {
				if (account.id == g_defaultAccountId && AccountFilters::IsAccountUsable(account)) {
					return credentialsFromAccount(account);
				}
			}
		}

		// Fall back to first usable account
		for (const auto &account : g_accounts) {
			if (AccountFilters::IsAccountUsable(account)) { return credentialsFromAccount(account); }
		}

		return std::nullopt;
	}

	/**
	 * Get auth credentials for a specific account ID, or fallback to default
	 * @param accountId The account ID to look for
	 * @return AuthCredentials if found and usable, or default account credentials
	 */
	inline std::optional<Roblox::HBA::AuthCredentials> getAuthCredentials(int accountId) {
		for (const auto &account : g_accounts) {
			if (account.id == accountId && AccountFilters::IsAccountUsable(account)) {
				return credentialsFromAccount(account);
			}
		}

		// Fall back to default
		return getDefaultAuthCredentials();
	}

	/**
	 * Get auth credentials from the first selected account that is usable
	 * @return AuthCredentials if a usable selected account exists, nullopt otherwise
	 */
	inline std::optional<Roblox::HBA::AuthCredentials> getSelectedAuthCredentials() {
		for (int id : g_selectedAccountIds) {
			for (const auto &account : g_accounts) {
				if (account.id == id && AccountFilters::IsAccountUsable(account)) {
					return credentialsFromAccount(account);
				}
			}
		}

		// Fall back to default if no selected accounts are usable
		return getDefaultAuthCredentials();
	}

} // namespace AccountUtils
