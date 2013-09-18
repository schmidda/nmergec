/*
 *  NMergeC is Copyright 2013 Desmond Schmidt
 * 
 *  This file is part of NMergeC. NMergeC is a C commandline tool and 
 *  static library and a collection of dynamic libraries for merging 
 *  multiple versions into multi-version documents (MVDs), and for 
 *  reading, searching and comparing them.
 *
 *  NMergeC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NMergeC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unicode/uchar.h"
#include "bitset.h"
#include "link_node.h"
#include "pair.h"
#include "plugin_log.h"
#include "dyn_array.h"
#include "linkpair.h"
#include "hashmap.h"

#define IMPLICIT_SIZE 12
/**
 * A linkpair is necessary because when creating an MVD by using
 * only a list of fragments we need to insert new pairs We can't
 * do this using an array without introducing inefficiencies and 
 * greatly complicating references to existing pairs by indices 
 * which may change all the time. So we wrap each pair in a 
 * 'linkpair', which increases memory usage but solves the problem.
 * A linkpair implements a doubly-linked list of pairs.
 */
struct linkpair_struct
{
    // the original pair from the mvd pairs array
    pair *p;
    // doubly-linked list
    linkpair *left;
    linkpair *right;
    // absolute offset in suffixtree text if aligned to new version
    int st_off;
};
linkpair *linkpair_create( pair *p, plugin_log *log )
{
    linkpair *lp = calloc( 1, sizeof(linkpair) );
    if ( lp != NULL )
        lp->p = p;
    else
        plugin_log_add( log, "linkpair: failed to create object\n");
    return lp;
}
/**
 * Just dispose of us
 * @param lp the linkpair object to free
 */
void linkpair_dispose( linkpair *lp )
{
    free( lp );
}
/**
 * Set the left pointer
 * @param lp the linkpair to set it for
 * @param left the next pair on the left
 */
void linkpair_set_left( linkpair *lp, linkpair *left )
{
    lp->left = left;
}
/**
 * Set the right pointer
 * @param lp the linkpair to set it for
 * @param right the next pair on the right
 */
void linkpair_set_right( linkpair *lp, linkpair *right )
{
    lp->right = right;
}
/**
 * Get the left pointer
 * @param lp the linkpair in question
 * @return the next pair on the left
 */
linkpair *linkpair_left( linkpair *lp )
{
    return lp->left;
}
/**
 * Get the right pointer
 * @param lp the linkpair in question
 * @return the next pair on the right
 */
linkpair *linkpair_right( linkpair *lp )
{
    return lp->right;
}
/**
 * Replace one linkpair with another in the list
 * @param old_lp the old linkpair in a list
 * @param new_lp the new linkpair to add to the same list
 */
void linkpair_replace( linkpair *old_lp, linkpair *new_lp )
{
    if ( old_lp->left != NULL )
        old_lp->left->right = new_lp;
    if ( old_lp->right != NULL )
        old_lp->right->left = new_lp;
    new_lp->right = old_lp->right;
    new_lp->left = old_lp->left;
    old_lp->left = old_lp->right = NULL;
}
/**
 * Set the index into the suffixtree text
 * @param lp the linkpair to set it for
 * @param st_off the index
 */
void linkpair_set_st_off( linkpair *lp, int st_off )
{
    lp->st_off = st_off;
}
/**
 * Get the index into the suffixtree text
 * @param lp the linkpair to set it for
 * @return the index
 */
int linkpair_st_off( linkpair *lp )
{
    return lp->st_off;
}
/**
 * Get the pair associated with this linkpair
 * @param lp the linkpair
 * @return its pair
 */
pair *linkpair_pair( linkpair *lp )
{
    return lp->p;
}
/**
 * Is a linkpair the trailing arc of a node (or a hint)?
 * @param lp the linkpair to test
 * @return 1 if it is, else 0
 */
int linkpair_trailing_node( linkpair *lp )
{
    if ( lp->left != NULL )
    {
        if ( pair_is_hint(lp->left->p)
            || bitset_intersects(pair_versions(lp->left->p),
            pair_versions(lp->p)) )
            return 1;
    }
    return 0;
}
/**
 * Get the next pair in an array
 * @param pairs a list of linkpairs
 * @param lp the first linkpair to look at
 * @param bs the bitset of versions to follow
 * @return NULL on failure else the the next relevant linkpair
 */
