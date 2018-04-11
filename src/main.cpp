#include <iostream>
#include "BaseAccount.h"
#include "gdrive/Account.h"
#include "gdrive/Filesystem.h"
#include <easylogging++.h>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

INITIALIZE_EASYLOGGINGPP;

int main(int argc, const char **argv) {

    const char *v1 = "DriveFS\0";
    const char *v2 =  "-v\0";
    char **v = ( char **) malloc(sizeof(char * ) * 2);
    v[0] = (char *)v1;
    v[1] = (char *)v2;

    START_EASYLOGGINGPP(2, v);
    el::Configurations defaultConf;
    defaultConf.setToDefault();

    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "this help message")
            ("mount", po::value<std::string>(), "set the mount point")
            ("database", po::value<std::string>()->default_value("mongodb://localhost/"), "set the database path")
            ("fuse-foreground,f", "run the fuse application in foreground instead of a daemon")
            ("fuse-debug,d", "run the fuse application in debug mode")
            ("fuse-allow-other", "set the allow-other-option for fuse")
            ("fuse-singlethread,s", "use a single thread for the fuse event loop")
            ("log-location", po::value<std::string>()->default_value("/tmp/DriveFS.log"))
            ;

    po::positional_options_description p;
    p.add("mount", 1).add("other",-1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
            options(desc).positional(p).run(), vm);
    po::notify(vm);

    defaultConf.setGlobally(
            el::ConfigurationType::Format, "%datetime [%levshort] [%fbase:%line] %msg"
    );
    defaultConf.parseFromText("*GLOBAL:\nTO_FILE = true\nFilename = \"/tmp/DriveFS.log\"\nMax_Log_File_Size = 104857600"); // 100MB rorate log sizes
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Loggers::reconfigureLogger("default", defaultConf);
    el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);
    vm["database"];
    DriveFS::Account account = DriveFS::Account::getAccount(
            vm["database"].as<std::string>() 
    );
    if(account.needToInitialize()) {
        account.run();
    }

    struct fuse_session *session;
    std::vector<const char *>fuse_args;

    auto filesystemName = "DriveFS";
    fuse_args.push_back(&filesystemName[0]);
    auto mountpoint = vm["mount"].as<std::string>();
    fuse_args.push_back(mountpoint.c_str());
    if(vm.count("fuse-foreground")){
        fuse_args.push_back("-f");
    }
    if(vm.count("fuse-debug")){
        fuse_args.push_back("-d");
    }
    if(vm.count("fuse-allow-other")){
        fuse_args.push_back("--allow-other");
    }

    struct fuse_args args = FUSE_ARGS_INIT(fuse_args.size(), (char**)fuse_args.data());
    struct fuse_cmdline_opts opts;
    if( fuse_parse_cmdline(&args, &opts) != 0){
        return -1;
    }

    struct fuse_lowlevel_ops ops = DriveFS::getOps();
    session = fuse_session_new(&args,&ops, sizeof(ops), &account);
    account.fuse_session = session;
    if (session == NULL)
        goto err_out1;

    if (fuse_set_signal_handlers(session) != 0)
        goto err_out2;

    fuse_session_mount(session, opts.mountpoint);

//    if (fuse_session_mount(se, opts.mountpoint) != 0)
//        goto err_out3;

    fuse_daemonize(opts.foreground);

    /* Block until ctrl+c or fusermount -u */
    LOG(INFO) << "Starting the filesystem";
    if(opts.singlethread){
        fuse_session_loop(session);
    }else {
//        ret = fuse_session_loop(se);
        fuse_session_loop_mt(session,1);
    }
    fuse_remove_signal_handlers(session);
    fuse_session_destroy(session);
    fuse_session_unmount(session);

    err_out3:
    fuse_remove_signal_handlers(session);
    err_out2:
    fuse_session_destroy(session);
    err_out1:
    fuse_opt_free_args(&args);
    ucout << "Done." << std::endl;

    return 0;
}