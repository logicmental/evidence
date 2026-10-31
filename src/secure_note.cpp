#include "secure_note.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <climits>

namespace {
constexpr int SaltSize = 16;
constexpr int NonceSize = 12;
constexpr int TagSize = 16;
constexpr int KeySize = 32;
constexpr int Iterations = 310000;
constexpr char Magic[] = "EVNOTE01";
constexpr int HeaderSize = 8 + SaltSize + NonceSize + TagSize;

bool deriveKey(const QString &password, const QByteArray &salt, QByteArray *key)
{
    QByteArray passwordBytes = password.toUtf8();
    key->resize(KeySize);
    const bool success = PKCS5_PBKDF2_HMAC(passwordBytes.constData(), passwordBytes.size(),
                                           reinterpret_cast<const unsigned char *>(salt.constData()), salt.size(),
                                           Iterations, EVP_sha256(), KeySize,
                                           reinterpret_cast<unsigned char *>(key->data())) == 1;
    OPENSSL_cleanse(passwordBytes.data(), passwordBytes.size());
    if (!success)
        OPENSSL_cleanse(key->data(), key->size());
    return success;
}
}

bool SecureNote::encrypt(const QByteArray &plaintext, const QString &password, QByteArray *envelope)
{
    if (!envelope || password.isEmpty() || plaintext.size() > INT_MAX - EVP_MAX_BLOCK_LENGTH)
        return false;

    QByteArray salt(SaltSize, '\0');
    QByteArray nonce(NonceSize, '\0');
    QByteArray key;
    if (RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), salt.size()) != 1 ||
        RAND_bytes(reinterpret_cast<unsigned char *>(nonce.data()), nonce.size()) != 1 ||
        !deriveKey(password, salt, &key))
        return false;

    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context) {
        OPENSSL_cleanse(key.data(), key.size());
        return false;
    }

    QByteArray ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    QByteArray tag(TagSize, '\0');
    int written = 0;
    int finalWritten = 0;
    const bool success =
        EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1 &&
        EVP_EncryptUpdate(context, reinterpret_cast<unsigned char *>(ciphertext.data()), &written,
                          reinterpret_cast<const unsigned char *>(plaintext.constData()),
                          plaintext.size()) == 1 &&
        EVP_EncryptFinal_ex(context,
                            reinterpret_cast<unsigned char *>(ciphertext.data()) + written,
                            &finalWritten) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(key.data(), key.size());
    if (!success)
        return false;

    ciphertext.resize(written + finalWritten);
    *envelope = QByteArray(Magic, 8) + salt + nonce + tag + ciphertext;
    return true;
}

bool SecureNote::decrypt(const QByteArray &envelope, const QString &password, QByteArray *plaintext)
{
    if (!plaintext || password.isEmpty() || envelope.size() < HeaderSize ||
        !envelope.startsWith(QByteArray(Magic, 8)) ||
        envelope.size() - HeaderSize > INT_MAX - EVP_MAX_BLOCK_LENGTH)
        return false;

    const QByteArray salt = envelope.mid(8, SaltSize);
    const QByteArray nonce = envelope.mid(8 + SaltSize, NonceSize);
    QByteArray tag = envelope.mid(8 + SaltSize + NonceSize, TagSize);
    const QByteArray ciphertext = envelope.mid(HeaderSize);
    QByteArray key;
    if (!deriveKey(password, salt, &key))
        return false;

    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context) {
        OPENSSL_cleanse(key.data(), key.size());
        return false;
    }

    QByteArray decrypted(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int written = 0;
    int finalWritten = 0;
    const bool initialized =
        EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1 &&
        EVP_DecryptUpdate(context, reinterpret_cast<unsigned char *>(decrypted.data()), &written,
                          reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                          ciphertext.size()) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, tag.size(), tag.data()) == 1;
    const bool authenticated = initialized &&
        EVP_DecryptFinal_ex(context,
                            reinterpret_cast<unsigned char *>(decrypted.data()) + written,
                            &finalWritten) == 1;
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(key.data(), key.size());
    if (!authenticated) {
        OPENSSL_cleanse(decrypted.data(), decrypted.size());
        return false;
    }

    decrypted.resize(written + finalWritten);
    *plaintext = decrypted;
    return true;
}