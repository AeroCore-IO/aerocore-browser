#pragma once

#include <QString>

namespace AppLogger {

/// Install Qt message handler and open app.log. Call once in the browser main process.
void init();

/// Flush and close the log file; restore default Qt handler.
void shutdown();

/// Default log file path (respects AEROCORE_LOG_FILE / AEROCORE_LOG_DIR).
QString logFilePath();

/// True when debug messages are written (AEROCORE_LOG_LEVEL=debug|trace or AEROCORE_DEBUG).
bool debugEnabled();

} // namespace AppLogger
