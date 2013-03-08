#include <stdlib.h>
#include <limits.h>
#include "bitset.h"
#include <unicode/uchar.h>
#include "link_node.h"
#include "pair.h"
#include "plugin_log.h"
#include "node.h"
#include "pos.h"
#include "suffixtree.h"
#include "match.h"
#define KDIST 2
struct match_struct
{
    // index of first matched/unmatched pair
    int start_p;
    // index of last matched/unmatched pair
    int end_p;
    // index into data of first pair where we started this match
    int start_pos;
    // index into data of last pair of current match/mismatch
    int end_pos;
    // offset of match in suffixtree's underlying string
    int st_off;
    // last matched value of end_p
    int prev_p;
    // last matched value of end_pos
    int prev_pos;
    // length of match
    int len;
    // index of last pair
    int end;
    // pairs array - read only
    pair **pairs;
    // suffix tree of new version to match pairs against - read only
    suffixtree *st;
    // cumulative ANDed versions of current matched path
    bitset *bs;
    // next match in this sequence satisfying certain rigid criteria
    match *next;
};
match *match_create( int i, int j, pair **pairs, int end, plugin_log *log )
{
    match *mt = calloc( 1, sizeof(match) );
    if ( mt != NULL )
    {
        mt->prev_p = mt->end_p = mt->start_p = i;
        mt->prev_pos = mt->end_pos = mt->start_pos = j;
        mt->pairs = pairs;
        mt->end = end;
    }
    else
        plugin_log_add( log, "match: failed to create match object\n");
    return mt;
}
/**
 * Clone a match object making ready to continue from where it left off
 * @param mt the match to copy
 * @param log the log to report errors to
 * @return a copy of mt or NULL on failure
 */
match *match_clone( match *mt, plugin_log *log )
{
    match *mt2 = calloc( 1, sizeof(match) );
    if ( mt2 != NULL )
    {
        *mt2 = *mt;
        // pick up where our parent left off
        mt2->start_p = mt->end_p;
        mt2->start_pos = mt->end_pos;
        mt2->len = 0;
        mt->st_off += mt->len;
    }
    else
        plugin_log_add( log, "match: failed to create match object\n");
    return mt2;
}
/**
 * Dispose of a match object
 * @param m the match in question
 */
void match_dispose( match *m )
{
    if ( m->bs != NULL )
        bitset_dispose( m->bs );
    if ( m->next != NULL )
        match_dispose( m->next );
    free( m );
}
/**
 * Add a match onto the end of this one
 * @param m1 the first match, us
 * @param m2 the new match
 */
void match_append( match *m1, match *m2 )
{
    match *temp = m1;
    while ( temp->next != NULL )
        temp = temp->next;
    temp->next = m2;
}
/**
 * Get the next pair in an array
 * @param pairs an array of pairs
 * @param end index of last pair
 * @param pos position of first pair to look at
 * @param bs the bitset of versions to follow
 * @return -1 on failure else the index of the next pair
 */
static int next_pair( pair **pairs, int end, int pos, bitset *bs )
{
    int res = -1;
    int i = pos;
    while ( i<=end && !bitset_intersects(bs,pair_versions(pairs[i])) )
        i++;
    if ( i <= end )
        res = i;
    return res;
}
/**
 * Does one match follow another within a certain edit distance (KDIST)
 * @param first the first match
 * @param second the second, which should follow it closely
 * @return 1 if they do follow else 0
 */
int match_follows( match *first, match *second )
{
    int res = 0;
    int pairs_dist = 0;
    int st_dist = 0;
    // compute proximity in pairs list 
    // 1. simplest case: same pair
    if ( second->start_p == first->end_p )
        pairs_dist = second->start_pos-first->end_pos;
    // 2. compute distance between pairs
    else if ( second->start_p > first->end_p )
    {
        pairs_dist = pair_len(first->pairs[first->end_p])-(first->end_pos+1);
        int i = first->end_p;
        while ( pairs_dist <= KDIST )
        {
            i = next_pair( first->pairs,first->end,i+1, first->bs );
            if ( i != -1 )
            {
                int p_len = pair_len(first->pairs[i]);
                if ( i < second->start_p )
                    pairs_dist += p_len;
                else if ( i == second->start_p )
                    pairs_dist += second->end_pos;
            }
            else
                break;
            i++;
        }
    }
    else
        pairs_dist = INT_MAX;
    // compute distance between matches in the suffixtree
    st_dist = second->st_off - (first->st_off+first->len);
    if ( pairs_dist <= KDIST && st_dist>= 0 && st_dist <= KDIST  )
        res = 1;
    return res;
}
/**
 * Advance the match position and return the next char
 * @param m the matcher instance
 * @param mt the match object
 * @return the next UChar or 0 if this we reached the end
 */
UChar match_advance( match *m )
{
    m->prev_p = m->end_p;
    m->prev_pos = m->end_pos;
    if ( pair_len(m->pairs[m->end_p])>m->end_pos+1 )
    {
        int i;
        for ( i=m->end_p+1;i<=m->end;i++ )
        {
            bitset *bs = pair_versions(m->pairs[i]);
            if ( bitset_intersects(m->bs,bs) )
            {
                bitset_and( m->bs, bs );
                if ( pair_len(m->pairs[i])> 0 )
                {
                    m->end_p = i;
                    m->end_pos = 0;
                    UChar *data = pair_data(m->pairs[i]);
                    return data[0];
                }
            }
        }
        // end of text
        return (UChar)0;
    }
    else
    {
        UChar *data = pair_data(m->pairs[m->end_p]);
        return data[++m->end_pos];
    }
}
/**
 * Complete a single match between the pairs list and the suffixtree
 * @param m the match all ready to go
 * @return 1 if the match was successful else 0
 */
int match_single( match *m )
{
    UChar c;
    pos loc;
    loc.v = suffixtree_root( m->st );
    loc.loc = node_start(loc.v);
    do 
    {
        c = match_advance( m );
        if ( c != 0 && suffixtree_advance_pos(m->st,&loc,c) )
            match_inc_len( m );
        else
            break;
    }
    while ( 1 );
    return m->len > 0;
}
int match_start_index( match *m )
{
    return m->start_p;
}
int match_prev_pos( match *m )
{
    return m->prev_pos;
}

int match_inc_end_pos( match *m )
{
    return ++m->end_pos;
}
suffixtree *match_suffixtree( match *m )
{
    return m->st;
}
void match_inc_len( match *m )
{
    m->len++;
}
void match_set_st_offset( match *m, int off )
{
    m->st_off = off;
}
int match_len( match *m )
{
    return m->len;
}
int match_total_len( match *m )
{
    int len = m->len;
    while ( m->next != NULL )
    {
        m = m->next;
        len += m->len;
    }
    return len;
}
void match_set_versions( match *m, bitset *bs )
{
    m->bs = bs;
}
/**
 * Compare two matches based on their length
 * @param a the first match
 * @param b the second match
 * @return 1 if a was greater else if b greater -1 else 0 if equal
 */
int match_compare( void *a, void *b )
{
    match *one = (match*)a;
    match *two = (match*)b;
    int len1 = one->len;
    int len2 = two->len;
    while ( one->next != NULL )
    {
        one = one->next;
        len1 += one->len;
    }
    while ( two->next != NULL )
    {
        two = two->next;
        len2 += two->len;
    }
    if ( len1>len2 )
        return 1;
    else if ( len2>len1 )
        return -1;
    else
        return 0;
}