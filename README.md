# mp3_downloader

Simple mp3 downloader application written using sockets. Important point: (mostly) written in one night, while 
wearing a suit and having no internet connection. Requires `libsqlite3` and `libedit` to compile and work, Otherwise,
plain C should suffice, nothing fancy was used. Developed and tested on an Ubuntu 13.10 box, using GCC 
(don't remember which version, but probably the one that comes with ubuntu 13.10). Also tested, it compiles oon works on OSX Yosemite, using clang-700.1.81.
Just use `make` to build the binaries and two programs (one for client and one for the server) should pop up.
