add_executable(nrfusion_probe tools/game_probe_main.cpp)
target_link_libraries(nrfusion_probe PRIVATE nrfusion_core)
set_target_properties(nrfusion_probe PROPERTIES OUTPUT_NAME "NRFusionProbe")

add_executable(nrfusion_sim tests/simulation.cpp)
target_link_libraries(nrfusion_sim PRIVATE nrfusion_core)
