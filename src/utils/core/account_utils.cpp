#include "account_utils.h"
#include "crypto.h"

namespace AccountUtils {

	bool generateHBAKeys(AccountData &account) {
		Crypto::ECKeyPair keyPair = Crypto::generateECKeyPair();
		if (!keyPair.isValid()) {
			LOG_ERROR("Failed to generate HBA keys for account: " + account.username);
			return false;
		}

		account.hbaPrivateKey = keyPair.privateKeyPEM;
		LOG_INFO("Generated HBA keys for account: " + account.username);
		return true;
	}

} // namespace AccountUtils
