
// book_make.cpp

// includes

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>  
#include <iostream>
#include <sstream>
#include <iomanip>


#include "board.h"
#include "book_make.h"
#include "move.h"
#include "move_do.h"
#include "move_legal.h"
#include "pgn.h"
#include "san.h"
#include "util.h"

// constants

using namespace std;
static const int COUNT_MAX = 16384;

static const int NIL = -1;

// types

struct entry_t {
    uint64 key;
    //   uint16 move;
    int n;
    int white_score;
    int draws;

    uint16 colour;
    set<int> * game_ids;
    set<uint16> * moves;
};

struct book_t {
    int size;
    int alloc;
    uint32 mask;
    entry_t * entry;
    sint32 * hash;
};

// variables

static int MaxPly;
static int MinElo;
static int NumBatchGames = 100000;
static int MinGame;
static double MinScore;
static bool RemoveWhite, RemoveBlack;
static bool Uniform;

static enum STORAGE {
    POLYGLOT, LEVELDB
} Storage;

static book_t Book[1];
//static leveldb::DB *BookLevelDb;

// prototypes

static void book_clear();
static void book_insert(const char pgn_file_name[], const char level_db_file_name[]);
static void book_filter();
static void book_sort();
static void book_save(const char file_name[], const char leveldb_file[]);

static int find_entry(const board_t * board);
static void resize();
static void halve_stats(uint64 key);

static bool keep_entry(int pos);

static int entry_score(const entry_t * entry);

static int key_compare(const void * p1, const void * p2);

static void write_integer(FILE * file, int size, uint64 n);


// functions
// book_make()

void book_make(int argc, char * argv[]) {

    int i;
    const char * pgn_file;
    const char * bin_file;
    const char * leveldb_file;

    pgn_file = NULL;
    my_string_set(&pgn_file, "book.pgn");

    bin_file = NULL;
    my_string_set(&bin_file, "book.bin");

    leveldb_file = NULL;
    my_string_set(&leveldb_file, "game_index.db");

    MaxPly = 1024;
    MinElo = 2400;
    MinGame = 3;
    MinScore = 0.0;
    RemoveWhite = false;
    RemoveBlack = false;
    Uniform = false;
    Storage = POLYGLOT;

    for (i = 1; i < argc; i++) {

        if (my_string_equal(argv[i], "make-book")) {

            // skip

        } else if (my_string_equal(argv[i], "-pgn")) {

            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

            my_string_set(&pgn_file, argv[i]);

        } else if (my_string_equal(argv[i], "-bin")) {

            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

            my_string_set(&bin_file, argv[i]);

        } else if (my_string_equal(argv[i], "-max-ply")) {

            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

            MaxPly = atoi(argv[i]);
            ASSERT(MaxPly >= 0);

        } else if (my_string_equal(argv[i], "-min-game")) {

            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

            MinGame = atoi(argv[i]);
            ASSERT(MinGame > 0);

        } else if (my_string_equal(argv[i], "-min-score")) {

            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

            MinScore = atof(argv[i]) / 100.0;
            ASSERT(MinScore >= 0.0 && MinScore <= 1.0);

        } else if (my_string_equal(argv[i], "-only-white")) {

            RemoveWhite = false;
            RemoveBlack = true;

        } else if (my_string_equal(argv[i], "-only-black")) {

            RemoveWhite = true;
            RemoveBlack = false;

        } else if (my_string_equal(argv[i], "-uniform")) {

            Uniform = true;

        } else if (my_string_equal(argv[i], "-leveldb")) {
            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument leveldb\n");

            my_string_set(&leveldb_file, argv[i]);
            Storage = LEVELDB;
        }
        else if (my_string_equal(argv[i], "-min-elo")) {
            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument min-elo\n");
            MinElo = (int) atof(argv[i]);
            ASSERT(MinElo >= 1000 && MinELo <= 3000);
        }
        else if (my_string_equal(argv[i], "-num-batch-games")) {
            i++;
            if (argv[i] == NULL) my_fatal("book_make(): missing argument num-batch-game\n");
            NumBatchGames = (int) atof(argv[i]);
            ASSERT(NumBatchGames >= 1);
        }

        else {
            my_fatal("book_make(): unknown option \"%s\"\n", argv[i]);
        }
    }

    //   string b_file = bin_file;
    book_clear();

    printf("inserting games ...\n");
    //   book_insert(pgn_file);

    //   if (Storage == LEVELDB) {
    printf("Saving to leveldb.. \n");
    book_insert(pgn_file, leveldb_file);
    //   }
    //   else {
    //        book_insert(pgn_file, NULL);
    //        printf("filtering entries ...\n");
    //        book_filter();
    //
    //        printf("sorting entries ...\n");
    //        book_sort();
    //
    //        printf("saving entries ...\n");
    ////        if (Storage == LEVELDB) {
    ////                printf("Saving to leveldb.. \n");
    ////                book_save(NULL, leveldb_file);
    ////        }
    ////        else {
    //        book_save(bin_file, NULL);
    ////        }
    //    }


    printf("\nall done!\n");
}

