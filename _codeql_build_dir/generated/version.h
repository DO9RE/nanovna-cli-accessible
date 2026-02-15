#ifndef VERSION_H
#define VERSION_H

#define APP_NAME "NanoVNA CLI Accessible"
#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 6
#define APP_VERSION_PATCH 0
#define APP_VERSION_SUFFIX "-beta"
#define APP_VERSION_STRING "0.6.0-beta"
#define APP_GIT_COMMIT "416b1ff"
#define APP_BUILD_TIMESTAMP "2026-02-15 03:31:41 UTC"
#define APP_REPO_TYPE "dev"
#define APP_BUILD_TYPE "Release"

// Smart version string depending on repo
#ifdef _DEBUG
    #define APP_VERSION_FULL APP_VERSION_STRING " (" APP_GIT_COMMIT " " APP_REPO_TYPE ")"
#else
    #define APP_VERSION_FULL APP_VERSION_STRING
#endif

#endif // VERSION_H