linkpair *linkpair_next( linkpair *lp, bitset *bs )
{
    lp = lp->right;
    while ( lp != NULL 
        && !bitset_intersects(bs,pair_versions(linkpair_pair(lp))) )
        lp = linkpair_right(lp);
    return lp;
}
/**
 * Is this linkpair free? i.e. is it not the trailing pair of a node?
 * @param lp the linkpair to test
 * @return 1 if it is free else 0
 */
int linkpair_free( linkpair *lp )
{
    int res = 1;
    if ( lp->left != NULL )
    {
        pair *p = linkpair_pair(lp->left);
        if ( !pair_is_hint(p) )
        {
            pair *q = linkpair_pair(lp);
            char str1[34];
            char str2[34];
            bitset *bs1 = pair_versions(p);
            bitset *bs2 = pair_versions(q);
            bitset_tostring(bs1,str1,34);
            bitset_tostring(bs2,str2,34);
            res = !bitset_intersects(pair_versions(p),pair_versions(q));
        }
    }
    return res;
}
/**
 * Add a hint to the node immediately BEFORE the given pair
 * @param lp the linkpair trailing pair of the node
 * @param version the version of the hint
 * @param log the lg to record errors in
 */
void linkpair_add_hint( linkpair *lp, int version, plugin_log *log )
{
    pair *p = lp->left->p;
    if ( pair_is_hint(p) )
        bitset_set(pair_versions(p),version);
    else if ( bitset_next_set_bit(pair_versions(p),version)!=version )
    {
        bitset *bs = bitset_create();
        bitset_set( bs, version );
        pair *hint = pair_create_hint( bs );
        linkpair *hint_lp = linkpair_create( hint, log );
        hint_lp->left = lp->left;
        hint_lp->right = lp;
        lp->left->right = hint_lp;
        lp->left = hint_lp;
    }
}
/**
 * Split a linkpair and adjust parent/child if not a basic pair
 * @param lp the linkpair to split
 * @param at the offset in the pair data before which to split
 * @param log the log to record errors in
 */
void linkpair_split( linkpair *lp, int at, plugin_log *log )
{
    if ( pair_is_ordinary(lp->p) )
    {
        // q is the new trailing pair
        pair *q = pair_split( &lp->p, at );
        if ( q != NULL )
        {
            linkpair *lp2 = linkpair_create( q, log );
            if ( lp2 != NULL )
            {
                lp2->right = lp->right;
                if ( lp2->right != NULL )
                    lp2->right->left = lp2;
                lp->right = lp2;
                lp2->left = lp;
            }
        }
    }
    else if ( pair_is_child(lp->p) )
    {
        // to do
    }
    else if ( pair_is_parent(lp->p) )
    {
        // to do
    }
}
/**
 * Convert a list of linkpairs to a standard pair array
 * @param lp the head of the linkpair list
 * @return an allocated dynamic array of pairs or NULL
 */
dyn_array *linkpair_to_pairs( linkpair *lp )
{
    int npairs = linkpair_list_len(lp);
    dyn_array *da = dyn_array_create( npairs );
    if ( da != NULL )
    {
        while ( lp != NULL )
        {
            dyn_array_add( da, lp->p );
            lp = lp->right;
        }
    }
    return da;
}
/**
 * Do we define a node immediately on our right?
 * @param lp the linkpair in question
 * @return 1 if we do else 0
 */
int linkpair_node_to_right( linkpair *lp )
{
    int res = 0;
    if ( lp->right != NULL )
    {
        if ( pair_is_hint(lp->right->p) )
            res = 1;
        else
        {
            bitset *bs1 = pair_versions(lp->p);
            bitset *bs2 = pair_versions(lp->right->p);
            res = bitset_intersects(bs1,bs2);
        }
    }
    return res;
}
/**
 * Do we define a node immediately on our left?
 * @param lp the linkpair in question
 * @return 1 if we do else 0
 */
int linkpair_node_to_left( linkpair *lp )
{
    int res = 0;
    if ( lp->left != NULL )
    {
        if ( pair_is_hint(lp->left->p) )
            res = 1;
        else
        {
            bitset *bs1 = pair_versions(lp->p);
            bitset *bs2 = pair_versions(lp->left->p);
            res = bitset_intersects(bs1,bs2);
        }
    }
    return res;
}
/**
 * Compute the overhang for a linkpair node
 * @param lp the leading pair of a node
 * @return the bitset of the overhang or NULL (user to free)
 */
