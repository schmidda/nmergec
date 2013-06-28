#include <stdio.h>
#include "bitset.h"
#include "unicode/uchar.h"
#include "link_node.h"
#include "version.h"
#include "pair.h"
#include "dyn_array.h"
#include "mvd.h"
#include "plugin.h"

/**
 * Do the work of this plugin
 * @param mvd the mvd to manipulte or read
 * @param options a string contianing the plugin's options
 * @param output buffer of length SCRATCH_LEN
 * @return 1 if the process completed successfully
 */
int process( MVD **mvd, char *options, unsigned char *output,
    unsigned char *data, size_t data_len )
{
    return 1;
}
int changes()
{
    return 0;
}
/**
 * Print a help message to stdout explaining what the paramerts are
 */
char *help()
{
    return "help\n";
}
/**
 * Report the plugin's version and author to stdout
 */
char *plug_version()
{
    "version 0.1 (c) 2013 Desmond Schmidt\n";
}
/**
 * Explain what we do
 * @return a string
 */
char *description()
{
    return "export an mvd to XML\n";
}
/**
 * Report the plugin's name
 * @return a string being it's canonical name
 */
char *name()
{
    return "export";
}
/**
 * Test the plugin
 * @param p VAR param update number of passed tests
 * @param f VAR param update number of failed tests
 * @return 1 if all tests passed else 0
 */
int test(int *p,int *f)
{
    return 1;
}
