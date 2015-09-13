
// book_make.cpp

// includes

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
    int n = 0;
    int white_score = 0;
    int draws = 0;

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
static std::map<uint64, entry_t> openingBook;

//static leveldb::DB *BookLevelDb;

// prototypes

static void book_clear();
static void book_insert(const char pgn_file_name[], const char level_db_file_name[]);

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


    printf("\nall done!\n");
}

static std::string game_info_to_string(const char* a, int i, const char* b) {
    std::ostringstream os;
    os << a << i << b;
    return os.str();
}


// book_clear()

static void book_clear() {

    openingBook.clear();
}


static void insert_into_leveldb(leveldb::DB* db, int passNumber) {
    printf("\nPutting games into leveldb..");
    leveldb::WriteBatch batch;

    leveldb::WriteOptions writeOptions;
    leveldb::ReadOptions readOptions;

//                writeOptions.disableWAL = true;
    
    for(auto const &it : openingBook) {
//        cout << "key: "<< it.first;
        
        const entry_t e = it.second;
        
        
        std::stringstream game_id_stream;
        std::string currentValue;

        for (set<int>::iterator it2 = e.game_ids->begin(); it2 != e.game_ids->end(); ++it2) {
            game_id_stream << *it2 << ",";
        }


        std::stringstream move_stream;
        for (set<uint16>::iterator it2 = e.moves->begin(); it2 != e.moves->end(); ++it2) {
            move_stream << *it2 << ",";
        }

        batch.Put(std::to_string(it.first)+"_p_"+std::to_string(passNumber), game_id_stream.str());
        batch.Put(std::to_string(it.first) + "_moves_p_"+std::to_string(passNumber), move_stream.str());

//                    counters.add((std::to_string(Book->entry[pos].key) + "_freq"), Book->entry[pos].n);
//                    counters.add((std::to_string(Book->entry[pos].key) + "_white_score"), Book->entry[pos].white_score);
//                    counters.add((std::to_string(Book->entry[pos].key) + "_draws"), Book->entry[pos].draws);
//                    
        batch.Put(std::to_string(it.first) + "_freq_p_"+std::to_string(passNumber), std::to_string(e.n));
        batch.Put(std::to_string(it.first) + "_white_score_p_"+std::to_string(passNumber), std::to_string(e.white_score));
        batch.Put(std::to_string(it.first) + "_draws_p_"+std::to_string(passNumber), std::to_string(e.draws));
        
        e.game_ids->clear(); 
        delete(e.game_ids);
        e.moves->clear();
        delete(e.moves);
    }

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
    int numSkippedGames = 0;
    leveldb::WriteOptions writeOptions;

    leveldb::DB* db;
    
    ASSERT(file_name != NULL);

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

    std::cout << leveldb_file_name << "\n";

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
                    ++numSkippedGames;
                }

            }
            catch (const std::invalid_argument& ex) {
                WhiteElo = MinElo;
                BlackElo = MinElo;
            }
            catch (const std::exception& ex) {
                WhiteElo = MinElo;
                BlackElo = MinElo;
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

                if(openingBook.find(board->key) == openingBook.end()) {
                    /*it doesn't exist in my_map*/
//                    entry_t* e = new entry_t;
                    entry_t e;

                    e.moves = new set<uint16>();
                    e.game_ids = new set<int>();
                            
                           
                    openingBook[board->key] = e;
                }

                openingBook[board->key].n++;

                openingBook[board->key].white_score += result;
                openingBook[board->key].draws += draw;
                openingBook[board->key].moves->insert(move);

                openingBook[board->key].game_ids->insert(game_nb);

                move_do(board, move);
                ply++;
                //            result = -result;
            }
        }

        game_nb++;
        if (game_nb % 10000 == 0) {
            printf("%d games ...\n", game_nb);
            if (numSkippedGames > 0) {
                printf("Skipped %d  games...\n", numSkippedGames);
            }
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
        printf("%lu entries.\n", openingBook.size());
    } else {
        printf("%lu entries.\n", openingBook.size());
        insert_into_leveldb(db, passNumber);
        db->Put(writeOptions, "total_game_count", std::to_string(game_nb + 1));
        db->Put(writeOptions, "pgn_filename", file_name);
        db->Put(writeOptions, "numPasses",  std::to_string(passNumber));

        delete db;

    }

    return;
}


// end of book_make.cpp

