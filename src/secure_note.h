#pragma once

#include <QByteArray>
#include <QString>

namespace SecureNote {
bool encrypt(const QByteArray &plaintext, const QString &password, QByteArray *envelope);
bool decrypt(const QByteArray &envelope, const QString &password, QByteArray *plaintext);
}