bitset *linkpair_node_overhang( linkpair *lp )
{
    bitset *bs = bitset_create();
    bs = bitset_or( bs, pair_versions(lp->p) );
    if ( pair_is_hint(lp->right->p) )
    {
        pair *pp = lp->right->p;
        bs = bitset_or( bs, pair_versions(pp) );
        if ( pair_is_hint(pp) )
            bitset_clear_bit(bs,0);
        lp = lp->right;
    }
    bitset_and_not( bs, pair_versions(lp->right->p) );
    return bs;
}
/**
 * Add a linkpair after a node. lp->right will be displaced by one 
 * linkpair. Although this will increase the node's overhang, the increase
 * is exactly that of the displaced linkpair's versions, which will reattach 
 * via the overhang, rather than the forced, rule.
 * @param lp the incoming pair of a node
 * @param after the linkpair to add into lp's node. must intersect with lp.
 * @return 1 if the new node is a bona fide node
 */
int linkpair_add_at_node( linkpair *lp, linkpair *after )
{
    bitset *bs1 = pair_versions(lp->p);
    if ( pair_is_hint(linkpair_pair(lp->right)) ) 
        lp = lp->right;
    after->right = lp->right;
    after->left = lp;
    if ( lp->right != NULL )
        lp->right->left = after;
    lp->right = after;
    return bitset_intersects( bs1, pair_versions(after->p) );
}
/**
 * Add a linkpair after a given point, creating a new node.
 * @param lp the point to add it after
 * @param after the linkpair to become after lp
 * @return 1 if successful else 0
 */
int linkpair_add_after( linkpair *lp, linkpair *after )
{
    if ( !linkpair_node_to_right(lp) )
    {
        after->right = lp->right;
        after->left = lp;
        if ( lp->right != NULL )
            lp->right->left = after;
        lp->right = after;
        return 1;
    }
    else
        return 0;
}
/**
 * Measure the length of a list
 * @param lp the list to measure
 * @return its length in linkpairs
 */
int linkpair_list_len( linkpair *lp )
{
    int len = 0;
    while ( lp != NULL )
    {
        len++;
        lp = lp->right;
    }
    return len;
}
/**
 * Check if a list is broken by being circular
 * @param lp a linkpair pointer somewhere in the list
 * @return 1 if it has two ends else 0
 */
int linkpair_list_circular( linkpair *lp )
{
    int res = 0;
    hashmap *hm = hashmap_create( 12, 0 );
    linkpair *right = lp->right;
    char junk[16];
    UChar key[32];
    char ckey[32];
    strcpy( junk, "junk" );
    snprintf(ckey,32,"%lx",(long)right);
    u_print( key, ckey, strlen(ckey) );
    hashmap_put( hm, key, junk );
    while ( !res && right != NULL )
    {
        snprintf(ckey,32,"%lx",(long)right);
        //printf("%s\n",ckey);
        ascii_to_uchar( ckey, key, 32 );
        if ( hashmap_contains(hm,key) )
            res = 1;
        else
        {
            hashmap_put( hm, key, junk );
            right = right->right;
        }
    }
    linkpair *left = lp->left;
    while ( !res && left != NULL )
    {
        snprintf(ckey,32,"%lx",(long)left);
        //printf("%s\n",ckey);
        ascii_to_uchar( ckey, key, 32 );
        if ( hashmap_contains(hm,key) )
            res = 1;
        else
        {
            hashmap_put( hm, key, junk );
            left = left->left;
        }
    }
    hashmap_dispose( hm, NULL );
    return res;
}
/**
 * Remove a pair, disposing of it and delinking it form the list
 * @param lp the linkpair to remove
 */
