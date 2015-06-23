
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
static int MinGame;
static double MinScore;
static bool RemoveWhite, RemoveBlack;
static bool Uniform;

static enum STORAGE {
    POLYGLOT, LEVELDB
} Storage;

static book_t Book[1];
//static rocksdb::DB *BookLevelDb;

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

class SetCounters {
protected:
    std::shared_ptr<rocksdb::DB> db_;

    rocksdb::WriteOptions put_option_;
    rocksdb::ReadOptions get_option_;
    rocksdb::WriteOptions delete_option_;

    std::set<int> default_;

public:

    explicit SetCounters(std::shared_ptr<rocksdb::DB> db, std::set<int> defaultSet)
    : db_(db),
    put_option_(),
    get_option_(),
    delete_option_(),
    default_(defaultSet) {
        assert(db_);
    }

    virtual ~SetCounters() {
    }

    // public interface of Counters.
    // All four functions return false
    // if the underlying level db operation failed.

    // mapped to a levedb Put

    bool set(const string& key, int value) {
        // just treat the internal rep of int32 as the string
        rocksdb::Slice slice((char *) &value, sizeof (value));
//        
//        rocksdb::Status s = db_->Get(readOptions, std::to_string(Book->entry[pos].key), &currentValue);
//                    if (s.ok()) {
//                        game_id_stream << currentValue;
//                    }
        
        auto s = db_->Put(put_option_, key, slice);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << endl;
            return false;
        }
    }

    // mapped to a rocksdb Delete

    bool remove(const string& key) {
        auto s = db_->Delete(delete_option_, key);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << std::endl;
            return false;
        }
    }

    // mapped to a rocksdb Get

    bool get(const string& key, int *value) {
        string str;
        auto s = db_->Get(get_option_, key, &str);

        if (s.IsNotFound()) {
            // return default value if not found;
//            *value = default_;
            return true;
        } else if (s.ok()) {
            // deserialization
            if (str.size() != sizeof (int)) {
                cerr << "value corruption\n";
                return false;
            }
            *value = rocksdb::DecodeFixed32(&str[0]);
            return true;
        } else {
            cerr << s.ToString() << std::endl;
            return false;
        }
    }

    // 'add' is implemented as get -> modify -> set
    // An alternative is a single merge operation, see MergeBasedCounters

    virtual bool add(const string& key, int value) {
        return false;
//        int base = default_;
//        return get(key, &base) && set(key, base + value);
    }

};

// Implement 'add' directly with the new Merge operation

class MergeBasedSetCounters : public SetCounters {
private:
    rocksdb::WriteOptions merge_option_; // for merge

public:

    explicit MergeBasedSetCounters(std::shared_ptr<rocksdb::DB> db, std::set<int> defaultSet)
    : SetCounters(db, defaultSet),
    merge_option_() {
    }

    // mapped to a rocksdb Merge operation

    virtual bool add(const string& key, int value) override {
        char encoded[sizeof (int)];
        rocksdb::EncodeFixed32(encoded, value);
        rocksdb::Slice slice(encoded, sizeof (int));
        auto s = db_->Merge(merge_option_, key, slice);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << endl;
            return false;
        }
    }
};


// A 'model' merge operator with uint64 addition semantics
// Implemented as an AssociativeMergeOperator for simplicity and example.

class SetAddOperator : public rocksdb::AssociativeMergeOperator {
public:

    virtual bool Merge(const rocksdb::Slice& key,
            const rocksdb::Slice* existing_value,
            const rocksdb::Slice& value,
            std::string* new_value,
            rocksdb::Logger* logger) const override {
        int orig_value = 0;
        if (existing_value) {
            orig_value = DecodeInteger32(*existing_value, logger);
        }
        int operand = DecodeInteger32(value, logger);

        assert(new_value);
        new_value->clear();

        rocksdb::PutFixed32(new_value, orig_value + operand);

        return true; // Return true always since corruption will be treated as 0
    }

    virtual const char* Name() const override {
        return "SetAddOperator";
    }

private:
    // Takes the string and decodes it into a uint64_t
    // On error, prints a message and returns 0

