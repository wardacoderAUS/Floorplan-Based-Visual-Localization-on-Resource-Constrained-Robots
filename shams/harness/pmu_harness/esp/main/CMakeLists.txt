# Profile any code: workload.c (replace it with your own) runs inside pmu_profile().
# To use the stage-marker test instead, replace "../../workload.c" "../../mcu_main.c" with "../../demo.c".
idf_component_register(SRCS "../../pmu.c" "../../workload.c" "../../mcu_main.c"
                       INCLUDE_DIRS "../.."
                       REQUIRES esp_timer esp_hw_support heap freertos)
