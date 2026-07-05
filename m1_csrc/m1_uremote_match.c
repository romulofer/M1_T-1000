/* See COPYING.txt for license details. */

/*
 *
 * m1_uremote_match.c
 *
 * Implementation of the pure Universal Remotes record-name matcher.
 * See m1_uremote_match.h for the contract.
 *
 * M1 Project
 *
 */

#include "m1_uremote_match.h"

#include <stddef.h>

/* Case-insensitive full-string equality. NULL operands compare non-equal.
 * ASCII-only fold (record names are ASCII Flipper identifiers). */
static bool ci_equal(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;

	while (*a != '\0' && *b != '\0')
	{
		unsigned char ca = (unsigned char)*a;
		unsigned char cb = (unsigned char)*b;

		if (ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb - 'A' + 'a');

		if (ca != cb)
			return false;

		a++;
		b++;
	}

	/* Match only if BOTH ended together (full-string, no prefix match). */
	return (*a == '\0') && (*b == '\0');
}

bool uremote_name_matches(const char *name,
                          const char *record,
                          const char *const *aliases)
{
	if (name == NULL)
		return false;

	if (ci_equal(name, record))
		return true;

	if (aliases != NULL)
	{
		for (const char *const *a = aliases; *a != NULL; a++)
		{
			if (ci_equal(name, *a))
				return true;
		}
	}

	return false;
}
