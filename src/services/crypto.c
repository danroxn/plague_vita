#include "services/methods.h"

#include "compat/jni/runtime.h"
#include "compat/jni/strings.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#error OpenSSL 1.1.0 or later is required
#endif

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>
#include <pthread.h>

static pthread_once_t crypto_once = PTHREAD_ONCE_INIT;
static void initialize_crypto(void) {
    OSSL_PROVIDER_load(NULL, "default");
    OSSL_PROVIDER_load(NULL, "legacy");
}
#endif
static void ensure_crypto_initialized(void) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    pthread_once(&crypto_once, initialize_crypto);
#endif
}
enum {
    MAX_DERIVED_KEY_BYTES = 65536,
    MAX_PBKDF2_ITERATIONS = 10000000,
    MAX_CIPHER_INPUT_BYTES = 16 * 1024 * 1024,
    MIN_BLOWFISH_KEY_BYTES = 4,
    MAX_BLOWFISH_KEY_BYTES = 56,
};

typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
} CryptoBuffer;

typedef struct {
    jbyteArray array;
    jbyte *data;
    jsize length;
} BorrowedByteArray;

typedef enum { CIPHER_AES_CBC, CIPHER_BLOWFISH_CBC } CipherAlgorithm;

static void release_crypto_buffer(CryptoBuffer *buffer) {
    OPENSSL_clear_free(buffer->data, buffer->capacity);
    *buffer = (CryptoBuffer){0};
}

static unsigned hash_buffer(const EVP_MD *algorithm, const void *input, size_t input_length,
                            unsigned char digest[EVP_MAX_MD_SIZE]) {
    unsigned digest_length = 0;
    return EVP_Digest(input, input_length, digest, &digest_length, algorithm, NULL) == 1
               ? digest_length
               : 0;
}

static unsigned hmac_buffer(const EVP_MD *algorithm, const void *input, size_t input_length,
                            const void *key, int key_length,
                            unsigned char digest[EVP_MAX_MD_SIZE]) {
    unsigned digest_length = 0;
    return key_length > 0 &&
                   HMAC(algorithm, key, key_length, input, input_length, digest, &digest_length)
               ? digest_length
               : 0;
}

static CryptoBuffer derive_pbkdf2_sha1(const char *password, size_t password_length,
                                       const unsigned char *salt, int salt_length, int key_length,
                                       int iterations) {
    CryptoBuffer output = {0};
    if (key_length > 0 && key_length <= MAX_DERIVED_KEY_BYTES) {
        output.capacity = key_length;
        output.data = OPENSSL_malloc(output.capacity);
    }
    if (password && salt && salt_length > 0 && output.data && iterations > 0 &&
        iterations <= MAX_PBKDF2_ITERATIONS &&
        PKCS5_PBKDF2_HMAC_SHA1(password, password_length, salt, salt_length, iterations, key_length,
                               output.data) == 1)
        output.length = key_length;
    return output;
}

static CryptoBuffer transform_cbc(CipherAlgorithm algorithm, int encrypt,
                                  const unsigned char *input, int input_length,
                                  const unsigned char *key, int key_length, const unsigned char *iv,
                                  int iv_length) {
    CryptoBuffer output = {0};
    const int is_aes = algorithm == CIPHER_AES_CBC;
    if ((is_aes && key_length != 16 && key_length != 24 && key_length != 32) ||
        (!is_aes && (key_length < MIN_BLOWFISH_KEY_BYTES || key_length > MAX_BLOWFISH_KEY_BYTES)) ||
        iv_length != (is_aes ? 16 : 8) || input_length > MAX_CIPHER_INPUT_BYTES)
        return output;
    const EVP_CIPHER *cipher = is_aes ? (key_length == 32   ? EVP_aes_256_cbc()
                                         : key_length == 24 ? EVP_aes_192_cbc()
                                                            : EVP_aes_128_cbc())
                                      : EVP_bf_cbc();
    output.capacity = input_length + EVP_MAX_BLOCK_LENGTH;
    output.data = OPENSSL_malloc(output.capacity);
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    int produced_bytes = 0, final_bytes = 0;
    if (output.data && context &&
        EVP_CipherInit_ex(context, cipher, NULL, NULL, NULL, encrypt) == 1 &&
        EVP_CIPHER_CTX_set_key_length(context, key_length) == 1 &&
        EVP_CipherInit_ex(context, NULL, NULL, key, iv, -1) == 1 &&
        EVP_CipherUpdate(context, output.data, &produced_bytes, input, input_length) == 1 &&
        EVP_CipherFinal_ex(context, output.data + produced_bytes, &final_bytes) == 1)
        output.length = produced_bytes + final_bytes;
    EVP_CIPHER_CTX_free(context);
    return output;
}

static BorrowedByteArray borrow_byte_array(jobject object) {
    BorrowedByteArray view = {.array = (jbyteArray)object};
    if (view.array) {
        view.length = port_jni->GetArrayLength(&port_jni, view.array);
        view.data = port_jni->GetByteArrayElements(&port_jni, view.array, NULL);
    }
    return view;
}

static void release_byte_array(BorrowedByteArray view) {
    if (view.array)
        port_jni->ReleaseByteArrayElements(&port_jni, view.array, view.data, JNI_ABORT);
}

static jbyteArray new_byte_array_from_buffer(const void *buffer, size_t length) {
    jbyteArray array = port_jni->NewByteArray(&port_jni, (jsize)length);
    if (length)
        port_jni->SetByteArrayRegion(&port_jni, array, 0, length, buffer);
    return array;
}

