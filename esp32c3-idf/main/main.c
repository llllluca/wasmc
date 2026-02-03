#include <stdio.h>
#include "wasm_export.h"
#include "bh_platform.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "wasmc.h"
#include "fib2-Os.h"

#define WASM fib2_Os_wasm
#define WASM_LEN fib2_Os_wasm_len

#define AOT_MAX_LEN (16 * 1024)
uint8_t aot[AOT_MAX_LEN];
uint32_t aot_len;

#define IWASM_MAIN_STACK_SIZE 5120
char wamr_error_buf[128];

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
            ESP_LOGE(LOG_TAG, "%s", exception);
        }
        uint64_t time = end - start;
        total += time;
        times[i] = time;
        ESP_LOGI(LOG_TAG, "%u: %f milliseconds", i, (double) time / 1000);
        // wait 100 millisecond
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(LOG_TAG, "total: %f milliseconds", (double) total / 1000);
    double mean = total / ITERATION;
    ESP_LOGI(LOG_TAG, "mean: %f milliseconds", mean / 1000);

    double sum = 0;
    for (unsigned int i = 0; i < ITERATION; i++) {
        double diff = times[i] - mean;
        sum += diff * diff;
    }
    double std = sqrt(sum / ITERATION);
    ESP_LOGI(LOG_TAG, "std: %f milliseconds", std / 1000);
}

void wamr_wasm_interpreter(void) {

    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;

    ESP_LOGI(LOG_TAG, "Run wamr with interpreter");

    /* load WASM module */
    wasm_module = wasm_runtime_load(
        WASM,
        WASM_LEN,
        wamr_error_buf,
        sizeof(wamr_error_buf));

    if (!wasm_module) {
        ESP_LOGE(LOG_TAG, "Error in wasm_runtime_load: %s", wamr_error_buf);
        return;
    }

    uint32_t stack_size = 1024;
    uint32_t heap_size  = 0;
    ESP_LOGI(LOG_TAG, "Instantiate WASM runtime");
    wasm_module_inst = wasm_runtime_instantiate(
        wasm_module,
        stack_size,
        heap_size,
        wamr_error_buf,
        sizeof(wamr_error_buf));

    if (!wasm_module_inst) {
        ESP_LOGE(LOG_TAG, "Error while instantiating: %s", wamr_error_buf);
        goto UNLOAD;
    }

    ESP_LOGI(LOG_TAG, "run main() of the application");
    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    ESP_LOGI(LOG_TAG, "Deinstantiate WASM runtime");
    wasm_runtime_deinstantiate(wasm_module_inst);

UNLOAD:
    /* unload the module */
    ESP_LOGI(LOG_TAG, "Unload WASM module");
    wasm_runtime_unload(wasm_module);
}

void wamr_aot_runtime(void) {

    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;

    ESP_LOGI(LOG_TAG, "Run wamr with AoT");

    /* load WASM module */
    wasm_module = wasm_runtime_load(
            aot,
            aot_len,
            wamr_error_buf, 
        sizeof(wamr_error_buf));

    if (!wasm_module) {
        ESP_LOGE(LOG_TAG, "Error in wasm_runtime_load: %s", wamr_error_buf);
        return;
    }

    ESP_LOGI(LOG_TAG, "Instantiate WASM runtime");

    uint32_t stack_size = 1024;
    uint32_t heap_size  = 0;
    wasm_module_inst = wasm_runtime_instantiate(
        wasm_module,
        stack_size,
        heap_size,
        wamr_error_buf,
        sizeof(wamr_error_buf));

    if (!wasm_module_inst) {
        ESP_LOGE(LOG_TAG, "Error while instantiating: %s", wamr_error_buf);
        goto UNLOAD;
    }

    ESP_LOGI(LOG_TAG, "run main() of the application");
    app_instance_main(wasm_module_inst);

    /* destroy the module instance */
    ESP_LOGI(LOG_TAG, "Deinstantiate WASM runtime");
    wasm_runtime_deinstantiate(wasm_module_inst);

UNLOAD:
    /* unload the module */
    ESP_LOGI(LOG_TAG, "Unload WASM module");
    wasm_runtime_unload(wasm_module);
}



void *wamr_main(void *arg) {
    (void)arg; /* unused */
    /* setup variables for instantiating and running the wasm module */

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
    wamr_wasm_interpreter();
#endif

#if WASM_ENABLE_AOT != 0
    wamr_aot_runtime();
#endif

    /* destroy runtime environment */
    ESP_LOGI(LOG_TAG, "Destroy WASM runtime");
    wasm_runtime_destroy();
    return NULL;
}

void app_main(void) {

    ESP_ERROR_CHECK(esp_task_wdt_deinit());

    aot_len = wasmc_compile(WASM, WASM_LEN, aot, AOT_MAX_LEN);
    switch (aot_len) {
    case WASMC_ERR_MALFORMED_WASM_MODULE:
        ESP_LOGE(LOG_TAG, "Error: compilation failure, malformed WebAssembly module");
        return;
    case WASMC_ERR_OUT_OF_HEAP_MEMORY:
        ESP_LOGE(LOG_TAG, "Error: compilation failure, out of heap memory");
        return;
    case WASMC_ERR_AOT_BUFFER_TOO_SMALL:
        ESP_LOGE(LOG_TAG, "Error: compilation failure, the aot buffer is too small");
        return;
    }

    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&tattr, IWASM_MAIN_STACK_SIZE);

    pthread_t t;
    int res;
    res = pthread_create(&t, &tattr, wamr_main, (void *)NULL);
    assert(res == 0);

    res = pthread_join(t, NULL);
    assert(res == 0);

    ESP_LOGI(LOG_TAG, "Exiting...");
}
