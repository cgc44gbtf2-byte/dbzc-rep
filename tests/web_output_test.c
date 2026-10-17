/* Compile the production converter with a tracked, standalone STRALLOC stub. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t outstanding;
static unsigned int errors;
static int fail_allocation;

static char *test_stralloc( const char *text )
{
    char *copy;

    if ( fail_allocation )
        return NULL;
    copy = malloc( strlen(text) + 1 );
    assert( copy );
    strcpy( copy, text );
    ++outstanding;
    return copy;
}

static void test_strfree( char *text )
{
    assert( text && outstanding );
    --outstanding;
    free( text );
}

static void bug( const char *format, ... )
{
    assert( format );
    ++errors;
}

#define WEB_OUTPUT_TEST
#define STRALLOC(text) test_stralloc(text)
#define STRFREE(text) do { test_strfree(text); (text) = NULL; } while (0)
#include "../dbzc/src/web_output.c"

static void expect( const char *input, const char *expected )
{
    char *actual = htmlcolor( input );

    assert( actual );
    assert( strcmp(actual, expected) == 0 );
    assert( outstanding == 1 );
    STRFREE( actual );
    assert( outstanding == 0 );
}

static void test_colors( void )
{
    static const char codes[] = "xrgObpcwzRGYBPCW";
    static const char *colors[] = {
        "#333333", "#400000", "#004000", "#404000",
        "#000040", "#404000", "#004040", "#FFFFFF",
        "#404040", "#FF0000", "#00FF00", "#FFFF00",
        "#0000FF", "#FF00FF", "#00FFFF", "#C0C0C0"
    };
    char input[3], expected[64];
    size_t i;

    for ( i = 0; i < sizeof(codes) - 1; ++i )
    {
        input[0] = '&';
        input[1] = codes[i];
        input[2] = '\0';
        snprintf( expected, sizeof(expected), "</font><font color='%s'>", colors[i] );
        expect( input, expected );
    }
    expect( "&R&R", "</font><font color='#FF0000'></font><font color='#FF0000'>" );
}

static void test_large( void )
{
    static const char pattern[] = "&R<>&?%n\r\n";
    static const char converted[] = "</font><font color='#FF0000'>&lt;&gt;&amp;?%n\n";
    size_t repeats = 100000, i;
    char *input = malloc( repeats * (sizeof(pattern) - 1) + 1 );
    char *actual;

    assert( input );
    for ( i = 0; i < repeats; ++i )
        memcpy( input + i * (sizeof(pattern) - 1), pattern, sizeof(pattern) - 1 );
    input[repeats * (sizeof(pattern) - 1)] = '\0';
    actual = htmlcolor( input );
    assert( actual );
    assert( strlen(actual) == repeats * (sizeof(converted) - 1) );
    for ( i = 0; i < repeats; ++i )
        assert( memcmp(actual + i * (sizeof(converted) - 1),
                       converted, sizeof(converted) - 1) == 0 );
    STRFREE( actual );
    free( input );
    assert( outstanding == 0 );
}

static void test_writer( void )
{
    static const char percent[] = "100% %s %n %08x %%";
    FILE *stream = tmpfile();
    char buffer[128];
    int i;

    assert( stream );
    assert( write_htmlcolor(stream, percent) >= 0 );
    rewind( stream );
    assert( fgets(buffer, sizeof(buffer), stream) );
    assert( strcmp(buffer, percent) == 0 );
    assert( outstanding == 0 );
    fclose( stream );

    stream = fopen( "/dev/null", "w" );
    assert( stream );
    for ( i = 0; i < 5000; ++i )
    {
        assert( write_htmlcolor(stream, "&W100% <&q>\r\n") >= 0 );
        assert( outstanding == 0 );
    }
    fclose( stream );

    stream = fopen( "/dev/full", "w" );
    assert( stream );
    assert( setvbuf(stream, NULL, _IONBF, 0) == 0 );
    assert( write_htmlcolor(stream, "test %n") == EOF );
    assert( outstanding == 0 );
    fclose( stream );

    fail_allocation = 1;
    stream = tmpfile();
    assert( stream );
    assert( write_htmlcolor(stream, "allocation failure") == EOF );
    assert( outstanding == 0 );
    assert( ftell(stream) == 0 );
    fclose( stream );
    fail_allocation = 0;
}

int main( void )
{
    expect( "", "" );
    expect( "100% %n %s %%", "100% %n %s %%" );
    expect( "<script>&q & &amp; >", "&lt;script&gt;&amp;q &amp; &amp;amp; &gt;" );
    expect( "trailing&", "trailing&amp;" );
    expect( "a\r\nb\rc", "a\nbc" );
    test_colors();
    test_large();
    test_writer();
    assert( htmlcolor(NULL) == NULL );
    assert( errors == 2 );
    assert( outstanding == 0 );
    puts( "web_output tests passed" );
    return 0;
}