static std::string game_info_to_string(const char* a, int i, const char* b) {
    std::ostringstream os;
    os << a << i << b;
    return os.str();
}


// book_clear()

static void book_clear() {

    //  if (Storage == LEVELDB) {
    //      leveldb::Options options;
    //      options.create_if_missing = true;
    //      leveldb::DB::Open(options, bin_file, &BookLevelDb);
    //    }
    //  else {
    int index;

    Book->alloc = 1;
    Book->mask = (uint32) ((Book->alloc * 2) - 1);

    Book->entry = (entry_t *) my_malloc((int) (Book->alloc * sizeof (entry_t)));
    Book->size = 0;

    Book->hash = (sint32 *) my_malloc((int) ((Book->alloc * 2) * sizeof (sint32)));
    for (index = 0; index < Book->alloc * 2; index++) {
        Book->hash[index] = NIL;
    }
    //    }
}


static void insert_into_leveldb(leveldb::DB* db, int passNumber) {
    printf("\nPutting games into leveldb..");
    leveldb::WriteBatch batch;

    leveldb::WriteOptions writeOptions;
    leveldb::ReadOptions readOptions;

//                writeOptions.disableWAL = true;

    for (int pos = 0; pos < Book->size; pos++) {

        if (pos % 100000 == 0) {
//            cout << "pos : "<< pos<< " total: "<<Book->size;
            cout << "\n "<< setprecision(4) << pos*100.0/Book->size<< " %";
        }
        
        std::stringstream game_id_stream;
        std::string currentValue;
//        leveldb::Status s = db->Get(readOptions, std::to_string(Book->entry[pos].key), &currentValue);
//        if (s.ok()) {
//            game_id_stream << currentValue;
//        }
                
        for (set<int>::iterator it = Book->entry[pos].game_ids->begin(); it != Book->entry[pos].game_ids->end(); ++it) {
            game_id_stream << *it << ",";
        }

//        s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_moves", &currentValue);
//        if (s.ok()) {
            //                    cout << "OK..";
            //                    cout << currentValue;
//            std::istringstream ss(currentValue);
//            std::string token;

//            while (std::getline(ss, token, ',')) {
//                Book->entry[pos].moves->insert(std::stoi(token));
//            }
//        }

        std::stringstream move_stream;
        for (set<uint16>::iterator it = Book->entry[pos].moves->begin(); it != Book->entry[pos].moves->end(); ++it) {
            move_stream << *it << ",";
        }

        batch.Put(std::to_string(Book->entry[pos].key)+"_p_"+std::to_string(passNumber), game_id_stream.str());
        batch.Put(std::to_string(Book->entry[pos].key) + "_moves_p_"+std::to_string(passNumber), move_stream.str());

//        s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_freq", &currentValue);
//        if (s.ok()) {
//            Book->entry[pos].n += std::stoi(currentValue);
//        }
//
//        s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_white_score", &currentValue);
//        if (s.ok()) {
////                        cout << "White score : "<<Book->entry[pos].white_score << "\n";
////                        cout << "Current Value White Score: " << currentValue << "\n";
//
//            Book->entry[pos].white_score += std::stoi(currentValue);
//        }
//
//        s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_draws", &currentValue);
//        if (s.ok()) {
////                        cout << "Current Value DRAWS: " << currentValue << "\n";
//
//            Book->entry[pos].draws += std::stoi(currentValue);
//        }

//                    setCounters.add((std::to_string(Book->entry[pos].key)), game_id_stream.str());
//                    
//                    counters.add((std::to_string(Book->entry[pos].key) + "_freq"), Book->entry[pos].n);
//                    counters.add((std::to_string(Book->entry[pos].key) + "_white_score"), Book->entry[pos].white_score);
//                    counters.add((std::to_string(Book->entry[pos].key) + "_draws"), Book->entry[pos].draws);
//                    
        batch.Put(std::to_string(Book->entry[pos].key) + "_freq_p_"+std::to_string(passNumber), std::to_string(Book->entry[pos].n));
        batch.Put(std::to_string(Book->entry[pos].key) + "_white_score_p_"+std::to_string(passNumber), std::to_string(Book->entry[pos].white_score));
        batch.Put(std::to_string(Book->entry[pos].key) + "_draws_p_"+std::to_string(passNumber), std::to_string(Book->entry[pos].draws));

    }
    //            batch.Clear();
    book_clear();

    db->Write(leveldb::WriteOptions(), &batch);
    batch.Clear();
}

