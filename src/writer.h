#pragma once

#include <QString>

int runWriteJob(const QString &jobPath, bool allowFile);
bool writerSelfTest(QString *error);
