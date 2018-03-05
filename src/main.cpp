#include <iostream>
#include "gdrive/Account.h"
#include "gdrive/Filesystem.h"
#include <easylogging++.h>
INITIALIZE_EASYLOGGINGPP;

int main(int argc, char **argv) {

    const char *v1 = "DriveFS\0";
    const char *v2 =  "-v\0";
    char **v = ( char **) malloc(sizeof(char * ) * 2);
    v[0] = (char *)v1;
    v[1] = (char *)v2;

    START_EASYLOGGINGPP(2, v);
    el::Configurations defaultConf;
    defaultConf.setToDefault();

    defaultConf.setGlobally(
//            el::ConfigurationType::Format, "%datetime [%levshort] %thread [%fbase:%line] %msg");
            el::ConfigurationType::Format, "%datetime [%levshort] [%fbase:%line] %msg");
    defaultConf.parseFromText("*GLOBAL:\nTO_FILE = true\nFilename = \"/tmp/DriveFS.log\"\nMax_Log_File_Size = 104857600"); // 100MB rorate log sizes
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Loggers::reconfigureLogger("default", defaultConf);
    el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);

    DriveFS::Account account = DriveFS::Account::getAccount();
    if(account.needToInitialize()) {
        account.run();
    }

    struct fuse_session *session;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    const char *temp = "GDriveFS";
    const char* fsName = strdup(temp);
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