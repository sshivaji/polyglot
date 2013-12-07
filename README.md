polyglot
========

This is the open source polyglot chess opening book utility. I could not find this on github anywhere. Changes to support game indexing via leveldb have been added.
Fabien Letouzey's original readme is in readme.txt.

New Features:
Builds a polyglot book but supports a leveldb position/game index as well. The leveldb option can be enabled with -leveldb

Compile:
 1. Install leveldb library (for mac OS X, brew install leveldb)
 2. Execute make

Usage:
./polyglot make-book -pgn \<pgn\_file\> -leveldb \<leveldb\_dir\_name\> -min-game 1

Builds a leveldb game number and header index. To access the indexes:

 1. To access the game header index:
	The "game\_\<game\_number\>\_data" key contains a pipe (|) separated list of values: 
  1. White player name 
  1. White player elo 
  1. Black player name 
  1. Black player elo 
  1. result (1-0, 0-1, 1/2-1/2 etc)
  1. data
  1. site
  1. eco
  1. last\_stream\_position
  1. fen
	Most of the fields are self-explanatory, the last\_stream\_position key contains the fseek position of the game (similar to the byte position of the game) in the PGN file. This is useful when wanting to quickly retrieve that game. To optimize retrieval, look up the last\_stream\_position of the next game and get all content between the fseeks. Note: The fseek content does not start with the opening "[", one needs to add in an opening "[" after the seek to work with PGN parsers.

 2. Position index:
	To look up the games referenced by a position:
  1. Compute the polyglot hash for a board position. Look up the position_hash as a key, a list of comma separated values containing game ids will be returned. Note: There will a trailing comma after the last game. The code reading the list of games has to ignore the last comma.
  2. The games are not in any order.

 3. Additional metadata can be accessed via:
	"total\_game\_count" contains the total number of games in the pgn file.
	"pgn\_filename" contains the name of the original PGN file.
	
 4. Additional notes:
  1. The leveldb game indexing is currently done in RAM. Later versions will be done without RAM. Thus, the RAM needed is similar to polyglot's typical RAM needs.
  2. The kivy-chess github project currently uses the leveldb indexes.
