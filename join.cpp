/*****************************************************************************\
**                                                                           **
** PBX4Linux                                                                 **
**                                                                           **
**---------------------------------------------------------------------------**
** Copyright: Andreas Eversberg                                              **
**                                                                           **
** join functions                                                            **
**                                                                           **
\*****************************************************************************/ 

#include "main.h"
//#define __u8 unsigned char
//#define __u16 unsigned short
//#define __u32 unsigned int

unsigned int join_serial = 1; /* must be 1, because 0== no join */

//JOIN_STATES

class Join *join_first = NULL;

/*
 * find the join with join_id
 */ 
class Join *find_join_id(unsigned int join_id)
{
	class Join *join = join_first;

	while(join) {
//printf("comparing: '%s' with '%s'\n", name, join->j_name);
		if (join->j_serial == join_id)
			return(join);
		join = join->next;
	}

	return(NULL);
}


/*
 * constructor for a new join 
 */
Join::Join(void)
{
	class Join **joinp;

	j_serial = join_serial++;
	j_type = JOIN_TYPE_NONE;

	/* attach to chain */
	next = NULL;
	joinp = &join_first;
	while(*joinp)
		joinp = &((*joinp)->next);
	*joinp = this;

	classuse++;
}


/*
 * join descructor
 */
Join::~Join()
{
	class Join *cl, **clp;

	classuse--;

	cl = join_first;
	clp = &join_first;
	while(cl) {
		if (cl == this)
			break;
		clp = &cl->next;
		cl = cl->next;
	}
	if (!cl)
		FATAL("software error, join not in chain!\n");
	*clp = cl->next; /* detach from chain */
}



/* epoint sends a message to a join
 *
 */
void Join::message_epoint(unsigned int epoint_id, int message_type, union parameter *param)
{
}


/* free all join structures */
void join_free(void)
{

	if (!join_first) {
		PDEBUG(DEBUG_JOIN, "no more pending join(s), done!\n");
		return;
	}
	while(join_first) {
		if (options.deb & DEBUG_JOIN) {
			PDEBUG(DEBUG_JOIN, "freeing pending join\n");
		}

		delete join_first;
	}
}



