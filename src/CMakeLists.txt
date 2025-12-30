
add_subdirectory(gpu)



add_sources(
    resmanager.c
    rendergraph.c
    filewatch.c
    res_async.c
    vector.c
    util.c
    submit_manager.c
)

include_this()
