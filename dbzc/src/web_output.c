#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standalone tests supply the same allocator interface without the game. */
#ifndef WEB_OUTPUT_TEST
#include "mud.h"
#endif

/*
 * Convert source text once, without ever scanning generated HTML as input.
 * The caller owns one STRALLOC reference and must release it with STRFREE.
 * Empty input is valid; NULL input and allocation failure return NULL.
 */
char *htmlcolor( const char *src )
{
    static const char codes[] = "xrgObpcwzRGYBPCW";
    static const char *colors[] = {
        "#333333", "#400000", "#004000", "#404000",
        "#000040", "#404000", "#004040", "#FFFFFF",
        "#404040", "#FF0000", "#00FF00", "#FFFF00",
        "#0000FF", "#FF00FF", "#00FFFF", "#C0C0C0"
    };
    static const char prefix[] = "</font><font color='";
    static const char tag[] = "</font><font color='#333333'>";
    size_t length;
    char *buffer, *out, *result;
    const char *code, *replacement;
    size_t count;

    if ( !src )
    {
        bug( "htmlcolor: NULL input" );
        return NULL;
    }

    length = strlen( src );
    /* Every input byte expands to at most one tag. Include the terminator. */
    if ( length > ((size_t)-1 - 1) / (sizeof(tag) - 1) )
    {
        bug( "htmlcolor: input too large" );
        return NULL;
    }
    buffer = malloc( length * (sizeof(tag) - 1) + 1 );
    if ( !buffer )
    {
        bug( "htmlcolor: out of memory" );
        return NULL;
    }

    out = buffer;
    while ( *src )
    {
        if ( *src == '&' && src[1] && (code = strchr( codes, src[1] )) )
        {
            memcpy( out, prefix, sizeof(prefix) - 1 );
            out += sizeof(prefix) - 1;
            memcpy( out, colors[code - codes], 7 );
            out += 7;
            memcpy( out, "'>", 2 );
            out += 2;
            src += 2;
            continue;
        }
        switch ( *src )
        {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        case '\r': ++src; continue;
        default: *out++ = *src++; continue;
        }
        count = strlen( replacement );
        memcpy( out, replacement, count );
        out += count;
        ++src;
    }
    *out = '\0';
    result = STRALLOC( buffer );
    free( buffer );
    if ( !result )
        bug( "htmlcolor: STRALLOC failed" );
    return result;
}

/* Write text as data, never as a printf format; release even on I/O failure. */
int write_htmlcolor( FILE *stream, const char *src )
{
    char *html = htmlcolor( src );
    int result;

    if ( !html )
        return EOF;
    result = fputs( html, stream );
    STRFREE( html );
    return result;
}
