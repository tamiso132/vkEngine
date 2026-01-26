
add_subdirectory(gpu)
add_subdirectory(world)
add_subdirectory(resource)
add_subdirectory(ui)



add_sources(
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

    vox_loader.c
    transfer_queue.c
    hashmaputil.c

    readback.c
    input.c
)

include_this()