// book_insert()
static void book_insert(const char file_name[], const char leveldb_file_name[]) {

    int game_nb = 0;
    pgn_t pgn[1];
    board_t board[1];
    int ply;
    int result;
    int draw;
    char string[256];
    int move;
    int pos;
    int passNumber = 0;
    leveldb::WriteOptions writeOptions;

    leveldb::DB* db;
    
    ASSERT(file_name != NULL);


//    if (leveldb_file_name != NULL) {

    leveldb::Options options;
    options.create_if_missing = true;
//    options.merge_operator.reset(new UInt32AddOperator);
//    options.merge_operator.reset(new SetAddOperator);

    //       MergeBasedCounters counters(db);

//    unsigned concurrentThreadsSupported = std::thread::hardware_concurrency();
//    if (concurrentThreadsSupported != 0) {
//        // Adjust for hyperthreading
//        if (concurrentThreadsSupported >= 4) {
//            concurrentThreadsSupported /= 2;
//        }
//        cout << "Using " << concurrentThreadsSupported << " threads\n";
//        options.max_background_compactions = concurrentThreadsSupported;
//    }
    leveldb::Status status = leveldb::DB::Open(options, leveldb_file_name, &db);
//    std::shared_ptr<leveldb::DB> dc (db);
//    MergeBasedCounters counters(dc);
//    MergeBasedSetCounters setCounters(dc, set<int>());
//        counters.add("a", 1);
    std::cout << leveldb_file_name << "\n";
    //       assert(status.ok());

//    }

    // scan loop

    pgn_open(pgn, file_name);

    while (pgn_next_game(pgn)) {
//        cout << "Proecessing game..\n";
//        cout << "Pgn: " << pgn->white << "\n";
        board_start(board);
        ply = 0;
        result = 0;
        draw = 0;
        int WhiteElo;
        int BlackElo;
        bool skipGame = false;

        if (my_string_equal(pgn->result, "1-0")) {
            result = +1;
        } else if (my_string_equal(pgn->result, "0-1")) {
            result = -1;
        } else {
            draw = 1;
        }

        if (leveldb_file_name != NULL) {

            std::stringstream game_info;
            game_info << pgn->white;
            game_info << "|" << pgn->whiteelo;
            try {
                WhiteElo = std::stoi(pgn->whiteelo);
                BlackElo = std::stoi(pgn->blackelo);
//                cout << "White ELO: " << WhiteElo << "\n";
//                cout << "Black ELO: " << BlackElo << "\n";

                if ((WhiteElo < MinElo) || (BlackElo < MinElo)) {
//                    cout << "Skipping game..";
                    skipGame = true;
                }

            }
            catch (const std::exception& ex) {
                WhiteElo = MinElo;
                BlackElo = MinElo;
                cout << ex.what() << "\n";
//                cout << "Exception\n ";
            }

            game_info << "|" << pgn->black;
            game_info << "|" << pgn->blackelo;
            game_info << "|" << pgn->result;

            game_info << "|" << pgn->date;
            game_info << "|" << pgn->event;
            game_info << "|" << pgn->site;
            game_info << "|" << pgn->eco;
            game_info << "|" << pgn->last_stream_pos;
            game_info << "|" << pgn->fen;

            game_info << "|" << pgn->plycount;
            game_info << "|" << pgn->eventdate;
            game_info << "|" << pgn->eventtype;

            db->Put(writeOptions, game_info_to_string("game_", game_nb, "_data"), game_info.str());
        }

        while (pgn_next_move(pgn, string, 256)) {
            if (ply < MaxPly && !skipGame)  {

                move = move_from_san(string, board);

                if (move == MoveNone || !move_is_legal(move, board)) {
                    my_log("book_insert(): illegal move \"%s\" at line %d, column %d\n", string, pgn->move_line, pgn->move_column);
                }

                //            if (leveldb_file_name==NULL) {

                pos = find_entry(board);

                Book->entry[pos].n++;
                Book->entry[pos].white_score += result;
                Book->entry[pos].draws += draw;

                Book->entry[pos].moves->insert((const unsigned short &) move);
                Book->entry[pos].game_ids->insert(game_nb);

                move_do(board, move);
                ply++;
                //            result = -result;
            }
        }

        game_nb++;
        if (game_nb % 10000 == 0) {
            printf("%d games ...\n", game_nb);

        }

        if (game_nb % NumBatchGames == 0) {
            if (leveldb_file_name != NULL) {
                insert_into_leveldb(db, passNumber);
                ++passNumber;
            }
        }
    }

    pgn_close(pgn);

    printf("%d game%s.\n", game_nb + 1, (game_nb > 1) ? "s" : "");
    if (leveldb_file_name == NULL) {
        printf("%d entries.\n", Book->size);
    } else {
        printf("%d entries.\n", Book->size);
        insert_into_leveldb(db, passNumber);
        db->Put(writeOptions, "total_game_count", std::to_string(game_nb + 1));
        db->Put(writeOptions, "pgn_filename", file_name);
        db->Put(writeOptions, "numPasses",  std::to_string(passNumber));

        delete db;

    }

    return;
}

