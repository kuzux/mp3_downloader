# mp3_downloader

Simple mp3 downloader application written using sockets. Important point: (mostly) written in one night, while 
wearing a suit and having no internet connection. Requires `libsqlite3` and `libreadline` to compile and work, Otherwise,
plain C should suffice, nothing fancy was used. Developed and tested on an Ubuntu 13.10 box, using GCC 
(don't remember which version, but probably the one that comes with ubuntun 13.10). Untested, but should work on OSX as well.
Just use `make` to build the binaries and two programs (one for client and one for the server) shpuld pop up.
