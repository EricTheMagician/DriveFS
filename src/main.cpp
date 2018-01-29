#include <iostream>
#include "gdrive/Account.h"
#include <easylogging++.h>
INITIALIZE_EASYLOGGINGPP;

int main() {

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
            el::ConfigurationType::Format, "%datetime [%levshort] [%fbase:%line %msg");
    defaultConf.parseFromText("*GLOBAL:\nTO_FILE = true\nFilename = \"/tmp/DriveFS.log\"\nMax_Log_File_Size = 104857600"); // 100MB rorate log sizes
    el::Loggers::reconfigureAllLoggers(defaultConf);
    el::Loggers::reconfigureLogger("default", defaultConf);
    el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);

    DriveFS::Account account = DriveFS::Account::getAccount();
    if(account.needToInitialize()) {
        account.run();
    }
    ucout << "Done." << std::endl;

    return 0;
}