// book_filter()

//static void book_filter() {
//
//   int src, dst;
//
//   // entry loop
//
//   dst = 0;
//
//   for (src = 0; src < Book->size; src++) {
//      if (keep_entry(src)) Book->entry[dst++] = Book->entry[src];
//   }
//
//   ASSERT(dst>=0&&dst<=Book->size);
//   Book->size = dst;
//
//   printf("%d entries.\n",Book->size);
//}
//
//// book_sort()
//
//static void book_sort() {
//
//   // sort keys for binary search
//
//   qsort(Book->entry,Book->size,sizeof(entry_t),&key_compare);
//}
//
//// book_save()
//// TODO: refactor this to 2 methods
//static void book_save(const char file_name[], const char leveldb_file[]) {
//
//   FILE * file;
//   int pos;
//
//   leveldb::WriteOptions writeOptions = leveldb::WriteOptions();
//   leveldb::DB* BookLevelDb;
//
//    if (leveldb_file != NULL) {
//        leveldb::Options options;
//        options.create_if_missing = true;
//        leveldb::Status status = leveldb::DB::Open(options, leveldb_file, &BookLevelDb);
//        assert(status.ok());
//    }
//   
//   if (file_name != NULL) {
//       file = fopen(file_name,"wb");
//       if (file == NULL) my_fatal("book_save(): can't open file \"%s\" for writing: %s\n",file_name,strerror(errno));
//   }
//   // entry loop
//
//    for (pos = 0; pos < Book->size; pos++) {
//
//        ASSERT(keep_entry(pos));
//        if (leveldb_file != NULL) {
//            std::stringstream game_id_stream;
//            std::string currentValue;
//            leveldb::Status s = BookLevelDb->Get(leveldb::ReadOptions(), uint64_to_string(Book->entry[pos].key), &currentValue);
//            if (s.ok()) {
//                 game_id_stream << currentValue;
//            }
//            
//            for (set<int>::iterator it = Book->entry[pos].game_ids->begin(); it != Book->entry[pos].game_ids->end(); ++it) {
//                game_id_stream << *it << ",";
//            }
//            
//            BookLevelDb->Put(writeOptions, uint64_to_string(Book->entry[pos].key), game_id_stream.str());
//        }
//
//        if (file_name != NULL) {
//
//            write_integer(file, 8, Book->entry[pos].key);
////            write_integer(file, 2, Book->entry[pos].move);
//            write_integer(file, 2, entry_score(&Book->entry[pos]));
//            write_integer(file, 2, 0);
//            write_integer(file, 2, 0);
//        }
//    }
//   if (leveldb_file != NULL) {
//        delete BookLevelDb;
//   }
//   
//  if (file_name != NULL) {
//      fclose(file);
//  }
//}

// find_entry()

