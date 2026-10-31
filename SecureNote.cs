using System;
using System.Security.Cryptography;
using System.Text;

namespace Evidence;

internal static class SecureNote
{
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("EVNOTE01");
    private const int SaltSize = 16;
    private const int NonceSize = 12;
    private const int TagSize = 16;
    private const int KeySize = 32;
    private const int Iterations = 310_000;
    private const int HeaderSize = 8 + SaltSize + NonceSize + TagSize;

    public static byte[] Encrypt(ReadOnlySpan<byte> plaintext, string password)
    {
        ArgumentException.ThrowIfNullOrEmpty(password);
        var salt = RandomNumberGenerator.GetBytes(SaltSize);
        var nonce = RandomNumberGenerator.GetBytes(NonceSize);
        var ciphertext = new byte[plaintext.Length];
        var tag = new byte[TagSize];
        var key = DeriveKey(password, salt);
        try
        {
            using var aes = new AesGcm(key, TagSize);
            aes.Encrypt(nonce, plaintext, ciphertext, tag);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(key);
        }

        var envelope = new byte[HeaderSize + ciphertext.Length];
        Magic.CopyTo(envelope, 0);
        salt.CopyTo(envelope, 8);
        nonce.CopyTo(envelope, 8 + SaltSize);
        tag.CopyTo(envelope, 8 + SaltSize + NonceSize);
        ciphertext.CopyTo(envelope, HeaderSize);
        return envelope;
    }

    public static bool TryDecrypt(ReadOnlySpan<byte> envelope, string password, out byte[] plaintext)
    {
        plaintext = [];
        if (string.IsNullOrEmpty(password) || envelope.Length < HeaderSize ||
            !envelope[..Magic.Length].SequenceEqual(Magic))
            return false;

        var salt = envelope.Slice(8, SaltSize);
        var nonce = envelope.Slice(8 + SaltSize, NonceSize);
        var tag = envelope.Slice(8 + SaltSize + NonceSize, TagSize);
        var ciphertext = envelope[HeaderSize..];
        var key = DeriveKey(password, salt);
        var decrypted = new byte[ciphertext.Length];
        try
        {
            using var aes = new AesGcm(key, TagSize);
            aes.Decrypt(nonce, ciphertext, tag, decrypted);
            plaintext = decrypted;
            return true;
        }
        catch (CryptographicException)
        {
            CryptographicOperations.ZeroMemory(decrypted);
            return false;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(key);
        }
    }

    private static byte[] DeriveKey(string password, ReadOnlySpan<byte> salt) =>
        Rfc2898DeriveBytes.Pbkdf2(Encoding.UTF8.GetBytes(password), salt, Iterations,
            HashAlgorithmName.SHA256, KeySize);
}
