#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint TRIGGER_PIN = 16;
const uint ECHO_PIN = 17;

QueueHandle_t xQueueTime;
SemaphoreHandle_t xSemaphoreTrigger; // 0 desativado e 1 ativado
QueueHandle_t xQueueDistance;


void pin_callback(uint gpio, uint32_t events) {
    // Captura tempo de inicio e fim do eco
    static uint32_t time_init;
    uint32_t time_end;
    if (events ==0x4) {
        time_end = time_us_32(); // A medição deve ser finalizada no falling edge do echo
        uint32_t dt = time_end-time_init;
        xQueueReset(xQueueTime);
        xQueueSendFromISR(xQueueTime, &dt, NULL);
    } else if (events == 0x8) {
        time_init = time_us_32(); //O alarme deve ser inicializado no rising edge do trigger
    }
}

void trigger_task(void *p) {
    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);
        gpio_put(TRIGGER_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger); // Indica que o trigger foi ativado
    }
}

void echo_task(void *p) {
    while (1) {
        int dt;
        if (xQueueReceive(xQueueTime, &dt, portMAX_DELAY)) {
        float distancia_cm = ((dt / 1000000.0) * 340.0 / 2.0)*100; //Calcula a distancia com base no dt
        xQueueReset(xQueueDistance); // Limpa a fila para garantir que a proxima a aparecer seja a mais recente
        xQueueSend(xQueueDistance, &distancia_cm, 0); // Insere a nova distancia na fila
        }
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    float dist;
    char str[20];
    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, 0) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &dist,  0)) {
                sprintf(str, "Distancia: %.2f cm", dist); //Serializa o a string mostrada com a distancia

                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, str);
                gfx_draw_line(&disp, 0, 27, dist,27);
                vTaskDelay(pdMS_TO_TICKS(50));
                gfx_show(&disp);
            } 
            else {
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, "ERRO: O sensor falhou");
                gfx_show(&disp);
            }
        }
    }
}

int main() {
    stdio_init_all();
    // Inicializa pinos
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    //Cria Semaforo
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    //Cria Filas
    xQueueTime = xQueueCreate(32, sizeof(int));
    xQueueDistance = xQueueCreate(32, sizeof(float));

    //Aciona callback
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &pin_callback);

    //Cria Tasks
    xTaskCreate(oled_task, "Display", 4095, NULL, 1, NULL);
    xTaskCreate(trigger_task, "Trigger", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
