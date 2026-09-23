#include "auth/PasswordService.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace {

const QString kLegacyScheme = QStringLiteral("legacy_sha256");
const QString kPbkdf2Scheme = QStringLiteral("pbkdf2_sha256");
constexpr int kSha256Length = 32;
constexpr int kSha256BlockSize = 64;
constexpr int kMaximumAcceptedIterations = 5000000;

} // namespace

Auth::PasswordCredential PasswordService::createCredential(
    const QString &password, QString *errorMessage) const
{
    if (password.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("密码不能为空");
        return {};
    }

    const QByteArray salt = randomSalt();
    if (salt.size() != 16) {
        if (errorMessage) *errorMessage = QStringLiteral("无法生成安全随机盐");
        return {};
    }

    const QByteArray derived = pbkdf2HmacSha256(
        password, salt, Auth::kPbkdf2Iterations, Auth::kPbkdf2KeyLength);
    if (derived.size() != Auth::kPbkdf2KeyLength) {
        if (errorMessage) *errorMessage = QStringLiteral("PBKDF2 派生密钥失败");
        return {};
    }

    Auth::PasswordCredential credential;
    credential.hash = QString::fromLatin1(derived.toHex());
    credential.salt = QString::fromLatin1(salt.toHex());
    credential.scheme = kPbkdf2Scheme;
    credential.iterations = Auth::kPbkdf2Iterations;
    return credential;
}

bool PasswordService::verifyPassword(const QString &password,
                                     const Auth::PasswordCredential &credential,
                                     bool *needsUpgrade,
                                     QString *errorMessage) const
{
    if (needsUpgrade) *needsUpgrade = false;
    if (!credential.isValid()) {
        if (errorMessage) *errorMessage = QStringLiteral("密码凭据不完整");
        return false;
    }

    const QByteArray stored = QByteArray::fromHex(credential.hash.toLatin1());
    const QByteArray salt = QByteArray::fromHex(credential.salt.toLatin1());
    if (stored.isEmpty() || salt.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("密码凭据格式错误");
        return false;
    }

    QByteArray computed;
    if (credential.scheme == kLegacyScheme) {
        if (credential.iterations < 1
            || credential.iterations > kMaximumAcceptedIterations) {
            if (errorMessage) *errorMessage = QStringLiteral("旧密码迭代次数异常");
            return false;
        }
        computed = legacyHash(password, salt, credential.iterations);
        if (needsUpgrade) *needsUpgrade = true;
    } else if (credential.scheme == kPbkdf2Scheme) {
        if (credential.iterations < 1
            || credential.iterations > kMaximumAcceptedIterations) {
            if (errorMessage) *errorMessage = QStringLiteral("PBKDF2 迭代次数异常");
            return false;
        }
        computed = pbkdf2HmacSha256(
            password, salt, credential.iterations, stored.size());
    } else {
        if (errorMessage) *errorMessage = QStringLiteral("不支持的密码算法");
        return false;
    }

    const bool matches = constantTimeEquals(stored, computed);
    if (!matches && needsUpgrade) {
        *needsUpgrade = false;
    }
    return matches;
}

QByteArray PasswordService::randomSalt() const
{
    QByteArray salt(16, '\0');
    for (int offset = 0; offset < salt.size(); offset += 4) {
        const quint32 value = QRandomGenerator::system()->generate();
        salt[offset] = static_cast<char>(value & 0xff);
        salt[offset + 1] = static_cast<char>((value >> 8) & 0xff);
        salt[offset + 2] = static_cast<char>((value >> 16) & 0xff);
        salt[offset + 3] = static_cast<char>((value >> 24) & 0xff);
    }
    return salt;
}

QByteArray PasswordService::legacyHash(const QString &password,
                                       const QByteArray &salt,
                                       int iterations) const
{
    QByteArray digest = salt + password.toUtf8();
    for (int i = 0; i < iterations; ++i) {
        QCryptographicHash hasher(QCryptographicHash::Sha256);
        hasher.addData(digest);
        hasher.addData(salt);
        hasher.addData(QByteArray::number(i));
        digest = hasher.result();
    }
    return digest.left(Auth::kLegacyKeyLength);
}

QByteArray PasswordService::pbkdf2HmacSha256(const QString &password,
                                             const QByteArray &salt,
                                             int iterations,
                                             int keyLength) const
{
    if (iterations < 1 || keyLength < 1) {
        return {};
    }

    const QByteArray key = password.toUtf8();
    QByteArray output;
    output.reserve(keyLength);

    for (quint32 block = 1; output.size() < keyLength; ++block) {
        QByteArray blockIndex(4, '\0');
        blockIndex[0] = static_cast<char>((block >> 24) & 0xff);
        blockIndex[1] = static_cast<char>((block >> 16) & 0xff);
        blockIndex[2] = static_cast<char>((block >> 8) & 0xff);
        blockIndex[3] = static_cast<char>(block & 0xff);

        QByteArray u = hmacSha256(key, salt + blockIndex);
        QByteArray accumulator = u;
        for (int iteration = 1; iteration < iterations; ++iteration) {
            u = hmacSha256(key, u);
            for (int i = 0; i < accumulator.size(); ++i) {
                accumulator[i] = static_cast<char>(accumulator.at(i) ^ u.at(i));
            }
        }
        output.append(accumulator);
    }

    output.truncate(keyLength);
    return output;
}

QByteArray PasswordService::hmacSha256(const QByteArray &key,
                                       const QByteArray &data) const
{
    QByteArray normalizedKey = key;
    if (normalizedKey.size() > kSha256BlockSize) {
        normalizedKey = QCryptographicHash::hash(
            normalizedKey, QCryptographicHash::Sha256);
    }

    QByteArray innerPad(kSha256BlockSize, '\0');
    QByteArray outerPad(kSha256BlockSize, '\0');
    for (int i = 0; i < kSha256BlockSize; ++i) {
        const char keyByte = i < normalizedKey.size() ? normalizedKey.at(i) : '\0';
        innerPad[i] = static_cast<char>(keyByte ^ 0x36);
        outerPad[i] = static_cast<char>(keyByte ^ 0x5c);
    }

    QCryptographicHash inner(QCryptographicHash::Sha256);
    inner.addData(innerPad);
    inner.addData(data);
    const QByteArray innerDigest = inner.result();

    QCryptographicHash outer(QCryptographicHash::Sha256);
    outer.addData(outerPad);
    outer.addData(innerDigest);
    return outer.result().left(kSha256Length);
}

bool PasswordService::constantTimeEquals(const QByteArray &left,
                                         const QByteArray &right) const
{
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (int i = 0; i < left.size(); ++i) {
        diff |= static_cast<unsigned char>(left.at(i))
            ^ static_cast<unsigned char>(right.at(i));
    }
    return diff == 0;
}
