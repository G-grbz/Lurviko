#pragma once
#include <QString>

namespace KWallet { class Wallet; }

// Moves legacy trees without rewriting encrypted files or discarding collisions.
bool migrateLegacyUserData(const QString &configRoot, const QString &dataRoot,
                           const QString &cacheRoot, QString *error);
bool selectLurvikoWalletFolder(KWallet::Wallet *wallet);
QString relocatedAppPath(const QString &pathOrUrl);
