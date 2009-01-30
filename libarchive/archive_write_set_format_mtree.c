/*-
 * Copyright (c) 2009 Michihiro NAKAJIMA
 * Copyright (c) 2008 Joerg Sonnenberger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_OPENSSL_MD5_H
#include <openssl/md5.h>
#else /* HAVE_OPENSSL_MD5_H */
#ifdef HAVE_MD5GLOBAL_H
#include <md5global.h>
#endif
#ifdef HAVE_MD5_H
#include <md5.h>
#endif
#endif /* HAVE_OPENSSL_MD5_H */
#ifdef HAVE_OPENSSL_RIPEMD_H
#include <openssl/ripemd.h>
#else /* HAVE_OPENSSL_RIPEMD_H */
#ifdef HAVE_RIPEMD_H
#include <ripemd.h>
#endif
#ifdef HAVE_RMD160_H
#include <rmd160.h>
#endif
#endif /* HAVE_OPENSSL_RIPEMD_H */
#ifdef HAVE_OPENSSL_SHA_H
#include <openssl/sha.h>
#else /* HAVE_OPENSSL_SHA_H */
#ifdef HAVE_SHA_H
#include <sha.h>
#endif
#ifdef HAVE_SHA1_H
#include <sha1.h>
#endif
#ifdef HAVE_SHA2_H
#include <sha2.h>
#endif
#ifdef HAVE_SHA256_H
#include <sha256.h>
#endif
#endif /* HAVE_OPENSSL_SHA_H */

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_write_private.h"

#define INDENTNAMELEN	15
#define MAXLINELEN	80

struct mtree_writer {
	struct archive_entry *entry;
	struct archive_string ebuf;
	struct archive_string buf;
	int first;
	int need_global_set;
	uint64_t entry_bytes_remaining;
	/* /set value */
	uid_t	set_uid;
	gid_t	set_gid;
	mode_t	set_mode;
	unsigned long set_fflags_set;
	unsigned long set_fflags_clear;
	/* chekc sum */
	int compute_sum;
	uint32_t crc;
	uint64_t crc_len;
#ifdef HAVE_MD5
	MD5_CTX md5ctx;
#endif
#if !defined(HAVE_OPENSSL_RIPEMD_H) && defined(HAVE_RMD160_H)
	RMD160_CTX rmd160ctx;
#else
	RIPEMD160_CTX rmd160ctx;
#endif
#ifdef HAVE_SHA1
#if defined(HAVE_OPENSSL_SHA_H) || defined(HAVE_SHA_H)
	SHA_CTX sha1ctx;
#else
	SHA1_CTX sha1ctx;
#endif
#endif
#ifdef HAVE_SHA256
	SHA256_CTX sha256ctx;
#endif
#ifdef HAVE_SHA384
#if defined(HAVE_OPENSSL_SHA_H)
	SHA512_CTX sha384ctx;
#else
	SHA384_CTX sha384ctx;
#endif
#endif
#ifdef HAVE_SHA512
	SHA512_CTX sha512ctx;
#endif
	/* Keyword options */
	int keys;
#define	F_CKSUM		0x00000001		/* check sum */
#define	F_DEV		0x00000002		/* device type */
#define	F_DONE		0x00000004		/* directory done */
#define	F_FLAGS		0x00000008		/* file flags */
#define	F_GID		0x00000010		/* gid */
#define	F_GNAME		0x00000020		/* group name */
#define	F_IGN		0x00000040		/* ignore */
#define	F_MAGIC		0x00000080		/* name has magic chars */
#define	F_MD5		0x00000100		/* MD5 digest */
#define	F_MODE		0x00000200		/* mode */
#define	F_NLINK		0x00000400		/* number of links */
#define	F_NOCHANGE 	0x00000800		/* If owner/mode "wrong", do
						 * not change */
#define	F_OPT		0x00001000		/* existence optional */
#define	F_RMD160 	0x00002000		/* RIPEMD160 digest */
#define	F_SHA1		0x00004000		/* SHA-1 digest */
#define	F_SIZE		0x00008000		/* size */
#define	F_SLINK		0x00010000		/* symbolic link */
#define	F_TAGS		0x00020000		/* tags */
#define	F_TIME		0x00040000		/* modification time */
#define	F_TYPE		0x00080000		/* file type */
#define	F_UID		0x00100000		/* uid */
#define	F_UNAME		0x00200000		/* user name */
#define	F_VISIT		0x00400000		/* file visited */
#define	F_SHA256	0x00800000		/* SHA-256 digest */
#define	F_SHA384	0x01000000		/* SHA-384 digest */
#define	F_SHA512	0x02000000		/* SHA-512 digest */
};

