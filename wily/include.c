/*******************************************
 *	Expand b3-clicks to "include" files, e.g. <stdio.h>
 *******************************************/

#include "wily.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

static char *   pathfind (const char *paths, const char *file);
static bool is_includebrackets(char left, char right);

/*
 * The user has selected 'r' in 'v'.
 * If possible, open an appropriate include file, return a View
 * representing its body.
 *
 * If no include file is appropriate, return 0.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

View*
openinclude(View *v, Range r)
{
	Range    expanded;
	char     *buf = NULL;
	char     *s = NULL;
	View     *result_view = NULL;
	int      len;
	Text     *t;

	t = view_text(v);
	expanded = text_expand(t, r, notinclude);
	len = RLEN(expanded);

	/* Minimum 3 chars: < x > */
	if (len < 3)
		return NULL;

	/* Allocate buffer for the raw selection */
	buf = malloc(len + 1);
	if (!buf)
		return NULL;

	len = text_copyutf(t, expanded, buf);
	buf[len] = '\0';

	/* Check for enclosing brackets <...> or "..." */
	if (!is_includebrackets(buf[0], buf[len-1]))
	{
		free(buf);
		return NULL;
	}

	/* Null-terminate before the closing bracket to isolate the filename */
	buf[len-1] = '\0';
	char *filename = buf + 1;

	/* 1. Try to find path in $INCLUDES */
	s = pathfind(getenv("INCLUDES"), filename);

	/* 2. Fall back to /usr/include if pathfind fails */
	if (!s)
	{
		if (asprintf(&s, "/usr/include/%s", filename) == -1)
		{
			free(buf);
			return NULL;
		}
		/* openlabel is called with 's' from asprintf, must free 's' after */
		result_view = openlabel(s, false);
		free(s);
	}
	else
	{
		/* s was returned by pathfind; assuming pathfind returns a static
		   or allocated buffer that openlabel handles or doesn't own. */
		result_view = openlabel(s, false);
		// free(s); // Uncomment if your pathfind returns heap memory
	}

	free(buf);
	return result_view;
}


/**********************************************************
	static functions
**********************************************************/

static bool
is_includebrackets(char left, char right)
{
	return (left == '"' && right == '"') ||
		(left == '<' && right == '>');
}


static const char *
nextstr (const char *p, const char *c, int *n)
{
	int i;

	if (!p || !*p)
		return 0;

	*n = i = strcspn (p, c);	 /* XXX - utf */
	if (p[i])
		i += 1;					 /* strspn (p+i, c); ? */
	return p+i;
}


static char *
pathfind (const char *paths, const char *file)
{
	const char *p,*ptmp;
	int flen;
	int plen;

	if (!paths || !file)
		return 0;

	flen = strlen(file);
	p = paths;
	while((ptmp = nextstr(p, ":", &plen))!= 0)
	{
		int fd;
		char *tmp = malloc(plen+1+flen+1);

		if (tmp)
		{
			sprintf(tmp, "%.*s/%s", plen, p, file);
			if ((fd = open(tmp, 0)) < 0)
			{
				free(tmp);
			}
			else
			{
				close(fd);
				return tmp;
			}
		}
		p = ptmp;
	}
	return 0;
}