void linkpair_remove( linkpair *lp )
{
    if ( lp->left != NULL )
        lp->left->right = lp->right;
    if ( lp->right != NULL )
        lp->right->left = lp->left;
    linkpair_dispose( lp );
}
#ifdef MVD_TEST
static UChar data1[7] = {'b','a','n','a','n','a',0};
static UChar data2[6] = {'a','p','p','l','e',0};
static UChar data3[5] = {'p','e','a','r',0};
static UChar data4[7] = {'o','r','a','n','g','e',0};
static UChar data5[6] = {'g','u','a','v','a',0};
static char buf[SCRATCH_LEN];
static bitset *bs1,*bs2,*bs3,*bs4,*bs5,*bsr,*bsl;
static linkpair *lp1,*lp2,*lp3,*lp4,*lpl,*lpr;
static pair *p1,*p2,*p3,*p4,*pl,*pr;
static int make_test_data( plugin_log *log )
{
    lp1 = lp2 = lp3 = lp4 = NULL;
    bs1 = bs2 = bs3 = bs4 = bs5 = NULL;
    p1 = p2 = p3 = p4 = NULL;
    bs1 = bitset_create();
    bitset_set(bs1,3);
    bitset_set(bs1,27);
    bs2 = bitset_create();
    bitset_set(bs2,5);
    bitset_set(bs2,17);
    bs3 = bitset_create();
    bitset_set(bs3,7);
    bitset_set(bs3,14);
    bitset_set(bs3,19);
    bs4 = bitset_create();
    bitset_set(bs4,7);
    bitset_set(bs4,14);
    bitset_set(bs4,22);
    bs5 = bitset_create();
    bitset_set(bs5,7);
    bitset_set(bs5,14);
    bsl = bitset_create();
    bsr = bitset_create();
    bsl = bitset_set(bsl,3);
    if ( bsl != NULL )
        bsl = bitset_set(bsl,23);
    bsr = bitset_set(bsr,3);
    if ( bsr != NULL )
        bsr = bitset_set(bsr,21);
    if ( bs1 != NULL && bs2 != NULL && bs3 != NULL && bs4 != NULL 
        && bs5 != NULL && bsl != NULL && bsr != NULL )
    {
        p1 = pair_create_basic(bs1, data1, 6);
        p2 = pair_create_basic(bs2, data2, 5);
        p3 = pair_create_basic(bs3, data3, 4);
        p4 = pair_create_basic(bs4, data4, 6);
        pl = pair_create_basic(bsl,data4,6);
        pr = pair_create_basic(bsr,data5,5);
        if ( p1 != NULL && p2 != NULL && p3 != NULL && p4 != NULL 
            && pl != NULL && pr != NULL )
        {
            lp1 = linkpair_create( p1, log );
            lp2 = linkpair_create( p2, log );
            lp3 = linkpair_create( p3, log );
            lp4 = linkpair_create( p4, log );
            lpl = linkpair_create( pl, log );
            lpr = linkpair_create( pr, log );
            if ( lp1 != NULL && lp2 != NULL && lp3 != NULL && lp4 != NULL 
                && lpl != NULL && lpr != NULL )
                return 1;
        }
    }
    return 0;
}
static void free_test_data( plugin_log *log )
{
    if ( lp1 != NULL )
        linkpair_dispose( lp1 );
    if ( lp2 != NULL )
        linkpair_dispose( lp2 );
    if ( lp3 != NULL )
        linkpair_dispose( lp3 );
    if ( lp4 != NULL )
        linkpair_dispose( lp4 );
    if ( lpl != NULL )
        linkpair_dispose( lpl );
    if ( lpr != NULL )
        linkpair_dispose( lpr );
    if ( p1 != NULL )
        pair_dispose( p1 );
    if ( p2 != NULL )
        pair_dispose( p2 );
    if ( p3 != NULL )
        pair_dispose( p3 );
    if ( p4 != NULL )
        pair_dispose( p4 );
    if ( pl != NULL )
        pair_dispose( pl );
    if ( pr != NULL )
        pair_dispose( pr );
    if ( bs1 != NULL )
        bitset_dispose( bs1 );
    if ( bs2 != NULL )
        bitset_dispose( bs2 );
    if ( bs3 != NULL )
        bitset_dispose( bs3 );
    if ( bs4 != NULL )
        bitset_dispose( bs4 );
    if ( bs5 != NULL )
        bitset_dispose( bs5 );
    if ( bsl != NULL )
        bitset_dispose( bsl );
    if ( bsr != NULL )
        bitset_dispose( bsr );
    if ( log != NULL )
        plugin_log_dispose( log );
}
static void linkpair_test_links( int *passed, int *failed, plugin_log *log )
{
    linkpair_set_left( lp2, lp1 );
    linkpair_set_right( lp1, lp2 );
    linkpair_set_right( lp2, lp3 );
    linkpair_set_left( lp3, lp2 );
    if ( linkpair_right(lp2) != lp3 && linkpair_left(lp3) != lp2 )
    {
        fprintf(stderr,"linkpair: linkpair_right failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
    if ( linkpair_left(lp2) != lp1 && linkpair_right(lp1) != lp2 )
    {
        fprintf(stderr,"linkpair: linkpair_left failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_replace( int *passed, int *failed, plugin_log *log )
{
    int len1 = linkpair_list_len(lp1);
    linkpair_replace( lp2, lp4 );
    if ( linkpair_right(lp4) != lp3 || linkpair_left(lp4) != lp1 )
    {
        (*failed)++;
        fprintf(stderr,"linkpair: linkpair_replace failed\n");
    }
    else
        (*passed)++;
    if ( linkpair_pair(lp1)!=p1 || linkpair_pair(lp2)!=p2
        || linkpair_pair(lp3)!=p3 || linkpair_pair(lp4)!=p4 )
    {
        fprintf(stderr,"linkpair: pair not set correctly\n");
        (*failed)++;
    }
    else
        (*passed)++;
    int len2 = linkpair_list_len(lp1);
    if ( len1 != len2 )
    {
        fprintf(stderr,"linkpair: replace failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_st_off( int *passed, int *failed, plugin_log *log )
{
    linkpair_set_st_off( lp2, 123 );
    if ( linkpair_st_off(lp2) != 123 )
    {
        fprintf(stderr,"linkpair: linkpair_set_st_off failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_trailing( int *passed, int *failed, plugin_log *log )
{
    if ( !linkpair_trailing_node(lp3) )
    {
        fprintf(stderr,"linkpair: not a trailing node\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_next( int *passed, int *failed, plugin_log *log )
{
    linkpair *next = linkpair_next(lp1,bs5);
    if ( next != lp4 )
    {
        fprintf(stderr,"linkpair: linkpair_next failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_free( int *passed, int *failed, plugin_log *log )
{
    if ( !linkpair_free(lp4) )
    {
        fprintf(stderr,"linkpair: linkpair_free failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_hint( int *passed, int *failed, plugin_log *log )
{
    int len1 = linkpair_list_len(lp1);
    linkpair_add_hint( lp4, 2, log );
    linkpair *left = linkpair_left( lp4 );
    pair *hp = linkpair_pair(left);
    bitset *bss = pair_versions(hp);
    int res2 = pair_is_hint(hp);
    int res3 = bitset_next_set_bit(bss,2);
    if ( !res2 || res3!=2 )
    {
        fprintf(stderr,"linkpair: failed to create hint\n");
        (*failed)++;
    }
    else
    {
        linkpair_remove(left);
        (*passed)++;
    }
    int len2 = linkpair_list_len(lp1);
    if ( len2 != len1 )
    {
        fprintf(stderr,"linkpair: add_hint failed\n");
        (*failed)++;
    }
    else
        (*passed)++;
}
static void linkpair_test_split( int *passed, int *failed, plugin_log *log )
{
    linkpair_split( lp2, 2, log );
    linkpair *right = linkpair_right(lp2);
    if ( right != NULL )
    {
        int len1 = pair_len(linkpair_pair(lp2));
        int len2 = pair_len(linkpair_pair(right));
        if ( len1!=2 ||len2 !=3 )
        {
            fprintf(stderr,"linkpair: split failed\n");
            (*failed)++;
        }
        else
            (*passed)++;
        pair_dispose( linkpair_pair(right) );
        linkpair_dispose( right );
        lp2->right = NULL;
    }
    else
    {
        fprintf(stderr,"linkpair: split failed\n");
        (*failed)++;
    }
}
static void linkpair_test_array( int *passed, int *failed, plugin_log *log )
{
    dyn_array *array = linkpair_to_pairs( lp1 );
    if ( array != NULL )
    {
        int i;
        linkpair *lp = lp1;
        for ( i=0;i<dyn_array_size(array);i++,
            lp=linkpair_right(lp) )
        {
            pair *p = dyn_array_get(array,i);
            if ( p != linkpair_pair(lp) )
            {
                (*failed)++;
                fprintf(stderr,
                    "linkpair: list to array failed 1\n");
                break;
            }
        }
        if ( lp != NULL )
        {
            (*failed)++;
            fprintf(stderr,"linkpair: list to array failed 2\n");
        }
        dyn_array_dispose( array );
    }
    else
    {
        fprintf(stderr,"linkpair: list to array failed 3\n");
        (*failed)++;
    }
}
static void linkpair_test_node_to_right( int *passed, int *failed, plugin_log *log )
{
    linkpair_set_right(lpl,lpr);
    linkpair_set_left(lpr,lpl);
    if ( !linkpair_node_to_right(lpl) )
    {
        fprintf(stderr,"linkpair: failed test node to right 1\n");
        (*failed)++;
    }
    else
    {
        (*passed)++;
        linkpair_add_hint(lpr,7,log);
        if ( !linkpair_node_to_right(lpl) )
        {
            fprintf(stderr,"linkpair: failed test node to right 2\n");
            (*failed)++;
        }
        else
            (*passed)++;
        linkpair *h = linkpair_right(lpl);
        if ( !pair_is_hint(linkpair_pair(h)) )
        {
            fprintf(stderr,"linkpair: failed to create hint\n");
            (*failed)++;
        }
        else
        {
            (*passed)++;
            linkpair_dispose(linkpair_right(lpl));
        }
    }
}
static void linkpair_test_node_to_left( int *passed, int *failed, plugin_log *log )
{
    linkpair_set_right(lpl,lpr);
    linkpair_set_left(lpr,lpl);
    if ( !linkpair_node_to_left(lpr) )
    {
        fprintf(stderr,"linkpair: failed test node to left 1\n");
        (*failed)++;
    }
    else
    {
        (*passed)++;
        linkpair_add_hint(lpr,7,log);
        if ( !linkpair_node_to_left(lpr) )
        {
            fprintf(stderr,"linkpair: failed test node to left 2\n");
            (*failed)++;
        }
        else
            (*passed)++;
        linkpair *h = linkpair_right(lpl);
        if ( !pair_is_hint(linkpair_pair(h)) )
        {
            fprintf(stderr,"linkpair: failed to create hint 2\n");
            (*failed)++;
        }
        else
        {
            (*passed)++;
            linkpair_dispose(linkpair_right(lpl));
        }
    }
}
static void linkpair_test_overhang( int *passed, int *failed, plugin_log *log )
{
    linkpair_set_right(lpl,lpr);
    linkpair_set_left(lpr,lpl);
    linkpair_add_hint(lpr,7,log);
    bitset *bs = linkpair_node_overhang( lpl );
    if ( bs != NULL )
    {
        bitset *bsc = bitset_create();
        if ( bsc != NULL )
        {
            bitset_or( bsc, bsl );
            bitset_set( bsc, 7 );
            bitset_and_not( bsc, pair_versions(pr) );
            if ( !bitset_equals(bsc,bs) )
            {
                fprintf(stderr,"linkpair: overhang text failed\n");
                (*failed)++;
            }
            else
                (*passed)++;
        }
        else
            (*failed)++;
        if ( bsc != NULL )
            bitset_dispose( bsc );
    }
    else
    {
        fprintf(stderr,"linkpair: failed to compute overhang\n");
        (*failed)++;
    }
    if ( bs != NULL )
        bitset_dispose( bs );
    linkpair *r = linkpair_right(lpl);
    pair *ph = linkpair_pair(r);
    if ( r != lpr && pair_is_hint(ph) )
    {
        linkpair_dispose( r );
        pair_dispose( ph );
    }
}
void linkpair_test( int *passed, int *failed )
{
    plugin_log *log = plugin_log_create( buf );
    if ( log != NULL )
    {
        int res = make_test_data(log);
        if ( res )
        {
            linkpair_test_links( passed, failed, log );
            linkpair_test_replace( passed, failed, log );
            linkpair_test_st_off( passed, failed, log );    
            linkpair_test_trailing( passed, failed, log );
            linkpair_test_next( passed, failed, log );
            linkpair_test_hint( passed, failed, log );
            linkpair_test_split( passed, failed, log ); 
            linkpair_test_array( passed, failed, log );
            linkpair_test_node_to_right( passed, failed, log );
            linkpair_test_node_to_left( passed, failed, log );
            linkpair_test_overhang( passed, failed, log );
        }
        else
        {
            fprintf(stderr,"linkpair: failed to initialise test data\n");
            (*failed)++;
        }
    }
    else
        (*failed)++;
    free_test_data(log);
}
#endif