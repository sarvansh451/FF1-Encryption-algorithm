#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/bn.h>
#include "fpe.h"
#include "fpe_locl.h"

void hex2chars(unsigned char hex[], unsigned char result[])
{
    int len = strlen(hex);
    unsigned char temp[3];
    temp[2] = 0x00;

    int j = 0;
    for (int i = 0; i < len; i += 2) {
        temp[0] = hex[i];
        temp[1] = hex[i + 1];
        result[j] = (char)strtol(temp, NULL, 16);
        ++j;
    }
}

void map_chars(unsigned char str[], unsigned int result[])
{
    int len = strlen(str);

    for (int i = 0; i < len; ++i)
        if (str[i] >= 'a')
            result[i] = str[i] - 'a' + 10;
        else
            result[i] = str[i] - '0';
}

void inverse_map_chars(unsigned result[], unsigned char str[], int len)
{
    for (int i = 0; i < len; ++i)
        if (result[i] < 10)
            str[i] = result[i] + '0';
        else
            str[i] = result[i] - 10 + 'a';

    str[len] = 0x00;
}

// convert numeral string to number
void str2num(BIGNUM* Y, const unsigned int* X, unsigned long long radix, unsigned int len, BN_CTX* ctx)
{
    BN_CTX_start(ctx);
    BIGNUM* r = BN_CTX_get(ctx),
        * x = BN_CTX_get(ctx);

    BN_set_word(Y, 0);
    BN_set_word(r, radix);
    for (int i = 0; i < len; ++i) {
        // Y = Y * radix + X[i]
        BN_set_word(x, X[i]);
        BN_mul(Y, Y, r, ctx);
        BN_add(Y, Y, x);
    }

    BN_CTX_end(ctx);
    return;
}

// convert number to numeral string
void num2str(const BIGNUM* X, unsigned int* Y, unsigned int radix, int len, BN_CTX* ctx)
{
    BN_CTX_start(ctx);
    BIGNUM* dv = BN_CTX_get(ctx),
        * rem = BN_CTX_get(ctx),
        * r = BN_CTX_get(ctx),
        * XX = BN_CTX_get(ctx);

    BN_copy(XX, X);
    BN_set_word(r, radix);
    memset(Y, 0, len << 2);

    for (int i = len - 1; i >= 0; --i) {
        // XX / r = dv ... rem
        BN_div(dv, rem, XX, r, ctx);
        // Y[i] = XX % r
        Y[i] = BN_get_word(rem);
        // XX = XX / r
        BN_copy(XX, dv);
    }

    BN_CTX_end(ctx);
    return;
}



void print_hex(const char* label, const unsigned char* data, int len) {
   
    printf("");
}