#define DEFAULT_KEYS	(F_DEV | F_FLAGS | F_GID | F_GNAME | F_SLINK | F_MODE\
			 | F_NLINK | F_SIZE | F_TIME | F_TYPE | F_UID\
			 | F_UNAME)

#define	COMPUTE_CRC(var, ch)	(var) = (var) << 8 ^ crctab[(var) >> 24 ^ (ch)]
static const uint32_t crctab[] = {
	0x0,
	0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
	0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
	0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
	0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
	0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
	0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
	0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
	0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
	0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
	0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
	0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
	0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
	0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
	0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
	0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
	0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
	0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
	0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
	0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
	0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
	0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
	0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
	0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
	0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
	0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
	0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
	0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
	0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
	0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
	0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
	0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
	0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
	0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
	0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
	0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
	0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
	0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
	0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
	0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
	0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
	0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
	0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

static int
mtree_safe_char(char c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return 1;
	if (c >= '0' && c <= '9')
		return 1;
	if (c == 35 || c == 61 || c == 92)
		return 0; /* #, = and \ are always quoted */
	
	if (c >= 33 && c <= 47) /* !"$%&'()*+,-./ */
		return 1;
	if (c >= 58 && c <= 64) /* :;<>?@ */
		return 1;
	if (c >= 91 && c <= 96) /* []^_` */
		return 1;
	if (c >= 123 && c <= 126) /* {|}~ */
		return 1;
	return 0;
}

static void
mtree_quote(struct archive_string *s, const char *str)
{
	const char *start;
	char buf[4];
	unsigned char c;

	for (start = str; *str != '\0'; ++str) {
		if (mtree_safe_char(*str))
			continue;
		if (start != str)
			archive_strncat(s, start, str - start);
		c = (unsigned char)*str;
		buf[0] = '\\';
		buf[1] = (c / 64) + '0';
		buf[2] = (c / 8 % 8) + '0';
		buf[3] = (c % 8) + '0';
		archive_strncat(s, buf, 4);
		start = str + 1;
	}

	if (start != str)
		archive_strncat(s, start, str - start);
}

static void
mtree_ensure_indent(struct mtree_writer *mtree, int final)
{
	int i;
	const char *r, *s, *x;

	if (!final) {
		if (mtree->ebuf.length > INDENTNAMELEN) {
			archive_string_concat(&mtree->buf, &mtree->ebuf);
			archive_strncat(&mtree->buf, " \\\n", 3);
			archive_string_empty(&mtree->ebuf);
		}
		for (i = mtree->ebuf.length; i < INDENTNAMELEN; i++)
			archive_strappend_char(&mtree->ebuf, ' ');
		return;
	}

	s = mtree->ebuf.s;
	x = NULL;
	if (mtree->ebuf.length <= INDENTNAMELEN)
		r = NULL;
	else
		r = strchr(s + INDENTNAMELEN + 1, ' ');
	while (r != NULL) {
		if (r - s <= MAXLINELEN - 3)
			x = r++;
		else {
			if (x == NULL)
				x = r;
			archive_strncat(&mtree->buf, s, x - s);
			archive_strncat(&mtree->buf, " \\\n", 3);
			for (i = 0; i < (INDENTNAMELEN + 1); i++)
				archive_strappend_char(&mtree->buf, ' ');
			s = r = ++x;
			x = NULL;
		}
		r = strchr(r, ' ');
	}
	if (x != NULL && strlen(s) > MAXLINELEN - 3) {
		archive_strncat(&mtree->buf, s, x - s);
		archive_strncat(&mtree->buf, " \\\n", 3);
		for (i = 0; i < (INDENTNAMELEN + 1); i++)
			archive_strappend_char(&mtree->buf, ' ');
		s = ++x;
	}
	archive_strcat(&mtree->buf, s);
	archive_string_empty(&mtree->ebuf);
}

static int
archive_write_mtree_header(struct archive_write *a,
    struct archive_entry *entry)
{
	struct mtree_writer *mtree= a->format_data;
	const char *path;
	const char *name;

	mtree->entry = archive_entry_clone(entry);
	path = archive_entry_pathname(mtree->entry);

	if (mtree->first) {
		mtree->first = 0;
		archive_strcat(&mtree->buf, "#mtree\n");
	}
	if (mtree->need_global_set && archive_entry_filetype(entry) == AE_IFREG) {
		mtree->need_global_set = 0;
		if (mtree->keys & (F_FLAGS | F_GID | F_GNAME | F_NLINK | F_MODE |
		    F_TYPE | F_UID | F_UNAME)) {
			struct archive_string setstr;

			archive_string_init(&setstr);
			if ((mtree->keys & F_TYPE) != 0)
				archive_strcat(&setstr, " type=file");
			if ((mtree->keys & F_UNAME) != 0 &&
			    (name = archive_entry_uname(entry)) != NULL) {
				archive_strcat(&setstr, " uname=");
				mtree_quote(&setstr, name);
			}
			mtree->set_uid = archive_entry_uid(entry);
			if ((mtree->keys & F_UID) != 0)
				archive_string_sprintf(&setstr, " uid=%jd",
				    (intmax_t)mtree->set_uid);
			if ((mtree->keys & F_GNAME) != 0 &&
			    (name = archive_entry_gname(entry)) != NULL) {
				archive_strcat(&setstr, " gname=");
				mtree_quote(&setstr, name);
			}
			mtree->set_gid = archive_entry_gid(entry);
			if ((mtree->keys & F_GID) != 0)
				archive_string_sprintf(&setstr, " gid=%jd",
				    (intmax_t)mtree->set_gid);
			mtree->set_mode = archive_entry_mode(entry) & 07777;
			if ((mtree->keys & F_MODE) != 0)
				archive_string_sprintf(&setstr, " mode=%o",
				    mtree->set_mode);
			if ((mtree->keys & F_NLINK) != 0)
				archive_strcat(&setstr, " nlink=1");
			if ((mtree->keys & F_FLAGS) != 0 &&
			    (name = archive_entry_fflags_text(entry)) != NULL) {
				archive_strcat(&setstr, " flags=");
				mtree_quote(&setstr, name);
			}
			archive_entry_fflags(entry, &mtree->set_fflags_set,
			    &mtree->set_fflags_clear);

			if (setstr.length > 0)
				archive_string_sprintf(&mtree->buf, "/set%s\n",
				    setstr.s);
			archive_string_free(&setstr);
		}
	}

	archive_string_empty(&mtree->ebuf);
	mtree_quote(&mtree->ebuf, path);
	mtree_ensure_indent(mtree, 0);

	mtree->entry_bytes_remaining = archive_entry_size(entry);
	if ((mtree->keys & F_CKSUM) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_CKSUM;
		mtree->crc = 0;
		mtree->crc_len = 0;
	} else
		mtree->compute_sum &= ~F_CKSUM;
#ifdef HAVE_MD5
	if ((mtree->keys & F_MD5) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_MD5;
		MD5_Init(&mtree->md5ctx);
	} else
		mtree->compute_sum &= ~F_MD5;
#endif
#ifdef HAVE_RMD160
	if ((mtree->keys & F_RMD160) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_RMD160;
		RIPEMD160_Init(&mtree->rmd160ctx);
	} else
		mtree->compute_sum &= ~F_RMD160;
#endif
#ifdef HAVE_SHA1
	if ((mtree->keys & F_SHA1) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_SHA1;
		SHA1_Init(&mtree->sha1ctx);
	} else
		mtree->compute_sum &= ~F_SHA1;
#endif
#ifdef HAVE_SHA256
	if ((mtree->keys & F_SHA256) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_SHA256;
		SHA256_Init(&mtree->sha256ctx);
	} else
		mtree->compute_sum &= ~F_SHA256;
#endif
#ifdef HAVE_SHA384
	if ((mtree->keys & F_SHA384) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_SHA384;
		SHA384_Init(&mtree->sha384ctx);
	} else
		mtree->compute_sum &= ~F_SHA384;
#endif
#ifdef HAVE_SHA512
	if ((mtree->keys & F_SHA512) != 0 &&
	    archive_entry_filetype(entry) == AE_IFREG) {
		mtree->compute_sum |= F_SHA512;
		SHA512_Init(&mtree->sha512ctx);
	} else
		mtree->compute_sum &= ~F_SHA512;
#endif

	return (ARCHIVE_OK);
}

#if defined(HAVE_MD5) || defined(HAVE_RMD160) || defined(HAVE_SHA1) || defined(HAVE_SHA256) || defined(HAVE_SHA384) || defined(HAVE_SHA512)
static void
strappend_bin(struct archive_string *s, const unsigned char *bin, int n)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < n; i++) {
		archive_strappend_char(s, hex[bin[i] >> 4]);
		archive_strappend_char(s, hex[bin[i] & 0x0f]);
	}
}
#endif

