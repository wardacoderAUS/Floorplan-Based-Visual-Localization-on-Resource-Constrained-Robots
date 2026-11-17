idf_component_register(SRCS "../../pmu.c" "../../demo.c"
                       INCLUDE_DIRS "../.."
                       REQUIRES esp_timer esp_hw_support heap freertos)
