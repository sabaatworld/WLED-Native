#ifndef WLED_HOST_CFG_H
#define WLED_HOST_CFG_H

#ifndef ARDUINO

bool backupConfig();
bool restoreConfig();
bool verifyConfig();
bool configBackupExists();
void resetConfig();
bool verifyConfigSec();

#endif

#endif // WLED_HOST_CFG_H
