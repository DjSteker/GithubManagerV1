/*
 * Cifrado.cpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#include "Cifrado.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <vector>
#include <stdexcept>

static std::string b64_encode(const unsigned char* d, int len){
    int out_len = 4*((len+2)/3);
    std::string out(out_len+1, '\0');
    int r = EVP_EncodeBlock((unsigned char*)out.data(), d, len);
    out.resize(r);
    return out;
}
static std::vector<unsigned char> b64_decode(const std::string& in){
    std::vector<unsigned char> out(in.size());
    int r = EVP_DecodeBlock(out.data(), (const unsigned char*)in.c_str(), in.size());
    out.resize(r);
    while(!out.empty() && out.back()==0) out.pop_back();
    return out;
}

std::string Cifrado::encriptar(const std::string& txt, const std::string& pass){
    unsigned char salt[16], iv[16];
    RAND_bytes(salt,16); RAND_bytes(iv,16);
    unsigned char key[32];
    PKCS5_PBKDF2_HMAC(pass.c_str(), pass.size(), salt,16, 10000, EVP_sha256(), 32, key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    std::vector<unsigned char> out(txt.size()+32);
    int len1=0,len2=0;
    EVP_EncryptUpdate(ctx, out.data(), &len1, (unsigned char*)txt.c_str(), txt.size());
    EVP_EncryptFinal_ex(ctx, out.data()+len1, &len2);
    EVP_CIPHER_CTX_free(ctx);
    out.resize(len1+len2);

    std::vector<unsigned char> blob;
    blob.insert(blob.end(), salt, salt+16);
    blob.insert(blob.end(), iv, iv+16);
    blob.insert(blob.end(), out.begin(), out.end());
    return b64_encode(blob.data(), blob.size());
}

std::string Cifrado::desencriptar(const std::string& b64, const std::string& pass){
    auto blob = b64_decode(b64);
    if(blob.size()<32) return "";
    unsigned char *salt = blob.data();
    unsigned char *iv = blob.data()+16;
    unsigned char *ct = blob.data()+32;
    int ct_len = blob.size()-32;
    unsigned char key[32];
    PKCS5_PBKDF2_HMAC(pass.c_str(), pass.size(), salt,16, 10000, EVP_sha256(), 32, key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    std::vector<unsigned char> out(ct_len+32);
    int len1=0,len2=0;
    EVP_DecryptUpdate(ctx, out.data(), &len1, ct, ct_len);
    EVP_DecryptFinal_ex(ctx, out.data()+len1, &len2);
    EVP_CIPHER_CTX_free(ctx);
    out.resize(len1+len2);
    return std::string(out.begin(), out.end());
}