static int
archive_write_mtree_finish_entry(struct archive_write *a)
{
	struct mtree_writer *mtree = a->format_data;
	struct archive_entry *entry;
	const char *name;
	int ret;

	entry = mtree->entry;
	if (entry == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_PROGRAMMER,
		    "Finished entry without being open first.");
		return (ARCHIVE_FATAL);
	}
	mtree->entry = NULL;

	if ((mtree->keys & F_NLINK) != 0 &&
	    archive_entry_nlink(entry) != 1 && 
	    archive_entry_filetype(entry) != AE_IFDIR)
		archive_string_sprintf(&mtree->ebuf,
		    " nlink=%u", archive_entry_nlink(entry));

	if ((mtree->keys & F_GNAME) != 0 &&
	    mtree->set_gid != archive_entry_gid(entry) &&
	    (name = archive_entry_gname(entry)) != NULL) {
		archive_strcat(&mtree->ebuf, " gname=");
		mtree_quote(&mtree->ebuf, name);
	}
	if ((mtree->keys & F_UNAME) != 0 &&
	    mtree->set_uid != archive_entry_uid(entry) &&
	    (name = archive_entry_uname(entry)) != NULL) {
		archive_strcat(&mtree->ebuf, " uname=");
		mtree_quote(&mtree->ebuf, name);
	}
	if ((mtree->keys & F_FLAGS) != 0) {
		unsigned long set, clear;

		archive_entry_fflags(entry, &set, &clear);
		if ((mtree->set_fflags_set != set ||
		    mtree->set_fflags_clear != clear) &&
		    (name = archive_entry_fflags_text(entry)) != NULL) {
			archive_strcat(&mtree->ebuf, " flags=");
			mtree_quote(&mtree->ebuf, name);
		}
	}
	if ((mtree->keys & F_TIME) != 0)
		archive_string_sprintf(&mtree->ebuf, " time=%jd.%jd",
		    (intmax_t)archive_entry_mtime(entry),
		    (intmax_t)archive_entry_mtime_nsec(entry));
	if ((mtree->keys & F_MODE) != 0 &&
	    mtree->set_mode != (archive_entry_mode(entry) & 07777))
		archive_string_sprintf(&mtree->ebuf, " mode=%o",
		    archive_entry_mode(entry) & 07777);
	if ((mtree->keys & F_GID) != 0 &&
	    mtree->set_gid != archive_entry_gid(entry))
		archive_string_sprintf(&mtree->ebuf, " gid=%jd",
		    (intmax_t)archive_entry_gid(entry));
	if ((mtree->keys & F_UID) != 0 &&
	    mtree->set_uid != archive_entry_uid(entry))
		archive_string_sprintf(&mtree->ebuf, " uid=%jd",
		    (intmax_t)archive_entry_uid(entry));

	switch (archive_entry_filetype(entry)) {
	case AE_IFLNK:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=link");
		if ((mtree->keys & F_SLINK) != 0) {
			archive_strcat(&mtree->ebuf, " link=");
			mtree_quote(&mtree->ebuf, archive_entry_symlink(entry));
		}
		break;
	case AE_IFSOCK:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=socket");
		break;
	case AE_IFCHR:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=char");
		if ((mtree->keys & F_DEV) != 0) {
			archive_string_sprintf(&mtree->ebuf,
			    " device=native,%d,%d",
			    archive_entry_rdevmajor(entry),
			    archive_entry_rdevminor(entry));
		}
		break;
	case AE_IFBLK:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=block");
		if ((mtree->keys & F_DEV) != 0) {
			archive_string_sprintf(&mtree->ebuf,
			    " device=native,%d,%d",
			    archive_entry_rdevmajor(entry),
			    archive_entry_rdevminor(entry));
		}
		break;
	case AE_IFDIR:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=dir");
		break;
	case AE_IFIFO:
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=fifo");
		break;
	case AE_IFREG:
	default:	/* Handle unknown file types as regular files. */
#if 0
		if ((mtree->keys & F_TYPE) != 0)
			archive_strcat(&mtree->ebuf, " type=file");
#endif
		if ((mtree->keys & F_SIZE) != 0)
			archive_string_sprintf(&mtree->ebuf, " size=%jd",
			    (intmax_t)archive_entry_size(entry));
		break;
	}

	if (mtree->compute_sum & F_CKSUM) {
		uint64_t len;
		/* Include the length of the file. */
		for (len = mtree->crc_len; len != 0; len >>= 8)
			COMPUTE_CRC(mtree->crc, len & 0xff);
		mtree->crc = ~mtree->crc;
		archive_string_sprintf(&mtree->ebuf, " cksum=%ju",
		    (uintmax_t)mtree->crc);
	}
