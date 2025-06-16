# 🔐 FF1 Format-Preserving Encryption (FPE)

This project implements the **FF1 Format-Preserving Encryption (FPE)** algorithm, based on the NIST Special Publication 800-38G. It supports encryption and decryption of numeric strings using **AES** or **ChaCha20** as the underlying cipher.

## 📖 What is FF1?

FF1 is a format-preserving encryption algorithm that encrypts data without altering its format. For example, a 10-digit number will be encrypted into another 10-digit number. This is particularly useful for applications like encrypting credit card numbers, phone numbers, or other structured identifiers.

## 🔧 Features

- Format-preserving encryption using FF1
- Supports radix-based input (e.g., radix 10 for digits)
- Encryption/Decryption with:
  - AES-128 (OpenSSL)
  - ChaCha20 (experimental)
- Plaintext, ciphertext, and decrypted output display
- Easy to configure and extend


