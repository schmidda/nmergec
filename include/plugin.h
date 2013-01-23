/* 
 * File:   plugin.h
 * Author: desmond
 *
 * Created on January 15, 2013, 7:03 AM
 */

#ifndef PLUGIN_H
#define	PLUGIN_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct plugin_struct plugin;
/* function typedefs used in plugin.c */
typedef int (*plugin_process_type)( MVD *mvd, char *options, 
        unsigned char **output );
typedef void (*plugin_help_type)();
typedef void (*plugin_version_type)();
typedef char *(*plugin_name_type)();
typedef int (*plugin_test_type)(int *p,int *f);
plugin *plugin_create( void *handle );
void plugin_dispose( plugin *plug );
int plugin_process( plugin *plug, MVD *mvd, char *options, 
    unsigned char **output );
void plugin_help( plugin *plug );
void plugin_version( plugin *plug );
int plugin_test( plugin *plug, int *passed, int *failed );
char *plugin_name( plugin *plug );

#ifdef	__cplusplus
}
#endif

#endif	/* PLUGIN_H */
