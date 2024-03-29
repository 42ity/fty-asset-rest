cmake_minimum_required(VERSION 3.13)

########################################################################################################################

project(fty-asset-rest
    VERSION 1.0.0
    DESCRIPTION "Fty asset library for tntnet"
)

########################################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
########################################################################################################################

etn_target(shared ${PROJECT_NAME}
    SOURCES
        conf/43_assets.xml.in

        src/read.cpp
        src/read.h
        src/create.cpp
        src/create.h
        src/list.cpp
        src/list.h
        src/list-in.cpp
        src/list-in.h
        src/delete.cpp
        src/delete.h
        src/import.cpp
        src/import.h
        src/export.cpp
        src/export.h
        src/edit.cpp
        src/edit.h
        src/actions-get.cpp
        src/actions-get.h
        src/actions-post.cpp
        src/actions-post.h
        src/check-usize.cpp
        src/check-usize.h
    USES
        fty-cmake-rest
        cxxtools
        tntnet
        tntdb
        fty_common_rest
        fty_shm
        fty-pack
        fty-asset
        fty-asset-libng
        fty_proto
        fty_common_messagebus
        fty_common
        fty_common_dto
        fty-pack
    TARGET_DESTINATION /usr/lib/bios
)

########################################################################################################################

# mappings for tntnet
etn_configure_file(
    conf/53_assets.xml.in

    TARGET      ${PROJECT_NAME}
    DESTINATION /etc/tntnet/bios.d
)

########################################################################################################################
