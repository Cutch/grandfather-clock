/*
 * Utils.cpp
 *
 *  See Utils.h for description.
 *
 *  Created on: Jun 30, 2014
 *      Author: hoffmaj
 */

#include "Utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "sha256.h"

/* Constants for base64Encode. */
static const char ENCODE_CHARS[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char PAD_CHAR = '=';

/* Constants for findHttpStatusCode. */
static const char HTTP_STATUS_CODE_PREFIX[] = "HTTP/1.1 ";
static const int HTTP_STATUS_CODE_LEN = 3;

/* Constants for hmacSha256. */
static const int BLOCK_SIZE = 64;
static const char OPAD = 0x5c;
static const char IPAD = 0x36;
const int SHA256_DEC_HASH_LEN = 32;

char *base64Encode(const char *toEncode) {
    int inLen = strlen(toEncode);
    /* For every three input chars there are 4 output chars, plus an extra 4
     * output chars for the possible 1 or 2 remaining input chars, plus an
     * extra byte for null termination. */
    size_t encodedLen = (((inLen / 3) + (inLen % 3 > 0)) * 4) + 1;

    char *encoded = new char[encodedLen]();
    int chunkIdx;
    char inChar1;
    char inChar2;
    char inChar3;
    int outIdx1;
    int outIdx2;
    int outIdx3;
    int outIdx4;

    for (chunkIdx = 0; chunkIdx < inLen / 3; chunkIdx++) {
        /* This approach of treating each character individually instead of
         * containing all bits in a long type allows the encoding to work on 8,
         * 16, 32, and 64 bit systems. */
        inChar1 = *toEncode++;
        inChar2 = *toEncode++;
        inChar3 = *toEncode++;

        outIdx1 = (inChar1 & 0xFC) >> 2;
        outIdx2 = ((inChar1 & 0x03) << 4) + ((inChar2 & 0xF0) >> 4);
        outIdx3 = ((inChar2 & 0x0F) << 2) + ((inChar3 & 0xC0) >> 6);
        outIdx4 = inChar3 & 0x3F;

        encoded[chunkIdx * 4] = ENCODE_CHARS[outIdx1];
        encoded[chunkIdx * 4 + 1] = ENCODE_CHARS[outIdx2];
        encoded[chunkIdx * 4 + 2] = ENCODE_CHARS[outIdx3];
        encoded[chunkIdx * 4 + 3] = ENCODE_CHARS[outIdx4];
    }

    switch (inLen % 3) {
    case 1:
        /* 1 extra input char -> 2 output chars and 2 padding chars. */

        inChar1 = *toEncode++;

        outIdx1 = (inChar1 & 0xFC) >> 2;
        outIdx2 = (inChar1 & 0x03) << 4;

        encoded[chunkIdx * 4] = ENCODE_CHARS[outIdx1];
        encoded[chunkIdx * 4 + 1] = ENCODE_CHARS[outIdx2];
        encoded[chunkIdx * 4 + 2] = PAD_CHAR;
        encoded[chunkIdx * 4 + 3] = PAD_CHAR;
        chunkIdx++;
        break;
    case 2:
        /* 2 extra input chars -> 3 output chars and 1 padding char. */

        inChar1 = *toEncode++;
        inChar2 = *toEncode++;
        outIdx1 = (inChar1 & 0xFC) >> 2;
        outIdx2 = ((inChar1 & 0x03) << 4) + ((inChar2 & 0xF0) >> 4);
        outIdx3 = ((inChar2 & 0x0F) << 2);
        encoded[chunkIdx * 4] = ENCODE_CHARS[outIdx1];
        encoded[chunkIdx * 4 + 1] = ENCODE_CHARS[outIdx2];
        encoded[chunkIdx * 4 + 2] = ENCODE_CHARS[outIdx3];
        encoded[chunkIdx * 4 + 3] = PAD_CHAR;
        chunkIdx++;
        break;
    }
    /* Ensure null termination. */
    encoded[chunkIdx * 4] = 0;

    return encoded;
}

int digitCount(int i) {
    int digits;
    for (digits = 0; i != 0; digits++)
        i /= 10;
    return digits;
}

char* escapeQuotes(const char* unescaped) {
    int unescapedLen = strlen(unescaped);

    /* Count quotes so that the amount of memory to be allocated can be
     * determined */
    int quoteCount = 0;
    for (int i = 0; i < unescapedLen; i++) {
        if (unescaped[i] == '\"') {
            quoteCount++;
        }
    }

    /* Copy ever character over, including a backslash before every quote. */
    char* escaped = new char[unescapedLen + quoteCount + 1]();
    int escapedWritten = 0;
    for (int i = 0; i < unescapedLen; i++) {
        if (unescaped[i] == '\"') {
            escaped[escapedWritten] = '\\';
            escapedWritten++;
        }
        escaped[escapedWritten] = unescaped[i];
        escapedWritten++;
    }
    return escaped;
}


int findHttpStatusCode(const char* str) {
    /* If the input is null OR the input is not long enough to contain the
     * error code OR the first characters of the input are not
     * HTTP_STATUS_CODE_PREFIX, return 0; */
    if (str == NULL
            || strlen(str)
                    < strlen(HTTP_STATUS_CODE_PREFIX) + HTTP_STATUS_CODE_LEN
            || strncmp(HTTP_STATUS_CODE_PREFIX, str,
                    strlen(HTTP_STATUS_CODE_PREFIX))) {
        return 0;
    }
    /* copy the error code string and convert it to an int. */
    char errorCodeStr[HTTP_STATUS_CODE_LEN + 1];
    strncpy(errorCodeStr, str + strlen(HTTP_STATUS_CODE_PREFIX),
            HTTP_STATUS_CODE_LEN);
    return atoi(errorCodeStr);
}


char* getTimeFromInvalidSignatureMessage(const char* message) {
    int messageLen = strlen(message);
    /* Iterate through each character in the string. */
    for (int i = 0; i < messageLen; i++) {
        /* If an opening parenthesis is found, copy the following 15
         * characters, excluding the 9th character which is a 'T'.*/
        if (message[i] == '(') {
            char* time = new char[15]();
            sprintf(time, "%.8s%.6s", message + i + 1, message + i + 10);
            return time;
        }
    }
    return 0;
}

char* hmacSha256(const char* key, int keyLen, const char* message,
        int messageLen) {
    SHA256* sha256 = new SHA256();
    /* The corrected key should be BLOCK_SIZE long. */
    char* correctedKey = new char[BLOCK_SIZE + 1]();
    /* If the key is greater than BLOCK_SIZE long, copy over its sha256 hash of
     * SHA256_DEC_HASH_LEN, leaving 0-padding to fill the entire BLOCK_SIZE. */
    if ((int) keyLen > BLOCK_SIZE) {
        sha256->reset();
        sha256->add(key, keyLen);
        char* hashedKey = sha256->getHashDec();
        memcpy(correctedKey, hashedKey, SHA256_DEC_HASH_LEN);
        delete[] hashedKey;
    }
    /* if the key is less than BLOCK_SIZE long, simply copy it over, leaving
     * 0-padding to fill the entire BLOCK_SIZE. */
    else {
        memcpy(correctedKey, key, keyLen);
    }

    /* Using an exclusive or with these OPAD and IPAD values to create the
     * iPadded and oPadded values specified by the HMAC algorithm. */
    char* oPadded = new char[BLOCK_SIZE + 1]();
    char* iPadded = new char[BLOCK_SIZE + 1]();
    for (int i = 0; i < BLOCK_SIZE; i++) {
        oPadded[i] = correctedKey[i] ^ OPAD;
        iPadded[i] = correctedKey[i] ^ IPAD;
    }

    delete[] correctedKey;

    /* Create the inner hash with the concatenation of iPadded and message. */
    sha256->reset();
    sha256->add(iPadded, BLOCK_SIZE);
    delete[] iPadded;
    sha256->add(message, messageLen);
    char* innerHash = sha256->getHashDec();

    /* Create the outer hash with the concatenation of oPadded and
     * innerhash. */
    sha256->reset();
    sha256->add(oPadded, BLOCK_SIZE);
    delete[] oPadded;
    sha256->add(innerHash, SHA256_DEC_HASH_LEN);
    delete[] innerHash;
    char* final = sha256->getHashDec();
    delete sha256;
    return final;
}

