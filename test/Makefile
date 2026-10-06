CC = gcc
CFLAGS = -I. -I.. -I../build -std=c99 -w -DTEST_MODE

# All crypto source files
CRYPTO_SRC = ../src/crypto/mnemonic.c ../src/crypto/hd_wallet.c ../src/crypto/pbkdf2.c \
             ../src/crypto/sha256.c ../src/crypto/sha512.c \
             ../src/crypto/ripemd160.c ../src/crypto/secp256k1.c ../src/crypto/bn_inv.c \
             ../src/crypto/bip39_wordlist.c \
             ../src/crypto/bip39_words_1.c ../src/crypto/bip39_words_2.c \
             ../src/crypto/entropy_data.c \
             ../src/crypto/endian.c ../src/crypto/hmac.c \
             ../src/crypto/sha512_constants.c ../src/crypto/sha512_transform.c \
             ../src/crypto/gpow_table.c progress.c

mnemonic_to_address: mnemonic_to_address.c compat.c $(CRYPTO_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f mnemonic_to_address

.PHONY: clean
