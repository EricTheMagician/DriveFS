#include <iostream>
#include "BaseAccount.h"
#include "gdrive/Account.h"
#include "gdrive/Filesystem.h"
#include <easylogging++.h>
#include <boost/program_options.hpp>
#include "gdrive/FileIO.h"
#include "gdrive/File.h"

namespace po = boost::program_options;

INITIALIZE_EASYLOGGINGPP;

int main(int argc, char **argv) {

    po::options_description desc("General options"), log_desc("Log Options"), fuse_desc("Fuse Optionns");
    desc.add_options()
            ("help,h", "this help message")
            ("config-file,c", po::value<std::string>(), "path to a config file. arguments should be one per line")
            ("mount", po::value<std::string>(), "set the mount point. useful for config files")
            ("database", po::value<std::string>()->default_value("mongodb://localhost/"), "set the database path")
            ("cache-location", po::value<std::string>()->default_value("/tmp/DriveFS"))
            ("cache-chunk-size", po::value<size_t>()->default_value(8*1024*1024), "size of segments to download, in bytes, default: 8MB")
            ("cache-size", po::value<size_t>()->default_value(512), "maximum amount of memory to be used for in memory cache. values in MB. Default: 512MB")
            ("cache-disk-size", po::value<size_t>(), "maximum size of the cache on disk. only for downloads. currently not used.")
            ("download-chunks", po::value<int>()->default_value(4), "maximum number of chunks to download ahead")
            ("download-last-chunk", po::value<bool>()->default_value(true), "download the last chunk of a file when downloading the first chunk")
            ("move-to-download", po::value<bool>()->default_value(true), "move a uploaded file to the download cache")
            ;

    fuse_desc.add_options()
            ("fuse-foreground,f", "run the fuse application in foreground instead of a daemon")
            ("fuse-debug,d", "run the fuse application in debug mode")
            ("fuse-allow-other", "set the allow_other option for fuse")
            ("fuse-default-permissions", "set the default_permissions for fuse")
            ("fuse-singlethread,s", "use a single thread for the fuse event loop")
            ;

    log_desc.add_options()
            ("log-location", po::value<std::string>()->default_value("/tmp/DriveFS.log"), "sets the location for the log file")
            ("log-max-size", po::value<std::string>()->default_value("104857600"),"sets the maximum log size, in bytes. default is 100M")
            ("log-verbose,v", po::value<int>()->default_value(1)->implicit_value(9), "log verbosee. if no  value is passed, log maximum verbose. valid values: [0-9]")
            ;

    po::positional_options_description p;
    p.add("mount", 1).add("other",-1);

    po::variables_map vm;
    po::options_description all_desc("Allowed Options");
    all_desc.add(desc).add(fuse_desc).add(log_desc);

    po::store(po::command_line_parser(argc, argv).
            options(all_desc).positional(p).run(), vm);

    if(vm.count("config-file")){
        std::ifstream ifs{vm["config-file"].as<std::string>().c_str()};
        if (ifs)
            store(po::parse_config_file(ifs, all_desc), vm);
    }

    po::notify(vm);

    /***************
     *
     * Help section
     *
     **************/

    if (vm.count("help")) {
        std::cout << argv[0] << " mountpoint [options]" << "\n";
        std::cout << all_desc << "\n";
        return 0;
    }


    /******************
     *
     * Setup the logger / easylogging
     *
     ****************/

    const char *v1 = "DriveFS\0";
    char **v = nullptr;
    if(vm.count("log-verbose")){
        v = ( char **) malloc(sizeof(char * ) * 2);
        v[0] = (char *)v1;
        auto str = std::to_string(vm["log-verbose"].as<int>());
        v[1] = (char *) str.c_str();
        START_EASYLOGGINGPP(2, v);
    }else{
        v = ( char **) malloc(sizeof(char * ) * 1);
        v[0] = (char *)v1;
        START_EASYLOGGINGPP(1, v);
    }


    el::Configurations defaultConf;
    defaultConf.setToDefault();
    defaultConf.setGlobally(el::ConfigurationType::Format, "%datetime [%levshort] [%fbase:%line] %msg");
    const std::string log_location = vm["log-location"].as<std::string>();
    if(log_location.empty()) {
        defaultConf.setGlobally(el::ConfigurationType::ToFile, "false");
    }else {
        defaultConf.setGlobally(el::ConfigurationType::ToFile, "true");
        defaultConf.setGlobally(el::ConfigurationType::Filename, vm["log-location"].as<std::string>());
        defaultConf.setGlobally(el::ConfigurationType ::MaxLogFileSize,  vm["log-max-size"].as<std::string>());
    }

    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Loggers::reconfigureLogger("default", defaultConf);
    el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);

    /*********************
     *
     * Set up the FileIO and cache locations
     *
     ********************/

