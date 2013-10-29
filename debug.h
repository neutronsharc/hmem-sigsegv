#ifndef __MYDEBUG__
#define __MYDEBUG__

#include "assert.h"

#define MYDBG 0


#if MYDBG
	#define dbg(fmt, args... )	do { \
	fprintf(stderr, "%s: "fmt, __func__, ##args); fflush(stderr); \
	}while(0)
#else
	#define dbg(fmt, args... )
#endif
		
#define error(fmt, args... )	do { \
	fprintf(stderr, "%s: Error!!  "fmt, __func__, ##args );	 fflush(stderr); \
	}while(0)

#define err(fmt, args... )	error(fmt, ##args)


#define err_exit(fmt, args... )	 do { \
	error(fmt, ##args); assert(0); } while(0)


//#undef MYDBG



#endif  /// end of __MYDEBUG__