void FF1_encrypt(const unsigned int* in, unsigned int* out, EVP_CIPHER_CTX* evp_enc_ctx, const unsigned char* tweak, const unsigned int radix, size_t inlen, size_t tweaklen)
{
    BIGNUM* bnum = BN_new(),
        * y = BN_new(),
        * c = BN_new(),
        * anum = BN_new(),
        * qpow_u = BN_new(),
        * qpow_v = BN_new();
    BN_CTX* ctx = BN_CTX_new();

    union {
        long one;
        char little;
    } is_endian = { 1 };

    memcpy(out, in, inlen << 2);
    int u = floor2(inlen, 1);
    int v = inlen - u;
    unsigned int* A = out, * B = out + u;
    pow_uv(qpow_u, qpow_v, radix, u, v, ctx);

    unsigned int temp = (unsigned int)ceil(v * log2(radix));
    const int b = ceil2(temp, 3);
    const int d = 4 * ceil2(b, 2) + 4;

    int pad = ((-tweaklen - b - 1) % 16 + 16) % 16;
    int Qlen = tweaklen + pad + 1 + b;
    unsigned char P[16];
    unsigned char* Q = (unsigned char*)OPENSSL_malloc(Qlen), * Bytes = (unsigned char*)OPENSSL_malloc(b);

    // initialize P
    P[0] = 0x1;
    P[1] = 0x2;
    P[2] = 0x1;
    P[7] = u % 256;
    if (is_endian.little) {
        temp = (radix << 8) | 10;
        P[3] = (temp >> 24) & 0xff;
        P[4] = (temp >> 16) & 0xff;
        P[5] = (temp >> 8) & 0xff;
        P[6] = temp & 0xff;
        P[8] = (inlen >> 24) & 0xff;
        P[9] = (inlen >> 16) & 0xff;
        P[10] = (inlen >> 8) & 0xff;
        P[11] = inlen & 0xff;
        P[12] = (tweaklen >> 24) & 0xff;
        P[13] = (tweaklen >> 16) & 0xff;
        P[14] = (tweaklen >> 8) & 0xff;
        P[15] = tweaklen & 0xff;
    }
    else {
        *((unsigned int*)(P + 3)) = (radix << 8) | 10;
        *((unsigned int*)(P + 8)) = inlen;
        *((unsigned int*)(P + 12)) = tweaklen;
    }

    // initialize Q
    memcpy(Q, tweak, tweaklen);
    memset(Q + tweaklen, 0x00, pad);
    assert(tweaklen + pad - 1 <= Qlen);

    unsigned char R[16];
    int cnt = ceil2(d, 4) - 1;
    int Slen = 16 + cnt * 16;
    unsigned char* S = (unsigned char*)OPENSSL_malloc(Slen);



    for (int i = 0; i < FF1_ROUNDS; ++i) {
        // v
        int m = (i & 1) ? v : u;

        // i
        Q[tweaklen + pad] = i & 0xff;
        str2num(bnum, B, radix, inlen - m, ctx);
        int BytesLen = BN_bn2bin(bnum, Bytes);
        memset(Q + Qlen - b, 0x00, b);

        int qtmp = Qlen - BytesLen;
        memcpy(Q + qtmp, Bytes, BytesLen);

        // ii PRF(P || Q), P is always 16 bytes long
  
        int tlen;
        EVP_EncryptUpdate(evp_enc_ctx, R, NULL, P, 16);
      
        int count = Qlen / 16;
        unsigned char Ri[16];
        unsigned char* Qi = Q;
        for (int cc = 0; cc < count; ++cc) {
            for (int j = 0; j < 16; ++j)    Ri[j] = Qi[j] ^ R[j];
      
            EVP_EncryptUpdate(evp_enc_ctx, R, NULL, Ri, 16);
        
            Qi += 16;
        }

        // iii 
        unsigned char tmp[16], SS[16];
        memset(S, 0x00, Slen);
        assert(Slen >= 16);
        memcpy(S, R, 16);
        for (int j = 1; j <= cnt; ++j) {
            memset(tmp, 0x00, 16);

            if (is_endian.little) {
                // convert to big endian
                // full unroll
                tmp[15] = j & 0xff;
                tmp[14] = (j >> 8) & 0xff;
                tmp[13] = (j >> 16) & 0xff;
                tmp[12] = (j >> 24) & 0xff;
            }
            else *((unsigned int*)tmp + 3) = j;

            for (int k = 0; k < 16; ++k)    tmp[k] ^= R[k];
            EVP_EncryptUpdate(evp_enc_ctx, SS, NULL, tmp, 16);
            assert((S + 16 * j)[0] == 0x00);
            assert(16 + 16 * j <= Slen);
            memcpy(S + 16 * j, SS, 16);
        }

        // iv
        BN_bin2bn(S, d, y);
        // vi
        // (num(A, radix, m) + y) % qpow(radix, m);
        str2num(anum, A, radix, m, ctx);
        // anum = (anum + y) mod qpow_uv
        if (m == u)    BN_mod_add(c, anum, y, qpow_u, ctx);
        else    BN_mod_add(c, anum, y, qpow_v, ctx);

        // swap A and B
        assert(A != B);
        A = (unsigned int*)((uintptr_t)A ^ (uintptr_t)B);
        B = (unsigned int*)((uintptr_t)B ^ (uintptr_t)A);
        A = (unsigned int*)((uintptr_t)A ^ (uintptr_t)B);
        num2str(c, B, radix, m, ctx);


        print_hex("B", (unsigned char*)B, v * sizeof(unsigned int));
    }

    // free the space
    BN_clear_free(anum);
    BN_clear_free(bnum);
    BN_clear_free(c);
    BN_clear_free(y);
    BN_clear_free(qpow_u);
    BN_clear_free(qpow_v);
    BN_CTX_free(ctx);
    OPENSSL_free(Bytes);
    OPENSSL_free(Q);
    OPENSSL_free(S);
    return;
}