//            ("cache-size", po::value<size_t>()->default_value(512), "maximum amount of memory to be used for in memory cache. values in MB. Default: 512MB")
//            ("cache-disk-size", po::value<size_t>(), "maximum size of the cache on disk. only for downloads. currently not used.")

    DriveFS::FileIO::download_last_chunk_at_the_beginning = vm["download-last-chunk"].as<bool>();
    DriveFS::FileIO::setCachePath(vm["cache-location"].as<std::string>());
    DriveFS::FileIO::number_of_blocks_to_read_ahead = vm["download-chunks"].as<int>();
    DriveFS::FileIO::block_download_size = vm["cache-chunk-size"].as<size_t>();
    DriveFS::FileIO::block_read_ahead_end = std::min(DriveFS::FileIO::block_download_size + 128 * 1024, DriveFS::FileIO::block_download_size-1) ;
    DriveFS::FileIO::move_files_to_download_on_finish_upload = vm["move-to-download"].as<bool>();

    DriveFS::_Object::cache.m_block_download_size = DriveFS::FileIO::block_download_size;
    DriveFS::_Object::cache.maxCacheSize = vm["cache-size"].as<size_t>() *1024*1024;

    /************************
     *
     * Setup the user account
     *
     ***********************/
    File::executing_uid = geteuid();
    File::executing_gid = getegid();

    DriveFS::Account account = DriveFS::Account::getAccount(
            vm["database"].as<std::string>()
    );

    if(account.needToInitialize()) {
        account.run();
    }

    /****************************
     *
     * Setup the fuse filesystem
     *
     ****************************/


    std::vector<const char *>fuse_args;

    auto filesystemName = "DriveFS";
    fuse_args.push_back(&filesystemName[0]);
    if(vm.count("fuse-foreground")){
        LOG(TRACE) << "will run fuse in foreground";
        fuse_args.push_back("-f");
    }
    if(vm.count("fuse-debug")){
        LOG(TRACE) << "will run fuse in debug";
        fuse_args.push_back("-d");
    }
    if(vm.count("fuse-allow-other")){
        LOG(TRACE) << "will run fuse with allow_other";
        fuse_args.push_back("-o");
        fuse_args.push_back("allow_other");
    }

    if(vm.count("fuse-default-permissions")){
        LOG(TRACE) << "will run fuse with allow_other";
        fuse_args.push_back("-o");
        fuse_args.push_back("default_permissions");
    }


    fuse_args.push_back("-o");
    fuse_args.push_back("noatime");

#if FUSE_USE_VERSION < 30
    if(vm["cache-chunk-size"].as<size_t>() >= 524289) { //512kb+1b
        fuse_args.push_back("-o");
        fuse_args.push_back("max_readahead=1048576"); // 1MB
    }
#endif

    auto s_mountpoint = vm["mount"].as<std::string>();
    fuse_args.push_back(s_mountpoint.c_str());

    struct fuse_args args = FUSE_ARGS_INIT(fuse_args.size(), (char **) fuse_args.data());
    struct fuse_lowlevel_ops ops = DriveFS::getOps();

#if FUSE_USE_VERSION >= 30
    struct fuse_cmdline_opts opts;
    if( fuse_parse_cmdline(&args, &opts) != 0){
        return -1;
    }
    account.fuse_session = fuse_session_new(&args,&ops, sizeof(ops), &account);
    if (account.fuse_session == NULL)
        goto err_out1;

    if (account.fuse_session == NULL)
        goto err_out1;
    if (fuse_set_signal_handlers(account.fuse_session) != 0)
        goto err_out2;

    fuse_session_mount(account.fuse_session, opts.mountpoint);
    fuse_daemonize(opts.foreground);
    if(opts.singlethread){
        fuse_session_loop(account.fuse_session);
    }else {
        fuse_session_loop_mt(account.fuse_session,1);
    }

#else
    char * mountpoint;
    int foreground, multithreaded;
    int ret = fuse_parse_cmdline(&args, &mountpoint,
                                 &multithreaded, &foreground);
    if (ret == -1) {
        LOG(ERROR) << "Error parsing fuse options " << strerror(errno);
        throw std::runtime_error("Error parsing fuse options");
    }

    account.fuse_channel= fuse_mount((const char*) mountpoint, &args);
    if (account.fuse_channel== nullptr) {
        LOG(ERROR) << "Unable to mount filesystem: " <<  strerror(errno);
        return -1;
    }

    account.fuse_session = fuse_lowlevel_new(&args, &ops,
                                         sizeof(ops), &account);

    if (account.fuse_session == NULL)
        goto err_out1;
    fuse_session_add_chan(account.fuse_session, account.fuse_channel);

//    fuse_daemonize(opts.foreground);
    if(multithreaded)
        fuse_session_loop_mt(account.fuse_session);
    else
        fuse_session_loop(account.fuse_session);

#endif



    fuse_remove_signal_handlers(account.fuse_session);
    fuse_session_destroy(account.fuse_session);
#if FUSE_USE_VERSION >= 30
    fuse_session_unmount(account.fuse_session);
#else
    fuse_unmount(mountpoint, account.fuse_channel);
#endif
err_out3:
    fuse_remove_signal_handlers(account.fuse_session);
    err_out2:
    fuse_session_destroy(account.fuse_session);
    err_out1:
#ifndef USE_FUSE3
    fuse_unmount(mountpoint, account.fuse_channel);
#endif
    fuse_opt_free_args(&args);
    ucout << "Done." << std::endl;

    return 0;
}