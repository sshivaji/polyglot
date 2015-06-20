
// book_make.h

#ifndef BOOK_MAKE_H
#define BOOK_MAKE_H

// includes

#include "util.h"
//#include "leveldb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/db.h" 
#include "rocksdb/write_batch.h"



// functions

extern void book_make (int argc, char * argv[]);

#endif // !defined BOOK_MAKE_H

// end of book_make.h

