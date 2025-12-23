macro(add_sources)
    # Initialize the list if it's not already defined
    if(NOT DEFINED SOURCES_FILES_GAME_ENGINE)
        set(SOURCES_FILES_GAME_ENGINE "")
    endif()

    # Get the relative path from the project root to the current source directory
    file(RELATIVE_PATH RELATIVE_PATH_TO_PROJECT ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

    # Iterate over the source files and add them to the list
    foreach(SOURCE_FILE ${ARGV})
        # FIX: Använd rätt variabelnamn här (RELATIVE_PATH_TO_PROJECT)
        list(APPEND SOURCES_FILES_GAME_ENGINE "${RELATIVE_PATH_TO_PROJECT}/${SOURCE_FILE}")
    endforeach()

    # Set the updated list back to the parent scope
    set(SOURCES_FILES_GAME_ENGINE ${SOURCES_FILES_GAME_ENGINE} PARENT_SCOPE)
endmacro()

macro(include_this)
    file(RELATIVE_PATH RELATIVE_PATH_TO_PROJECT ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    
    # FIX: Tog bort "\n" i början av strängen. CMake hanterar listor automatiskt.
    # Jag ändrade också till RELATIVE_PATH_TO_PROJECT för att matcha variabeln ovan.
    list(APPEND INCLUDE_DIRECTORIES_ENGINE "${RELATIVE_PATH_TO_PROJECT}")
    
    set(INCLUDE_DIRECTORIES_ENGINE ${INCLUDE_DIRECTORIES_ENGINE} PARENT_SCOPE)
endmacro()

add_subdirectory(gpu)



add_sources(
    resmanager.c
    rendergraph.c
    filewatch.c
)

include_this()