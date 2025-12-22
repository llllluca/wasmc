#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wasm_export.h"
#include "bh_platform.h"
#include "esp_task_wdt.h"

#include "compile.h"
#include "wasm.h"
#include "aot.h"

//#include "heapsort.h"
//#include "matrix.h"
#include "sieve.h"

uint8_t *wasm = NULL;
uint32_t wasm_len = 0;

uint8_t *aot = NULL;
uint32_t aot_len = 0;

#include "esp_log.h"

#define IWASM_MAIN_STACK_SIZE 5120

#define LOG_TAG "wamr"

#define ITERATION 10
uint64_t times[ITERATION];
static void app_instance_main(wasm_module_inst_t module_inst) {
    const char *exception;
    uint64_t total = 0;

    int32_t argc = 2;
    char *argv[] = { "0", "0" };
    char *func = "main";
    for (unsigned int i = 0; i < ITERATION; i++) {
        uint64_t start = esp_timer_get_time();
        wasm_application_execute_func(module_inst, func, argc, argv); 
        uint64_t end = esp_timer_get_time();
        if ((exception = wasm_runtime_get_exception(module_inst))) {
            printf("%s\n", exception);
        }
        uint64_t time = end - start;
        total += time;
        times[i] = time;
        printf("%u: %f milliseconds\n", i, (double) time / 1000);
        // wait 100 millisecond
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    printf("total: %f milliseconds\n", (double) total / 1000);
    double mean = total / ITERATION;
    printf("mean: %f milliseconds\n", mean / 1000);

    double sum = 0;
    for (unsigned int i = 0; i < ITERATION; i++) {
        double diff = times[i] - mean;
        sum += diff * diff;
    }
    double std = sqrt(sum / ITERATION);
    printf("std: %f milliseconds\n", std / 1000);
}

void *iwasm_main(void *arg) {
    (void)arg; /* unused */
    /* setup variables for instantiating and running the wasm module */
    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;
    char error_buf[128];

    /* configure memory allocation */
    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func = (void *)os_malloc;
    init_args.mem_alloc_option.allocator.realloc_func = (void *)os_realloc;
    init_args.mem_alloc_option.allocator.free_func = (void *)os_free;

    ESP_LOGI(LOG_TAG, "Initialize WASM runtime");
    /* initialize runtime environment */
    if (!wasm_runtime_full_init(&init_args)) {
        ESP_LOGE(LOG_TAG, "Init runtime failed.");
        return NULL;
    }

#if WASM_ENABLE_INTERP != 0
    ESP_LOGI(LOG_TAG, "Run wamr with interpreter");

    /* load WASM module */
    wasm_module = wasm_runtime_load(
        wasm,
        wasm_len,
        error_buf,
        sizeof(error_buf));

    if (!wasm_module) {
        ESP_LOGE(LOG_TAG, "Error in wasm_runtime_load: %s", error_buf);
        goto fail1interp;
    }

    ESP_LOGI(LOG_TAG, "Instantiate WASM runtime");
    wasm_module_inst = wasm_runtime_instantiate(
        wasm_module,
        32 * 1024, // stack size
        32 * 1024, // heap size
        error_buf,
        sizeof(error_buf));

    if (!wasm_module_inst) {
        ESP_LOGE(LOG_TAG, "Error while instantiating: %s", error_buf);
        goto fail2interp;
    }

    ESP_LOGI(LOG_TAG, "run main() of the application");
    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    ESP_LOGI(LOG_TAG, "Deinstantiate WASM runtime");
    wasm_runtime_deinstantiate(wasm_module_inst);

fail2interp:
    /* unload the module */
    ESP_LOGI(LOG_TAG, "Unload WASM module");
    wasm_runtime_unload(wasm_module);

fail1interp:
#endif

#if WASM_ENABLE_AOT != 0
    ESP_LOGI(LOG_TAG, "Run wamr with AoT");

    /* load WASM module */
    wasm_module = wasm_runtime_load(
            aot,
            aot_len,
            error_buf, 
        sizeof(error_buf));

    if (!wasm_module) {
        ESP_LOGE(LOG_TAG, "Error in wasm_runtime_load: %s", error_buf);
        goto fail1aot;
    }

    ESP_LOGI(LOG_TAG, "Instantiate WASM runtime");

    uint32_t stack_size = 1024;
    uint32_t heap_size  = 0;
    wasm_module_inst = wasm_runtime_instantiate(
        wasm_module,
        stack_size,
        heap_size,
        error_buf,
        sizeof(error_buf));

    if (!wasm_module_inst) {
        ESP_LOGE(LOG_TAG, "Error while instantiating: %s", error_buf);
        goto fail2aot;
    }

    ESP_LOGI(LOG_TAG, "run main() of the application");
    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    ESP_LOGI(LOG_TAG, "Deinstantiate WASM runtime");
    wasm_runtime_deinstantiate(wasm_module_inst);

fail2aot:
    /* unload the module */
    ESP_LOGI(LOG_TAG, "Unload WASM module");
    wasm_runtime_unload(wasm_module);
fail1aot:
#endif

    /* destroy runtime environment */
    ESP_LOGI(LOG_TAG, "Destroy WASM runtime");
    wasm_runtime_destroy();
    return NULL;
}

extern Target rv32;

void app_main(void) {

    ESP_ERROR_CHECK(esp_task_wdt_deinit());

    //wasm = heapsort_wasm;
    //wasm_len = heapsort_wasm_len;

    //wasm = matrix_wasm;
    //wasm_len = matrix_wasm_len;

    wasm = sieve_wasm;
    wasm_len = sieve_wasm_len;

    WASMModule m;
    load_wasm_module(&m, wasm, wasm_len);
    compile(&m, &rv32, &aot, &aot_len);
    free_wasm_module(&m);

    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&tattr, IWASM_MAIN_STACK_SIZE);

    pthread_t t;
    int res;
    res = pthread_create(&t, &tattr, iwasm_main, (void *)NULL);
    assert(res == 0);

    res = pthread_join(t, NULL);
    assert(res == 0);

    free(aot);

    ESP_LOGI(LOG_TAG, "Exiting...");
}
