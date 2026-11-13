#ifndef TERMIC_VERSION_H
#define TERMIC_VERSION_H
/* Native Amiga version; independent of the desktop React/Tauri release. */
#define TERMIC_VERSION "0.1.0 Beta"
#define TERMIC_VERSION_NUMBER "0.1"
#define TERMIC_VERSION_DATE "13.11.2026"
#define TERMIC_VERSION_TAG(name)                                                                   \
    "\0$VER: " name " " TERMIC_VERSION_NUMBER " (" TERMIC_VERSION_DATE ") " TERMIC_VERSION
#endif
