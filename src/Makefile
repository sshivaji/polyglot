
# files

EXE = polyglot

OBJS = adapter.o attack.o board.o book.o book_make.o book_merge.o colour.o \
       engine.o epd.o fen.o game.o hash.o io.o line.o list.o main.o move.o \
       move_do.o move_gen.o move_legal.o option.o parse.o pgn.o piece.o \
       posix.o random.o san.o search.o square.o uci.o util.o

PREFIX = /usr
BINDIR = $(PREFIX)/bin

# rules

all: $(EXE) .depend

clean:
	$(RM) *.o .depend

install: all
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

uninstall:
	 $(RM) $(BINDIR)/$(EXE)

# general

CXX       = g++ -std=c++11
CXXFLAGS  = -pipe
LDFLAGS   = -lm
LATE_LD_FLAGS = -lhyperleveldb
#-lrocksdb -lpthread -lz -lbz2 -lsnappy -lleveldb
# C++

CXXFLAGS += -fno-exceptions -fno-rtti

# optimisation

CXXFLAGS += -O2
CXXFLAGS += -fomit-frame-pointer

# strip

#LDFLAGS  += -s

# dependencies

$(EXE): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LATE_LD_FLAGS)

%.s: %.cpp
	$(CXX) -S $(CXXFLAGS) $<

.depend:
	$(CXX) -MM $(OBJS:.o=.cpp) > $@
#include .depend