void FF1_decrypt(const unsigned int* in, unsigned int* out, EVP_CIPHER_CTX* evp_enc_ctx, const unsigned char* tweak, const unsigned int radix, size_t inlen, size_t tweaklen)
{
    BIGNUM* bnum = BN_new(),
        * y = BN_new(),
        * c = BN_new(),
        * anum = BN_new(),
        * qpow_u = BN_new(),
        * qpow_v = BN_new();
    BN_CTX* ctx = BN_CTX_new();

    union {
        long one;
        char little;
    } is_endian = { 1 };

    memcpy(out, in, inlen << 2);
    int u = floor2(inlen, 1);
    int v = inlen - u;
    unsigned int* A = out, * B = out + u;
    pow_uv(qpow_u, qpow_v, radix, u, v, ctx);

    unsigned int temp = (unsigned int)ceil(v * log2(radix));
    const int b = ceil2(temp, 3);
    const int d = 4 * ceil2(b, 2) + 4;

    int pad = ((-tweaklen - b - 1) % 16 + 16) % 16;
    int Qlen = tweaklen + pad + 1 + b;
    unsigned char P[16];
    unsigned char* Q = (unsigned char*)OPENSSL_malloc(Qlen), * Bytes = (unsigned char*)OPENSSL_malloc(b);

    // initialize P
    P[0] = 0x1;
    P[1] = 0x2;
    P[2] = 0x1;
    P[7] = u % 256;
    if (is_endian.little) {
        temp = (radix << 8) | 10;
        P[3] = (temp >> 24) & 0xff;
        P[4] = (temp >> 16) & 0xff;
        P[5] = (temp >> 8) & 0xff;
        P[6] = temp & 0xff;
        P[8] = (inlen >> 24) & 0xff;
        P[9] = (inlen >> 16) & 0xff;
        P[10] = (inlen >> 8) & 0xff;
        P[11] = inlen & 0xff;
        P[12] = (tweaklen >> 24) & 0xff;
        P[13] = (tweaklen >> 16) & 0xff;
        P[14] = (tweaklen >> 8) & 0xff;
        P[15] = tweaklen & 0xff;
    }
    else {
        *((unsigned int*)(P + 3)) = (radix << 8) | 10;
        *((unsigned int*)(P + 8)) = inlen;
        *((unsigned int*)(P + 12)) = tweaklen;
    }

    // initialize Q
    memcpy(Q, tweak, tweaklen);
    memset(Q + tweaklen, 0x00, pad);
    assert(tweaklen + pad - 1 <= Qlen);

    unsigned char R[16];
    int cnt = ceil2(d, 4) - 1;
    int Slen = 16 + cnt * 16;
    unsigned char* S = (unsigned char*)OPENSSL_malloc(Slen);


    for (int i = FF1_ROUNDS - 1; i >= 0; --i) {
        // v
        int m = (i & 1) ? v : u;

        // i
        Q[tweaklen + pad] = i & 0xff;
        str2num(bnum, A, radix, inlen - m, ctx);
        int BytesLen = BN_bn2bin(bnum, Bytes);
        memset(Q + Qlen - b, 0x00, b);

        int qtmp = Qlen - BytesLen;
        memcpy(Q + qtmp, Bytes, BytesLen);

       
      
        int tlen;
        EVP_EncryptUpdate(evp_enc_ctx, R, NULL, P, 16);
      
        int count = Qlen / 16;
        unsigned char Ri[16];
        unsigned char* Qi = Q;
        for (int cc = 0; cc < count; ++cc) {
            for (int j = 0; j < 16; ++j)    Ri[j] = Qi[j] ^ R[j];
        
            EVP_EncryptUpdate(evp_enc_ctx, R, NULL, Ri, 16);
         
            Qi += 16;
        }

        // iii 
        unsigned char tmp[16], SS[16];
        memset(S, 0x00, Slen);
        assert(Slen >= 16);
        memcpy(S, R, 16);
        for (int j = 1; j <= cnt; ++j) {
            memset(tmp, 0x00, 16);

            if (is_endian.little) {
                // convert to big endian
                // full unroll
                tmp[15] = j & 0xff;
                tmp[14] = (j >> 8) & 0xff;
                tmp[13] = (j >> 16) & 0xff;
                tmp[12] = (j >> 24) & 0xff;
            }
            else *((unsigned int*)tmp + 3) = j;

            for (int k = 0; k < 16; ++k)    tmp[k] ^= R[k];
            EVP_EncryptUpdate(evp_enc_ctx, SS, NULL, tmp, 16);
            assert((S + 16 * j)[0] == 0x00);
            assert(16 + 16 * j <= Slen);
            memcpy(S + 16 * j, SS, 16);
        }

        // iv
        BN_bin2bn(S, d, y);
        // vi
        // (num(B, radix, m) - y) % qpow(radix, m);
        str2num(anum, B, radix, m, ctx);
        // anum = (anum - y) mod qpow_uv
        if (m == u)    BN_mod_sub(c, anum, y, qpow_u, ctx);
        else    BN_mod_sub(c, anum, y, qpow_v, ctx);

        // swap A and B
        assert(A != B);
        A = (unsigned int*)((uintptr_t)A ^ (uintptr_t)B);
        B = (unsigned int*)((uintptr_t)B ^ (uintptr_t)A);
        A = (unsigned int*)((uintptr_t)A ^ (uintptr_t)B);
        num2str(c, A, radix, m, ctx);

        print_hex("B", (unsigned char*)B, v * sizeof(unsigned int));
    }

    // free the space
    BN_clear_free(anum);
    BN_clear_free(bnum);
    BN_clear_free(c);
    BN_clear_free(y);
    BN_clear_free(qpow_u);
    BN_clear_free(qpow_v);
    BN_CTX_free(ctx);
    OPENSSL_free(Bytes);
    OPENSSL_free(Q);
    OPENSSL_free(S);
    return;
}



