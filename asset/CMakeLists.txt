cmake_minimum_required(VERSION 3.13)

########################################################################################################################

project(fty-asset
    VERSION 1.0.0
    DESCRIPTION "Fty asset library for tntnet"
)

########################################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
########################################################################################################################


etn_target(static ${PROJECT_NAME}
    PUBLIC
        asset/json.h
        asset/asset-manager.h
        asset/asset-helpers.h
        asset/asset-computed.h
        asset/asset-db.h
        asset/asset-licensing.h
        asset/asset-import.h
        asset/asset-configure-inform.h
        asset/csv.h
        asset/error.h
        asset/logger.h
        asset/db.h

    SOURCES
        src/json.cpp
        src/asset-cam.cpp
        src/asset-manager.cpp
        src/asset-computed.cpp
        src/asset-helpers.cpp
        src/asset-db.cpp
        src/asset-licensing.cpp
        src/asset-import.cpp
        src/asset-configure-inform.cpp
        src/csv.cpp

        src/manager/read.cpp
        src/manager/delete.cpp
        src/manager/create.cpp
        src/manager/import.cpp
        src/manager/export.cpp
    INCLUDE_DIRS src
    USES
        fty_common_logging
        mlm
        fty_asset
        fty_asset_activator
        fty_common
        fty-utils
        fty_shm
        fty_common_rest
        fty_common_mlm
        czmq
        fty_proto
        fty_security_wallet
    PRIVATE
)
