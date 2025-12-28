
macro(add_sources)
    file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

    foreach(SOURCE_FILE ${ARGV})
        # Lägg till filen i en global lista
        set_property(GLOBAL APPEND PROPERTY GLOBAL_SOURCES "${REL_PATH}/${SOURCE_FILE}")
    endforeach()
endmacro()

macro(include_this)
    file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(GLOBAL APPEND PROPERTY GLOBAL_INCLUDES "${REL_PATH}")
endmacro()