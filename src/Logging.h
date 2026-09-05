/**
 * @file Logging.h
 * @brief Named logging categories for the application.
 *
 * Every message carries the name of the area it came from, the way a logger
 * name works in NLog or log4net. That makes a log readable one subsystem at a
 * time, and it makes the level adjustable per area at runtime through
 * QLoggingCategory's filter rules or the QT_LOGGING_RULES environment variable:
 *
 *     QT_LOGGING_RULES="qmlcommander.*.debug=false;qmlcommander.ops.debug=true"
 *
 * Debug messages are on by default in a debug build and off in a release build;
 * see installLogging() in main.cpp.
 */
#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcApp)     ///< qmlcommander.app    - startup, session, window
Q_DECLARE_LOGGING_CATEGORY(lcNav)     ///< qmlcommander.nav    - navigation between folders
Q_DECLARE_LOGGING_CATEGORY(lcModel)   ///< qmlcommander.model  - reading, filtering, sorting
Q_DECLARE_LOGGING_CATEGORY(lcCache)   ///< qmlcommander.cache  - the recently visited folders
Q_DECLARE_LOGGING_CATEGORY(lcOps)     ///< qmlcommander.ops    - copy, move, delete, rename
