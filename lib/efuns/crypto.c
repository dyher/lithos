// lib/efuns/crypto.c - OpenSSL Cryptography Efun Support

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "src/std.h"
#include "src/interpret.h"
#include "src/stralloc.h"

// string sha1(string str) -> returns 40-char hex string
void f_sha1(void) {
    const char *str = SVALUE_STRPTR(sp);
    size_t len = strlen(str);
    
    unsigned char hash[20];
    unsigned int hash_len = 20;
    
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, str, len);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    
    char hex[41];
    for (int i = 0; i < 20; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    
    pop_stack();
    push_malloced_string(string_copy(hex, "f_sha1"));
}

// string websocket_accept(string key) -> returns base64(sha1(key + magic))
void f_websocket_accept(void) {
    const char *key = SVALUE_STRPTR(sp);
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    char concat[512];
    snprintf(concat, sizeof(concat), "%s%s", key, magic);
    
    unsigned char hash[20];
    unsigned int hash_len = 20;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, concat, strlen(concat));
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, hash, hash_len);
    BIO_flush(b64);
    
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    
    // 使用 Neolith 的 string_copy 分配記憶體，確保 GC 安全
    char *result = string_copy(bptr->data, "f_websocket_accept");
    BIO_free_all(b64);
    
    pop_stack();
    push_malloced_string(result);
}