static jvalue digest_bytes(const PortJniCall *call, const EVP_MD *algorithm, int use_hmac) {
    ensure_crypto_initialized();
    if (!call->arguments[0].l)
        return (jvalue){.l = new_byte_array_from_buffer(NULL, 0)};
    BorrowedByteArray input = borrow_byte_array(call->arguments[0].l);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned digest_length;
    if (use_hmac) {
        BorrowedByteArray key = borrow_byte_array(call->arguments[1].l);
        digest_length =
            hmac_buffer(algorithm, input.data, input.length, key.data, key.length, digest);
        release_byte_array(key);
    } else {
        digest_length = hash_buffer(algorithm, input.data, input.length, digest);
    }
    release_byte_array(input);
    return (jvalue){.l = new_byte_array_from_buffer(digest, digest_length)};
}

static jvalue crypto_derive_key(const PortJniCall *call) {
    ensure_crypto_initialized();
    const jvalue *arguments = call->arguments;
    size_t password_length;
    char *password =
        port_jni_string_to_utf8_alloc_with_length((jstring)arguments[0].l, &password_length);
    BorrowedByteArray salt = borrow_byte_array(arguments[2].l);
    CryptoBuffer key =
        derive_pbkdf2_sha1(arguments[0].l ? password : NULL, password_length,
                           (unsigned char *)salt.data, salt.length, arguments[1].i, arguments[3].i);
    jbyteArray result = new_byte_array_from_buffer(key.data, key.length);
    release_crypto_buffer(&key);
    OPENSSL_cleanse(password, password_length);
    free(password);
    release_byte_array(salt);
    return (jvalue){.l = result};
}

static jvalue cipher_bytes(const PortJniCall *call, CipherAlgorithm algorithm, int encrypt) {
    ensure_crypto_initialized();
    const jvalue *arguments = call->arguments;
    if (!arguments[0].l || !arguments[1].l || !arguments[2].l)
        return (jvalue){.l = new_byte_array_from_buffer(NULL, 0)};
    BorrowedByteArray input = borrow_byte_array(arguments[0].l);
    BorrowedByteArray key = borrow_byte_array(arguments[1].l);
    BorrowedByteArray iv = borrow_byte_array(arguments[2].l);
    CryptoBuffer output =
        transform_cbc(algorithm, encrypt, (unsigned char *)input.data, input.length,
                      (unsigned char *)key.data, key.length, (unsigned char *)iv.data, iv.length);
    jbyteArray result = new_byte_array_from_buffer(output.data, output.length);
    release_crypto_buffer(&output);
    release_byte_array(input);
    release_byte_array(key);
    release_byte_array(iv);
    return (jvalue){.l = result};
}
static jvalue crypto_hash_md5(const PortJniCall *call) {
    return digest_bytes(call, EVP_md5(), 0);
}
static jvalue crypto_hash_sha1(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha1(), 0);
}
static jvalue crypto_hash_sha256(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha256(), 0);
}
static jvalue crypto_hash_sha512(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha512(), 0);
}
static jvalue crypto_hmac_md5(const PortJniCall *call) {
    return digest_bytes(call, EVP_md5(), 1);
}
static jvalue crypto_hmac_sha1(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha1(), 1);
}
static jvalue crypto_hmac_sha256(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha256(), 1);
}
static jvalue crypto_hmac_sha512(const PortJniCall *call) {
    return digest_bytes(call, EVP_sha512(), 1);
}
static jvalue crypto_encrypt_aes128(const PortJniCall *call) {
    return cipher_bytes(call, CIPHER_AES_CBC, 1);
}
static jvalue crypto_encrypt_blowfish(const PortJniCall *call) {
    return cipher_bytes(call, CIPHER_BLOWFISH_CBC, 1);
}
static jvalue crypto_decrypt_aes128(const PortJniCall *call) {
    return cipher_bytes(call, CIPHER_AES_CBC, 0);
}
static jvalue crypto_decrypt_blowfish(const PortJniCall *call) {
    return cipher_bytes(call, CIPHER_BLOWFISH_CBC, 0);
}
static const PortServiceMethod methods[] = {
    {{"com/miniclip/crypto/crypto", "decryptAES128", "([B[B[B)[B", 1}, crypto_decrypt_aes128},
    {{"com/miniclip/crypto/crypto", "decryptBlowfish", "([B[B[B)[B", 1}, crypto_decrypt_blowfish},
    {{"com/miniclip/crypto/crypto", "deriveKey", "(Ljava/lang/String;I[BI)[B", 1},
     crypto_derive_key},
    {{"com/miniclip/crypto/crypto", "encryptAES128", "([B[B[B)[B", 1}, crypto_encrypt_aes128},
    {{"com/miniclip/crypto/crypto", "encryptBlowfish", "([B[B[B)[B", 1}, crypto_encrypt_blowfish},
    {{"com/miniclip/crypto/crypto", "hashMD5", "([B)[B", 1}, crypto_hash_md5},
    {{"com/miniclip/crypto/crypto", "hashSHA1", "([B)[B", 1}, crypto_hash_sha1},
    {{"com/miniclip/crypto/crypto", "hashSHA256", "([B)[B", 1}, crypto_hash_sha256},
    {{"com/miniclip/crypto/crypto", "hashSHA512", "([B)[B", 1}, crypto_hash_sha512},
    {{"com/miniclip/crypto/crypto", "hmacMD5", "([B[B)[B", 1}, crypto_hmac_md5},
    {{"com/miniclip/crypto/crypto", "hmacSHA1", "([B[B)[B", 1}, crypto_hmac_sha1},
    {{"com/miniclip/crypto/crypto", "hmacSHA256", "([B[B)[B", 1}, crypto_hmac_sha256},
    {{"com/miniclip/crypto/crypto", "hmacSHA512", "([B[B)[B", 1}, crypto_hmac_sha512},
};
const PortServiceMethodTable port_crypto_methods = {methods, sizeof(methods) / sizeof(methods[0])};
