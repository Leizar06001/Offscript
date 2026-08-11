#include "apikey.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Format du fichier :
 *   "EQK1" (4) | sel (16) | iv (12) | tag GCM (16) | chiffre (n)
 * AES-256-GCM, cle derivee par PBKDF2-HMAC-SHA256. */

#define MAGIC      "EQK1"
#define MAGIC_LEN  4
#define SALT_LEN   16
#define IV_LEN     12
#define TAG_LEN    16
#define PBKDF2_ITER 200000

/* Constante compilee dans le binaire : elle ne rend pas le chiffrement
 * secret, elle evite juste qu'un fichier soit dechiffrable par un autre
 * programme qui connaitrait seulement l'identifiant machine. */
#define APP_PEPPER "enquete-2.5d/apikey/v1"

static char g_path[512];

const char *apikey_path(void) {
	if (g_path[0]) return g_path;

	const char *base = getenv("XDG_CONFIG_HOME");
	char root[400];

	if (base && *base) {
		snprintf(root, sizeof(root), "%s", base);
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home) {
			struct passwd *pw = getpwuid(getuid());
			home = (pw && pw->pw_dir) ? pw->pw_dir : ".";
		}
		snprintf(root, sizeof(root), "%s/.config", home);
	}

	snprintf(g_path, sizeof(g_path), "%s/enquete/credentials.enc", root);
	return g_path;
}

/* Cree les dossiers parents en 0700 : le fichier ne doit pas etre lisible
 * par les autres comptes de la machine. */
static bool ensure_parent_dir(void) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", apikey_path());

	char *slash = strrchr(dir, '/');
	if (!slash) return false;
	*slash = '\0';

	/* mkdir -p, segment par segment */
	for (char *p = dir + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		mkdir(dir, 0700);
		*p = '/';
	}
	if (mkdir(dir, 0700) != 0) {
		struct stat sb;
		if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) return false;
	}
	return true;
}

/* Secret propre a la machine ET au compte : un fichier copie ailleurs ne se
 * dechiffre pas. /etc/machine-id est stable ; a defaut on retombe sur le nom
 * d'hote, moins unique mais suffisant pour cet usage. */
static void machine_secret(char *out, size_t out_size) {
	char machine[128] = "";

	FILE *f = fopen("/etc/machine-id", "r");
	if (f) {
		if (!fgets(machine, sizeof(machine), f)) machine[0] = '\0';
		fclose(f);
	}
	if (!machine[0]) {
		if (gethostname(machine, sizeof(machine) - 1) != 0) {
			snprintf(machine, sizeof(machine), "no-machine-id");
		}
		machine[sizeof(machine) - 1] = '\0';
	}
	machine[strcspn(machine, "\r\n")] = '\0';

	snprintf(out, out_size, "%s:%u:%s", machine, (unsigned)getuid(), APP_PEPPER);
}

static bool derive_key(const unsigned char *salt, unsigned char key[32]) {
	char secret[320];
	machine_secret(secret, sizeof(secret));

	int ok = PKCS5_PBKDF2_HMAC(secret, (int)strlen(secret),
	                           salt, SALT_LEN, PBKDF2_ITER,
	                           EVP_sha256(), 32, key);
	/* Le secret ne doit pas trainer en memoire plus que necessaire. */
	OPENSSL_cleanse(secret, sizeof(secret));
	return ok == 1;
}

bool apikey_exists(void) {
	struct stat sb;
	return stat(apikey_path(), &sb) == 0 && sb.st_size > MAGIC_LEN;
}

