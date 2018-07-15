# DriveFS

DriveFS allows you to mount Google Drive's as a local filesystem. It is written entirely in C++ to reduce the overhead of crossing language boundaries between NodeJS, Python and GO.

DriveFS supports:
- Reading: Caching in memory and on drive.
- Writing: full files only, no partial editing
- Supports fuse2 and fuse3: asynchronous IO with the low level api.


# Installation
Check the wki [for compiling the code](https://github.com/thejinx0r/DriveFS/wiki/Compiling).

# Usage
Command line interface (see below) or use a [configuration file](https://github.com/thejinx0r/DriveFS/wiki/Sample-Config)
```
Usage:
./DriveFS mountpoint [options]

Allowed Options:                                                                                

General options:
  -h [ --help ]                         t his help message
  -c [ --config-file ] arg              path to a config file. arguments should
                                        be one per line
  --mount arg                           set the mount point. useful for config
                                        files
  --database arg (=mongodb://localhost/)
                                        set the database path
  --cache-location arg (=/tmp/DriveFS)
  --cache-chunk-size arg (=8388608)     size of segments to download, in bytes,
                                        default: 8MB
  --cache-size arg (=512)               maximum amount of memory to be used for
                                        in memory cache. values in MB. Default:
                                        512MB
  --cache-disk-size arg (=512)          maximum size of the cache on disk, in
                                        megabytes. only for downloads, defaults
                                        to 512MB
  --download-chunks arg (=4)            maximum number of chunks to download
                                        ahead
  --download-last-chunk arg (=1)        download the last chunk of a file when
                                        downloading the first chunk
  --move-to-download arg (=1)           move a uploaded file to the download
                                        cache
  --max-concurrent-downloads arg        max number of concurrent downloads,
                                        defaults to 2
  --max-concurrent-uploads arg          max number of concurrent uploads,
                                        defaults to 2

Fuse Optionns:
  -f [ --fuse-foreground ]              run the fuse application in foreground
                                        instead of a daemon
  -d [ --fuse-debug ]                   run the fuse application in debug mode
  --fuse-allow-other                    set the allow_other option for fuse
  --fuse-default-permissions            set the default_permissions for fuse
  -s [ --fuse-singlethread ]            use a single thread for the fuse event
                                        loop

Log Options:
  --log-location arg (=/tmp/DriveFS.log)
                                        sets the location for the log file
  --log-max-size arg (=104857600)       sets the maximum log size, in bytes.
                                        default is 100M
  -v [ --log-verbose ] [=arg(=9)]       log verbosee. if no  value is passed,
                                        log maximum verbose. valid values:
                                        [0-9]


```