int main() {
    // Predefined values
    const char* key_hex = "2B7E151628AED2A6ABF7158809CF4F3C2B7E151628AED2A6ABF7158809CF4F3C"; // 256-bit key for AES and ChaCha20
    const char* nonce_hex = "000000000000000000000000"; // 96-bit nonce for ChaCha20
    const int radix = 10;
    const char* plaintext = "012345678912345";

    int klen = strlen(key_hex) / 2;
    int nlen = strlen(nonce_hex) / 2;
    int xlen = strlen(plaintext);

    // Allocate memory dynamically
    unsigned char* k = (unsigned char*)malloc(klen);
    unsigned char* nonce = (unsigned char*)malloc(nlen);
    unsigned int* x = (unsigned int*)malloc(xlen * sizeof(unsigned int));
    unsigned int* y = (unsigned int*)malloc(xlen * sizeof(unsigned int));
    unsigned int* decrypted = (unsigned int*)malloc(xlen * sizeof(unsigned int)); // Allocate for decrypted text
    char* result = (char*)malloc(xlen + 1); // +1 for null terminator

    if (!k || !nonce || !x || !y || !decrypted || !result) {
        printf("Memory allocation failed\n");
        return 1;
    }

    // Convert hex and map characters
    hex2chars(key_hex, k);
    hex2chars(nonce_hex, nonce);
    map_chars(plaintext, x);

    for (int i = 0; i < xlen; ++i)
        assert(x[i] < radix);

    // Initialize EVP_CIPHER_CTX for ChaCha20
    EVP_CIPHER_CTX* chacha20_ctx = EVP_CIPHER_CTX_new();
    if (chacha20_ctx == NULL) {
        printf("Failed to create EVP_CIPHER_CTX for ChaCha20\n");
        free(k);
        free(nonce);
        free(x);
        free(y);
        free(decrypted);
        free(result);
        return 1;
    }

    const EVP_CIPHER* chacha20_cipher = EVP_chacha20();
    if (1 != EVP_EncryptInit_ex(chacha20_ctx, chacha20_cipher, NULL, k, nonce)) {
        printf("EVP_EncryptInit_ex for ChaCha20 failed\n");
        EVP_CIPHER_CTX_free(chacha20_ctx);
        free(k);
        free(nonce);
        free(x);
        free(y);
        free(decrypted);
        free(result);
        return 1;
    }

    // Initialize EVP_CIPHER_CTX for AES-256-ECB
    EVP_CIPHER_CTX* aes_ctx = EVP_CIPHER_CTX_new();
    if (aes_ctx == NULL) {
        printf("Failed to create EVP_CIPHER_CTX for AES-256-ECB\n");
        EVP_CIPHER_CTX_free(chacha20_ctx);
        free(k);
        free(nonce);
        free(x);
        free(y);
        free(decrypted);
        free(result);
        return 1;
    }

    const EVP_CIPHER* aes_cipher = EVP_aes_256_ecb();
    if (1 != EVP_EncryptInit_ex(aes_ctx, aes_cipher, NULL, k, NULL)) {
        printf("EVP_EncryptInit_ex for AES-256-ECB failed\n");
        EVP_CIPHER_CTX_free(chacha20_ctx);
        EVP_CIPHER_CTX_free(aes_ctx);
        free(k);
        free(nonce);
        free(x);
        free(y);
        free(decrypted);
        free(result);
        return 1;
    }

    // Timing variables
    clock_t start, end;
    double cpu_time_used;

    // Measure ChaCha20 performance
    printf("========== ChaCha20 ==========\n");
    for (int i = 0; i < 100; ++i) {
        start = clock();
        for (int j = 0; j < 10000; ++j) {
            FF1_encrypt(x, y, chacha20_ctx, NULL, radix, xlen, 0);
        }
        end = clock();
        cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC * 1000000; // Convert to microseconds
        printf("%d:, %f, microseconds\n", i + 1, cpu_time_used);
    }

    // Measure AES-256-ECB performance
    printf("========== AES-256-ECB ==========\n");
    for (int i = 0; i < 100; ++i) {
        start = clock();
        for (int j = 0; j < 10000; ++j) {
            FF1_encrypt(x, y, aes_ctx, NULL, radix, xlen, 0);
        }
        end = clock();
        cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC * 1000000; // Convert to microseconds
        printf("%d:, %f, microseconds\n", i + 1, cpu_time_used);
    }

    // Clean up
    EVP_CIPHER_CTX_free(chacha20_ctx);
    EVP_CIPHER_CTX_free(aes_ctx);
    free(k);
    free(nonce);
    free(x);
    free(y);
    free(decrypted);
    free(result);

    return 0;
} 