static int find_entry(const board_t * board) {

    uint64 key;
    int index;
    int pos;

    ASSERT(board != NULL);
    //  ASSERT(move_is_ok(move));
    //
    //  ASSERT(move_is_legal(move,board));

    // init

    key = board->key;

    // search

    //  if (Storage==POLYGLOT) {
    for (index = (int) (key & Book->mask); (pos = Book->hash[index]) != NIL; index = (index + 1) & Book->mask) {

        ASSERT(pos >= 0 && pos < Book->size);

        //        if (Book->entry[pos].key == key && Book->entry[pos].move == move) {
        //            return pos; // found
        //          }
        if (Book->entry[pos].key == key) {
            return pos; // found
        }
    }

    // not found

    ASSERT(Book->size <= Book->alloc);

    if (Book->size == Book->alloc) {

        // allocate more memory

        resize();

        for (index = (int) (key & Book->mask); Book->hash[index] != NIL; index = (index + 1) & Book->mask)
            ;
    }

    // create a new entry

    ASSERT(Book->size < Book->alloc);
    pos = Book->size++;

    Book->entry[pos].key = key;
    //    Book->entry[pos].move = move;
    Book->entry[pos].moves = new set<uint16>();
    Book->entry[pos].n = 0;
    Book->entry[pos].white_score = 0;
    Book->entry[pos].draws = 0;

    Book->entry[pos].game_ids = new set<int>();
    Book->entry[pos].colour = (uint16) board->turn;

    // insert into the hash table

    ASSERT(index >= 0 && index < Book->alloc * 2);
    ASSERT(Book->hash[index] == NIL);
    Book->hash[index] = pos;

    ASSERT(pos >= 0 && pos < Book->size);
    //    }
    return pos;

}

// resize()

static void resize() {

    int size;
    int pos;
    int index;

    ASSERT(Book->size == Book->alloc);

    Book->alloc *= 2;
    Book->mask = (uint32) ((Book->alloc * 2) - 1);

    size = 0;
    size += Book->alloc * sizeof (entry_t);
    size += (Book->alloc * 2) * sizeof (sint32);

    if (size >= 1048576) printf("\nallocating %gMB ...\n", double(size) / 1048576.0);

    // resize arrays

    Book->entry = (entry_t *) my_realloc(Book->entry, (int) (Book->alloc * sizeof (entry_t)));
    Book->hash = (sint32 *) my_realloc(Book->hash, (int) ((Book->alloc * 2) * sizeof (sint32)));

    // rebuild hash table

    for (index = 0; index < Book->alloc * 2; index++) {
        Book->hash[index] = NIL;
    }

    for (pos = 0; pos < Book->size; pos++) {

        for (index = (int) (Book->entry[pos].key & Book->mask); Book->hash[index] != NIL; index = (index + 1) & Book->mask)
            ;

        ASSERT(index >= 0 && index < Book->alloc * 2);
        Book->hash[index] = pos;
    }
}

// halve_stats()

static void halve_stats(uint64 key) {

    int index;
    int pos;

    // search

    for (index = (int) (key & Book->mask); (pos = Book->hash[index]) != NIL; index = (index + 1) & Book->mask) {

        ASSERT(pos >= 0 && pos < Book->size);

        if (Book->entry[pos].key == key) {
            Book->entry[pos].n = (Book->entry[pos].n + 1) / 2;
            Book->entry[pos].white_score = (Book->entry[pos].white_score + 1) / 2;
        }
    }
}

// keep_entry()

static bool keep_entry(int pos) {

    const entry_t * entry;
    int colour;
    double score;

    ASSERT(pos >= 0 && pos < Book->size);

    entry = &Book->entry[pos];

    // if (entry->n == 0) return false;
    if (entry->n < MinGame) return false;

    if (entry->white_score == 0) return false;

    score = (double(entry->white_score) / double(entry->n)) / 2.0;
    ASSERT(score >= 0.0 && score <= 1.0);

    if (score < MinScore) return false;

    colour = entry->colour;

    if ((RemoveWhite && colour_is_white(colour))
            || (RemoveBlack && colour_is_black(colour))) {
        return false;
    }

    return entry_score(entry) != 0; // REMOVE ME?

}

// entry_score()

static int entry_score(const entry_t * entry) {

    int score;

    ASSERT(entry != NULL);

    // score = entry->n; // popularity
    score = entry->white_score; // "expectancy"

    if (Uniform) score = 1;

    ASSERT(score >= 0);

    return score;
}

// key_compare()

static int key_compare(const void * p1, const void * p2) {

    const entry_t * entry_1, * entry_2;

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);

    entry_1 = (const entry_t *) p1;
    entry_2 = (const entry_t *) p2;

    if (entry_1->key > entry_2->key) {
        return +1;
    } else if (entry_1->key < entry_2->key) {
        return -1;
    } else {
        return entry_score(entry_2) - entry_score(entry_1); // highest score first
    }
}

// write_integer()

static void write_integer(FILE * file, int size, uint64 n) {

    int i;
    int b;

    ASSERT(file != NULL);
    ASSERT(size > 0 && size <= 8);
    ASSERT(size == 8 || n >> (size * 8) == 0);

    for (i = size - 1; i >= 0; i--) {
        b = (int) ((n >> (i * 8)) & 0xFF);
        ASSERT(b >= 0 && b < 256);
        fputc(b, file);
    }
}

// end of book_make.cpp