bool apikey_store(const char *key) {
	if (!key || !*key) return false;
	if (!ensure_parent_dir()) return false;

	unsigned char salt[SALT_LEN], iv[IV_LEN], tag[TAG_LEN], dk[32];
	if (RAND_bytes(salt, SALT_LEN) != 1) return false;
	if (RAND_bytes(iv, IV_LEN) != 1) return false;
	if (!derive_key(salt, dk)) return false;

	size_t key_len = strlen(key);
	unsigned char *cipher = malloc(key_len + 16);
	int cipher_len = 0, part = 0;
	bool ok = false;

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (ctx &&
	    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
	    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1 &&
	    EVP_EncryptInit_ex(ctx, NULL, NULL, dk, iv) == 1 &&
	    EVP_EncryptUpdate(ctx, cipher, &part, (const unsigned char *)key, (int)key_len) == 1) {
		cipher_len = part;
		if (EVP_EncryptFinal_ex(ctx, cipher + cipher_len, &part) == 1) {
			cipher_len += part;
			ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) == 1;
		}
	}
	if (ctx) EVP_CIPHER_CTX_free(ctx);
	OPENSSL_cleanse(dk, sizeof(dk));

	if (!ok) { free(cipher); return false; }

	/* Ecriture par fichier temporaire puis rename : une coupure ne peut pas
	 * laisser un fichier de cle tronque. */
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", apikey_path());

	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { free(cipher); return false; }

	FILE *f = fdopen(fd, "wb");
	if (!f) { close(fd); remove(tmp); free(cipher); return false; }

	ok = fwrite(MAGIC, 1, MAGIC_LEN, f) == MAGIC_LEN
	  && fwrite(salt, 1, SALT_LEN, f) == SALT_LEN
	  && fwrite(iv, 1, IV_LEN, f) == IV_LEN
	  && fwrite(tag, 1, TAG_LEN, f) == TAG_LEN
	  && fwrite(cipher, 1, (size_t)cipher_len, f) == (size_t)cipher_len;
	if (fclose(f) != 0) ok = false;
	free(cipher);

	if (!ok) { remove(tmp); return false; }

	if (rename(tmp, apikey_path()) != 0) { remove(tmp); return false; }
	chmod(apikey_path(), 0600);
	return true;
}

bool apikey_load(char *out, size_t out_size) {
	if (!out || out_size == 0) return false;
	out[0] = '\0';

	FILE *f = fopen(apikey_path(), "rb");
	if (!f) return false;

	char magic[MAGIC_LEN];
	unsigned char salt[SALT_LEN], iv[IV_LEN], tag[TAG_LEN], dk[32];

	if (fread(magic, 1, MAGIC_LEN, f) != MAGIC_LEN ||
	    memcmp(magic, MAGIC, MAGIC_LEN) != 0 ||
	    fread(salt, 1, SALT_LEN, f) != SALT_LEN ||
	    fread(iv, 1, IV_LEN, f) != IV_LEN ||
	    fread(tag, 1, TAG_LEN, f) != TAG_LEN) {
		fclose(f);
		return false;
	}

	unsigned char cipher[APIKEY_MAX + 32];
	size_t cipher_len = fread(cipher, 1, sizeof(cipher), f);
	fclose(f);
	if (cipher_len == 0 || cipher_len >= out_size + 32) return false;

	if (!derive_key(salt, dk)) return false;

	unsigned char plain[APIKEY_MAX + 32];
	int plain_len = 0, part = 0;
	bool ok = false;

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (ctx &&
	    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
	    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, NULL) == 1 &&
	    EVP_DecryptInit_ex(ctx, NULL, NULL, dk, iv) == 1 &&
	    EVP_DecryptUpdate(ctx, plain, &part, cipher, (int)cipher_len) == 1) {
		plain_len = part;
		/* Le tag GCM est verifie ici : mauvaise machine, mauvais compte ou
		 * fichier modifie -> l'appel echoue au lieu de rendre n'importe quoi. */
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, tag) == 1 &&
		    EVP_DecryptFinal_ex(ctx, plain + plain_len, &part) == 1) {
			plain_len += part;
			ok = true;
		}
	}
	if (ctx) EVP_CIPHER_CTX_free(ctx);
	OPENSSL_cleanse(dk, sizeof(dk));

	if (!ok || plain_len <= 0 || (size_t)plain_len >= out_size) {
		OPENSSL_cleanse(plain, sizeof(plain));
		return false;
	}

	memcpy(out, plain, (size_t)plain_len);
	out[plain_len] = '\0';
	OPENSSL_cleanse(plain, sizeof(plain));
	return true;
}

bool apikey_forget(void) {
	return remove(apikey_path()) == 0;
}