    int DecodeInteger32(const rocksdb::Slice& value, rocksdb::Logger* logger) const {
        int result = 0;

        if (value.size() == sizeof (int)) {
            result = rocksdb::DecodeFixed32(value.data());
        } else if (logger != nullptr) {
            // If value is corrupted, treat it as 0
            rocksdb::Log(rocksdb::InfoLogLevel::ERROR_LEVEL, logger,
                    "uint32 value corruption, size: %zu > %zu",
                    value.size(), sizeof (int));
        }

        return result;
    }

};


// Imagine we are maintaining a set of uint32 counters. Example taken from rocksdb merge example
// Each counter has a distinct name. And we would like
// to support four high level operations:
// set, add, get and remove
// This is a quick implementation without a Merge operation.

class Counters {
protected:
    std::shared_ptr<rocksdb::DB> db_;

    rocksdb::WriteOptions put_option_;
    rocksdb::ReadOptions get_option_;
    rocksdb::WriteOptions delete_option_;

    uint32_t default_;

public:

    explicit Counters(std::shared_ptr<rocksdb::DB> db, uint32_t defaultCount = 0)
    : db_(db),
    put_option_(),
    get_option_(),
    delete_option_(),
    default_(defaultCount) {
        assert(db_);
    }

    virtual ~Counters() {
    }

    // public interface of Counters.
    // All four functions return false
    // if the underlying level db operation failed.

    // mapped to a levedb Put

    bool set(const string& key, uint32_t value) {
        // just treat the internal rep of int32 as the string
        rocksdb::Slice slice((char *) &value, sizeof (value));
        auto s = db_->Put(put_option_, key, slice);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << endl;
            return false;
        }
    }

    // mapped to a rocksdb Delete

    bool remove(const string& key) {
        auto s = db_->Delete(delete_option_, key);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << std::endl;
            return false;
        }
    }

    // mapped to a rocksdb Get

    bool get(const string& key, uint32_t *value) {
        string str;
        auto s = db_->Get(get_option_, key, &str);

        if (s.IsNotFound()) {
            // return default value if not found;
            *value = default_;
            return true;
        } else if (s.ok()) {
            // deserialization
            if (str.size() != sizeof (uint32_t)) {
                cerr << "value corruption\n";
                return false;
            }
            *value = rocksdb::DecodeFixed32(&str[0]);
            return true;
        } else {
            cerr << s.ToString() << std::endl;
            return false;
        }
    }

    // 'add' is implemented as get -> modify -> set
    // An alternative is a single merge operation, see MergeBasedCounters

    virtual bool add(const string& key, uint32_t value) {
        uint32_t base = default_;
        return get(key, &base) && set(key, base + value);
    }


    // convenience functions for testing

    void assert_set(const string& key, uint32_t value) {
        assert(set(key, value));
    }

    void assert_remove(const string& key) {
        assert(remove(key));
    }

    uint32_t assert_get(const string& key) {
        uint32_t value = default_;
        int result = get(key, &value);
        assert(result);
        if (result == 0) exit(1); // Disable unused variable warning.
        return value;
    }

    void assert_add(const string& key, uint32_t value) {
        int result = add(key, value);
        assert(result);
        if (result == 0) exit(1); // Disable unused variable warning.
    }
};

// Implement 'add' directly with the new Merge operation

class MergeBasedCounters : public Counters {
private:
    rocksdb::WriteOptions merge_option_; // for merge

public:

    explicit MergeBasedCounters(std::shared_ptr<rocksdb::DB> db, uint32_t defaultCount = 0)
    : Counters(db, defaultCount),
    merge_option_() {
    }

    // mapped to a rocksdb Merge operation

    virtual bool add(const string& key, uint32_t value) override {
        char encoded[sizeof (uint32_t)];
        rocksdb::EncodeFixed32(encoded, value);
        rocksdb::Slice slice(encoded, sizeof (uint32_t));
        auto s = db_->Merge(merge_option_, key, slice);

        if (s.ok()) {
            return true;
        } else {
            cerr << s.ToString() << endl;
            return false;
        }
    }
};


// A 'model' merge operator with uint64 addition semantics
// Implemented as an AssociativeMergeOperator for simplicity and example.

class UInt32AddOperator : public rocksdb::AssociativeMergeOperator {
public:

    virtual bool Merge(const rocksdb::Slice& key,
            const rocksdb::Slice* existing_value,
            const rocksdb::Slice& value,
            std::string* new_value,
            rocksdb::Logger* logger) const override {
        uint32_t orig_value = 0;
        if (existing_value) {
            orig_value = DecodeInteger32(*existing_value, logger);
        }
        uint32_t operand = DecodeInteger32(value, logger);

        assert(new_value);
        new_value->clear();

        rocksdb::PutFixed32(new_value, orig_value + operand);

        return true; // Return true always since corruption will be treated as 0
    }

    virtual const char* Name() const override {
        return "UInt32AddOperator";
    }

private:
    // Takes the string and decodes it into a uint64_t
    // On error, prints a message and returns 0

    uint32_t DecodeInteger32(const rocksdb::Slice& value, rocksdb::Logger* logger) const {
        uint32_t result = 0;

        if (value.size() == sizeof (uint32_t)) {
            result = rocksdb::DecodeFixed32(value.data());
        } else if (logger != nullptr) {
            // If value is corrupted, treat it as 0
            rocksdb::Log(rocksdb::InfoLogLevel::ERROR_LEVEL, logger,
                    "uint32 value corruption, size: %zu > %zu",
                    value.size(), sizeof (uint32_t));
        }

        return result;
    }

};


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
    MinGame = 3;
    MinScore = 0.0;
    RemoveWhite = false;
    RemoveBlack = false;
    Uniform = false;
    Storage = POLYGLOT;

    for (i = 1; i < argc; i++) {

        if (false) {

        } else if (my_string_equal(argv[i], "make-book")) {

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
        } else {
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


    printf("all done!\n");
}

static std::string game_info_to_string(const char* a, int i, const char* b) {
    std::ostringstream os;
    os << a << i << b;
    return os.str();
}


// book_clear()

static void book_clear() {

    //  if (Storage == LEVELDB) {
    //      rocksdb::Options options;
    //      options.create_if_missing = true;
    //      rocksdb::DB::Open(options, bin_file, &BookLevelDb);
    //    }
    //  else {
    int index;

    Book->alloc = 1;
    Book->mask = (Book->alloc * 2) - 1;

    Book->entry = (entry_t *) my_malloc(Book->alloc * sizeof (entry_t));
    Book->size = 0;

    Book->hash = (sint32 *) my_malloc((Book->alloc * 2) * sizeof (sint32));
    for (index = 0; index < Book->alloc * 2; index++) {
        Book->hash[index] = NIL;
    }
    //    }
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
    rocksdb::WriteOptions writeOptions;


    rocksdb::DB* db;
    ASSERT(file_name != NULL);


//    if (leveldb_file_name != NULL) {

    rocksdb::Options options;
    options.create_if_missing = true;
    options.merge_operator.reset(new UInt32AddOperator);

    //       MergeBasedCounters counters(db);

    unsigned concurrentThreadsSupported = std::thread::hardware_concurrency();
    if (concurrentThreadsSupported != 0) {
        // Adjust for hyperthreading
        if (concurrentThreadsSupported >= 4) {
            concurrentThreadsSupported /= 2;
        }
        cout << "Using " << concurrentThreadsSupported << " threads\n";
        options.max_background_compactions = concurrentThreadsSupported;
    }
    rocksdb::Status status = rocksdb::DB::Open(options, leveldb_file_name, &db);
    std::shared_ptr<rocksdb::DB> dc (db);
    MergeBasedCounters counters(dc);
//        counters.add("a", 1);
    std::cout << leveldb_file_name << "\n";
    //       assert(status.ok());

//    }

    // scan loop

    pgn_open(pgn, file_name);

    while (pgn_next_game(pgn)) {

        board_start(board);
        ply = 0;
        result = 0;
        draw = 0;

        if (false) {
        } else if (my_string_equal(pgn->result, "1-0")) {
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

            if (ply < MaxPly) {

                move = move_from_san(string, board);

                if (move == MoveNone || !move_is_legal(move, board)) {
                    my_log("book_insert(): illegal move \"%s\" at line %d, column %d\n", string, pgn->move_line, pgn->move_column);
                }

                //            if (leveldb_file_name==NULL) {

                pos = find_entry(board);

                Book->entry[pos].n++;
                Book->entry[pos].white_score += result;
                Book->entry[pos].draws += draw;

                Book->entry[pos].moves->insert(move);
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

        //      TODO: Make it mem efficient

        if (game_nb % 10000 == 0) {
            if (leveldb_file_name != NULL) {
                printf("\nPutting games into leveldb..");
                rocksdb::WriteBatch batch;

                rocksdb::WriteOptions writeOptions;
                rocksdb::ReadOptions readOptions;

                writeOptions.disableWAL = true;

                for (pos = 0; pos < Book->size; pos++) {

                    std::stringstream game_id_stream;

                    std::string currentValue;
                    rocksdb::Status s = db->Get(readOptions, std::to_string(Book->entry[pos].key), &currentValue);
                    if (s.ok()) {
                        game_id_stream << currentValue;
                    }

                    for (set<int>::iterator it = Book->entry[pos].game_ids->begin(); it != Book->entry[pos].game_ids->end(); ++it) {
                        game_id_stream << *it << ",";
                    }

                    currentValue.clear();
                    s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_moves", &currentValue);
                    if (s.ok()) {
                        //                    cout << "OK..";
                        //                    cout << currentValue;
                        std::istringstream ss(currentValue);
                        std::string token;

                        while (std::getline(ss, token, ',')) {
                            Book->entry[pos].moves->insert(std::stoi(token));
                        }
                    }

                    std::stringstream move_stream;
                    for (set<uint16>::iterator it = Book->entry[pos].moves->begin(); it != Book->entry[pos].moves->end(); ++it) {
                        move_stream << *it << ",";
                    }


                    batch.Put(std::to_string(Book->entry[pos].key), game_id_stream.str());
                    batch.Put(std::to_string(Book->entry[pos].key) + "_moves", move_stream.str());

//                    currentValue.clear();
//                    s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_freq", &currentValue);
//                    if (s.ok()) {
////                        cout << "Current Value FREQ: " << currentValue << "\n";
//
//                        Book->entry[pos].n += std::stoi(currentValue);
//                    }
//                    
//                    currentValue.clear();
//                    s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_white_score", &currentValue);
//                    if (s.ok()) {
////                        cout << "White score : "<<Book->entry[pos].white_score << "\n";
////                        cout << "Current Value White Score: " << currentValue << "\n";
//
//                        Book->entry[pos].white_score += std::stoi(currentValue);
//                    }
//                    
//                    currentValue.clear();
//                    s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_draws", &currentValue);
//                    if (s.ok()) {
////                        cout << "Current Value DRAWS: " << currentValue << "\n";
//
//                        Book->entry[pos].draws += std::stoi(currentValue);
//                    }
                    
                    
                    counters.add((std::to_string(Book->entry[pos].key) + "_freq"), Book->entry[pos].n);
                    counters.add((std::to_string(Book->entry[pos].key) + "_white_score"), Book->entry[pos].white_score);
                    counters.add((std::to_string(Book->entry[pos].key) + "_draws"), Book->entry[pos].draws);
                    
//                    batch.Put(std::to_string(Book->entry[pos].key) + "_freq", std::to_string(Book->entry[pos].n));
                    
//                    cout << "White score inserted.. " << Book->entry[pos].white_score << "\n";
//                    batch.Put(std::to_string(Book->entry[pos].key) + "_white_score", std::to_string(Book->entry[pos].white_score));
//                    batch.Put(std::to_string(Book->entry[pos].key) + "_draws", std::to_string(Book->entry[pos].draws));;



                }
                //            batch.Clear();
                book_clear();

                db->Write(rocksdb::WriteOptions(), &batch);
                batch.Clear();
            }
        }
    }

    pgn_close(pgn);

    printf("%d game%s.\n", game_nb + 1, (game_nb > 1) ? "s" : "");
    if (leveldb_file_name == NULL) {
        printf("%d entries.\n", Book->size);
    } else {
        printf("Iterating thru all book positions..");
        printf("\nPutting games into leveldb..");
        rocksdb::WriteBatch batch;

        rocksdb::WriteOptions writeOptions;
        rocksdb::ReadOptions readOptions;

        writeOptions.disableWAL = true;

        for (pos = 0; pos < Book->size; pos++) {

            std::stringstream game_id_stream;

            std::string currentValue;
            rocksdb::Status s = db->Get(readOptions, std::to_string(Book->entry[pos].key), &currentValue);
            if (s.ok()) {
                game_id_stream << currentValue;
            }

            for (set<int>::iterator it = Book->entry[pos].game_ids->begin(); it != Book->entry[pos].game_ids->end(); ++it) {
                game_id_stream << *it << ",";
            }

            s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_moves", &currentValue);
            if (s.ok()) {

                std::istringstream ss(currentValue);
                std::string token;

                //                    while(std::getline(ss, token, ',')) {
                //                        Book->entry[pos].moves->insert(std::stoi(token));
                //                    }
            }

            std::stringstream move_stream;
            for (set<uint16>::iterator it = Book->entry[pos].moves->begin(); it != Book->entry[pos].moves->end(); ++it) {
                move_stream << *it << ",";
            }

            batch.Put(std::to_string(Book->entry[pos].key), game_id_stream.str());
            batch.Put(std::to_string(Book->entry[pos].key) + "_moves", move_stream.str());


//            s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_freq", &currentValue);
//            if (s.ok()) {
//                Book->entry[pos].n += std::stoi(currentValue);
//            }
//            s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_white_score", &currentValue);
//            if (s.ok()) {
//                Book->entry[pos].white_score += std::stoi(currentValue);
//            }
//            s = db->Get(readOptions, std::to_string(Book->entry[pos].key) + "_draws", &currentValue);
//            if (s.ok()) {
//                Book->entry[pos].draws += std::stoi(currentValue);
//            }
//
//            batch.Put(std::to_string(Book->entry[pos].key) + "_freq", std::to_string(Book->entry[pos].n));
//            batch.Put(std::to_string(Book->entry[pos].key) + "_white_score", std::to_string(Book->entry[pos].white_score));
//            batch.Put(std::to_string(Book->entry[pos].key) + "_draws", std::to_string(Book->entry[pos].draws));
//            
            
                    
            counters.add((std::to_string(Book->entry[pos].key) + "_freq"), Book->entry[pos].n);
            counters.add((std::to_string(Book->entry[pos].key) + "_white_score"), Book->entry[pos].white_score);
            counters.add((std::to_string(Book->entry[pos].key) + "_draws"), Book->entry[pos].draws);
                    

        }
        //            batch.Clear();
        book_clear();

        db->Write(rocksdb::WriteOptions(), &batch);
        batch.Clear();


        db->Put(writeOptions, "total_game_count", std::to_string(game_nb + 1));
        db->Put(writeOptions, "pgn_filename", file_name);

//        delete db;
//                                cout << "Before clearing book";

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
//   rocksdb::WriteOptions writeOptions = rocksdb::WriteOptions();
//   rocksdb::DB* BookLevelDb;
//
//    if (leveldb_file != NULL) {
//        rocksdb::Options options;
//        options.create_if_missing = true;
//        rocksdb::Status status = rocksdb::DB::Open(options, leveldb_file, &BookLevelDb);
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
//            rocksdb::Status s = BookLevelDb->Get(rocksdb::ReadOptions(), uint64_to_string(Book->entry[pos].key), &currentValue);
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
    for (index = key & Book->mask; (pos = Book->hash[index]) != NIL; index = (index + 1) & Book->mask) {

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

        for (index = key & Book->mask; Book->hash[index] != NIL; index = (index + 1) & Book->mask)
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
    Book->entry[pos].colour = board->turn;

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
    Book->mask = (Book->alloc * 2) - 1;

    size = 0;
    size += Book->alloc * sizeof (entry_t);
    size += (Book->alloc * 2) * sizeof (sint32);

    if (size >= 1048576) printf("allocating %gMB ...\n", double(size) / 1048576.0);

    // resize arrays

    Book->entry = (entry_t *) my_realloc(Book->entry, Book->alloc * sizeof (entry_t));
    Book->hash = (sint32 *) my_realloc(Book->hash, (Book->alloc * 2) * sizeof (sint32));

    // rebuild hash table

    for (index = 0; index < Book->alloc * 2; index++) {
        Book->hash[index] = NIL;
    }

    for (pos = 0; pos < Book->size; pos++) {

        for (index = Book->entry[pos].key & Book->mask; Book->hash[index] != NIL; index = (index + 1) & Book->mask)
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

    for (index = key & Book->mask; (pos = Book->hash[index]) != NIL; index = (index + 1) & Book->mask) {

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

    if (entry_score(entry) == 0) return false; // REMOVE ME?

    return true;
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
        b = (n >> (i * 8)) & 0xFF;
        ASSERT(b >= 0 && b < 256);
        fputc(b, file);
    }
}

// end of book_make.cpp