#ifdef HAVE_MD5
	if (mtree->compute_sum & F_MD5) {
		unsigned char buf[16];

		MD5_Final(buf, &mtree->md5ctx);
		archive_strcat(&mtree->ebuf, " md5digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
#ifdef HAVE_RMD160
	if (mtree->compute_sum & F_RMD160) {
		unsigned char buf[20];

		RIPEMD160_Final(buf, &mtree->rmd160ctx);
		archive_strcat(&mtree->ebuf, " rmd160digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
#ifdef HAVE_SHA1
	if (mtree->compute_sum & F_SHA1) {
		unsigned char buf[20];

		SHA1_Final(buf, &mtree->sha1ctx);
		archive_strcat(&mtree->ebuf, " sha1digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
#ifdef HAVE_SHA256
	if (mtree->compute_sum & F_SHA256) {
		unsigned char buf[32];

		SHA256_Final(buf, &mtree->sha256ctx);
		archive_strcat(&mtree->ebuf, " sha256digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
#ifdef HAVE_SHA384
	if (mtree->compute_sum & F_SHA384) {
		unsigned char buf[48];

		SHA384_Final(buf, &mtree->sha384ctx);
		archive_strcat(&mtree->ebuf, " sha384digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
#ifdef HAVE_SHA512
	if (mtree->compute_sum & F_SHA512) {
		unsigned char buf[64];

		SHA512_Final(buf, &mtree->sha512ctx);
		archive_strcat(&mtree->ebuf, " sha512digest=");
		strappend_bin(&mtree->ebuf, buf, sizeof(buf));
	}
#endif
	archive_strcat(&mtree->ebuf, "\n");
	mtree_ensure_indent(mtree, 1);

	archive_entry_free(entry);

	if (mtree->buf.length > 32768) {
		ret = (a->compressor.write)(a, mtree->buf.s, mtree->buf.length);
		archive_string_empty(&mtree->buf);
	} else
		ret = ARCHIVE_OK;

	return (ret == ARCHIVE_OK ? ret : ARCHIVE_FATAL);
}

static int
archive_write_mtree_finish(struct archive_write *a)
{
	struct mtree_writer *mtree= a->format_data;

	archive_write_set_bytes_in_last_block(&a->archive, 1);

	return (a->compressor.write)(a, mtree->buf.s, mtree->buf.length);
}

static ssize_t
archive_write_mtree_data(struct archive_write *a, const void *buff, size_t n)
{
	struct mtree_writer *mtree= a->format_data;

	if (n > mtree->entry_bytes_remaining)
		n = mtree->entry_bytes_remaining;
	if (mtree->compute_sum & F_CKSUM) {
		/*
		 * Compute a POSIX 1003.2 checksum
		 */
		const unsigned char *p;
		int nn;

		for (nn = n, p = buff; nn--; ++p)
			COMPUTE_CRC(mtree->crc, *p);
		mtree->crc_len += n;
	}
#ifdef HAVE_MD5
	if (mtree->compute_sum & F_MD5)
		MD5_Update(&mtree->md5ctx, buff, n);
#endif
#ifdef HAVE_RMD160
	if (mtree->compute_sum & F_RMD160)
		RIPEMD160_Update(&mtree->rmd160ctx, buff, n);
#endif
#ifdef HAVE_SHA1
	if (mtree->compute_sum & F_SHA1)
		SHA1_Update(&mtree->sha1ctx, buff, n);
#endif
#ifdef HAVE_SHA256
	if (mtree->compute_sum & F_SHA256)
		SHA256_Update(&mtree->sha256ctx, buff, n);
#endif
#ifdef HAVE_SHA384
	if (mtree->compute_sum & F_SHA384)
		SHA384_Update(&mtree->sha384ctx, buff, n);
#endif
#ifdef HAVE_SHA512
	if (mtree->compute_sum & F_SHA512)
		SHA512_Update(&mtree->sha512ctx, buff, n);
#endif
	return n;
}

static int
archive_write_mtree_destroy(struct archive_write *a)
{
	struct mtree_writer *mtree= a->format_data;

	if (mtree == NULL)
		return (ARCHIVE_OK);

	archive_entry_free(mtree->entry);
	archive_string_free(&mtree->ebuf);
	archive_string_free(&mtree->buf);
	free(mtree);
	a->format_data = NULL;
	return (ARCHIVE_OK);
}

static int
archive_write_mtree_options(struct archive_write *a, const char *key,
    const char *value)
{
	struct mtree_writer *mtree= a->format_data;
	int keybit = 0;

	switch (key[0]) {
	case 'a':
		if (strcmp(key, "all") == 0)
			keybit = -1;
		break;
	case 'c':
		if (strcmp(key, "cksum") == 0)
			keybit = F_CKSUM;
		break;
	case 'd':
		if (strcmp(key, "device") == 0)
			keybit = F_DEV;
		break;
	case 'f':
		if (strcmp(key, "flags") == 0)
			keybit = F_FLAGS;
		break;
	case 'g':
		if (strcmp(key, "gid") == 0)
			keybit = F_GID;
		else if (strcmp(key, "gname") == 0)
			keybit = F_GNAME;
		break;
	case 'l':
		if (strcmp(key, "link") == 0)
			keybit = F_SLINK;
		break;
	case 'm':
#ifdef HAVE_MD5
		if (strcmp(key, "md5") == 0 ||
		    strcmp(key, "md5digest") == 0)
			keybit = F_MD5;
#endif
		if (strcmp(key, "mode") == 0)
			keybit = F_MODE;
		break;
	case 'n':
		if (strcmp(key, "nlink") == 0)
			keybit = F_NLINK;
		break;
#ifdef HAVE_RMD160
	case 'r':
		if (strcmp(key, "ripemd160digest") == 0 ||
		    strcmp(key, "rmd160") == 0 ||
		    strcmp(key, "rmd160digest") == 0)
			keybit = F_RMD160;
		break;
#endif
	case 's':
#ifdef HAVE_SHA1
		if (strcmp(key, "sha1") == 0 ||
		    strcmp(key, "sha1digest") == 0)
			keybit = F_SHA1;
#endif
#ifdef HAVE_SHA256
		if (strcmp(key, "sha256") == 0 ||
		    strcmp(key, "sha256digest") == 0)
			keybit = F_SHA256;
#endif
#ifdef HAVE_SHA384
		if (strcmp(key, "sha384") == 0 ||
		    strcmp(key, "sha384digest") == 0)
			keybit = F_SHA384;
#endif
#ifdef HAVE_SHA384
		if (strcmp(key, "sha512") == 0 ||
		    strcmp(key, "sha512digest") == 0)
			keybit = F_SHA512;
#endif
		if (strcmp(key, "size") == 0)
			keybit = F_SIZE;
		break;
	case 't':
		if (strcmp(key, "time") == 0)
			keybit = F_TIME;
		else if (strcmp(key, "type") == 0)
			keybit = F_TYPE;
		break;
	case 'u':
		if (strcmp(key, "uid") == 0)
			keybit = F_UID;
		else if (strcmp(key, "uname") == 0)
			keybit = F_UNAME;
		break;
	}
	if (keybit != 0) {
		if (value != NULL)
			mtree->keys |= keybit;
		else
			mtree->keys &= ~keybit;
		return (ARCHIVE_OK);
	}

	return (ARCHIVE_WARN);
}

int
archive_write_set_format_mtree(struct archive *_a)
{
	struct archive_write *a = (struct archive_write *)_a;
	struct mtree_writer *mtree;

	if (a->format_destroy != NULL)
		(a->format_destroy)(a);

	if ((mtree = malloc(sizeof(*mtree))) == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate mtree data");
		return (ARCHIVE_FATAL);
	}

	mtree->entry = NULL;
	mtree->first = 1;
	mtree->need_global_set = 1;
	mtree->keys = DEFAULT_KEYS;
	archive_string_init(&mtree->ebuf);
	archive_string_init(&mtree->buf);
	a->format_data = mtree;
	a->format_destroy = archive_write_mtree_destroy;

	a->pad_uncompressed = 0;
	a->format_name = "mtree";
	a->format_options = archive_write_mtree_options;
	a->format_write_header = archive_write_mtree_header;
	a->format_finish = archive_write_mtree_finish;
	a->format_write_data = archive_write_mtree_data;
	a->format_finish_entry = archive_write_mtree_finish_entry;
	a->archive.archive_format = ARCHIVE_FORMAT_MTREE;
	a->archive.archive_format_name = "mtree";

	return (ARCHIVE_OK);
}
