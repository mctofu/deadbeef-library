# deadbeef-library

Library browser plugin for the DeaDBeeF audio player.

![image](https://user-images.githubusercontent.com/804610/103382115-9f239a00-4aa2-11eb-9bdd-f6f441a0b69c.png)

This allows browsing your music library by different dimensions.
- Directory structure
- Artist/Album
- Genre
- Release year
- File modification date

Any node from the browse tree can be added to your DeaDBeeF playlist. A text filter will narrow browsing options.

Check out the player itself here: https://deadbeef.sourceforge.io/

## Compiling

Build support is currently primitive. I've developed this on a Debian system and have not tested on any others.

First you need to build the [mctofu/musiclib-grpc](https://github.com/mctofu/musiclib-grpc) client library. Clone musiclib-grpc to an adjacent directory and run `cgo.sh` in that directory to build the C client.

Once the client library is built the `build.sh` script in this repo will build the plugin and attempt to copy it into your DeaDBeeF plugin directory. Restarting DeaDBeeF should recognize it.

## External libraries

A music library is hosted as an external process that the plugin connects to using [grpc](https://github.com/grpc/grpc). One library implementation is [mctofu/musiclib](https://github.com/mctofu/musiclib) but any implementation of [mctofu/musiclib-grpc](https://github.com/mctofu/musiclib-grpc) could work.

The library is expected to be listening on `127.0.0.1:8337` (hardcoded for now).

## Notes

I don't have much recent C development experience and I had zero DeaDBeeF or GTK development experience before working on this. I got it working but I suspect some things are not as good as they could be.

## Credits

This plugin is heavily based on:

Filebrowser plugin for the DeaDBeeF audio player
http://sourceforge.net/projects/deadbeef-fb/

Copyright (C) 2011-2016 Jan D. Behrens <zykure@web.de>
