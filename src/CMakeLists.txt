
add_subdirectory(gpu)



add_sources(
    resmanager.c
    submit_manager.c
    filewatch.c
    rendergraph.c
    
    command.c
    sample_runner.c
    res_async.c
    vector.c
    util.c

    triangle_sample.c
    raytrace_sample.c

    system_manager.c
)

include_this()
