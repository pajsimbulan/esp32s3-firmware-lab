#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#define MODE 2
#define DT_MS 10
#define SETPOINT 100.0f
#define RUN_SECONDS 40
#define PLANT_TAU 3.0f
#define OUT_MIN 0.0f
#define OUT_MAX 115.0f

static const char *TAG = "lab18";

typedef struct {
    float kp;
    float ki;
    float kd;
    float dt;
    float out_min;
    float out_max;
    bool anti_windup;
    float integ;
    float prev_meas;
    bool started;
} pid_controller_t;

static float pid_step(pid_controller_t *p, float sp, float meas) {
    float err = sp-meas;

    float deriv = p->started ? (meas -p->prev_meas) / p->dt : 0.0f;
    p->prev_meas = meas;
    p->started = true;

    float integ_trial = p->integ + (err * p->dt);
    float out = (p->kp*err) + (p->ki*integ_trial) - (p->kd*deriv);
    if(out > p->out_max) {
        out = p->out_max;
        if(!p->anti_windup || err < 0.0f) p->integ = integ_trial;
    } else if (out < p->out_min) {
        out = p->out_min;
        if(!p->anti_windup ||  err > 0.0f) p->integ = integ_trial;
    } else p->integ = integ_trial;
    return out;
}

static float g_pv = 0.0f;

static float plant_step(float u, float dt) {
    g_pv += (u-g_pv) * (dt / PLANT_TAU);
    return g_pv;
}

static void control_task( void *arg) {
    (void)arg;

    pid_controller_t ctl = {
        .dt = DT_MS / 1000.0f,
        .out_min = OUT_MIN,
        .out_max = OUT_MAX,
        .kd = 0.0f,
        .kp = 1.5f,
        .anti_windup = true,
    };
    if(MODE == 0) {
        ctl.ki = 0.0f;
        ctl.anti_windup = true;
    } else if (MODE == 1) {
        ctl.ki = 1.5f;
        ctl.anti_windup = false;
    } else {
        ctl.ki = 1.5f;
        ctl.anti_windup = true;
    }

    const char *mode_name = (MODE ==0) ? "P-only" : (MODE == 1) ? "PI, no anti-windup" : "PID, anti-windup";

    ESP_LOGW(TAG, "mode=%s kp=%.2f out_max=%.0f", mode_name, ctl.kp, ctl.ki, (double)OUT_MAX);
    printf("t,setpoint,pv,u\"n");
    float peak = 0.0f;
    int n = (RUN_SECONDS *1000) / DT_MS;
    TickType_t next = xTaskGetTickCount();

    for(int i=0; i<n; i++) {
        float u = pid_step(&ctl, SETPOINT, g_pv);
        float pv = plant_step(u, ctl.dt);
        if(pv > peak) peak = pv;
        
        printf("%.2f, %.2f,%.2f,%.2f\n", i*ctl.dt, (double)SETPOINT, (double)pv, (double)u);
        vTaskDelayUntil(&next, pdMS_TO_TICKS(DT_MS));
    }

    ESP_LOGW(TAG, "=========================================");
    ESP_LOGW(TAG, " mode       : %s", mode_name);
    ESP_LOGW(TAG, " final pv   : %.2f", (double)g_pv);
    ESP_LOGW(TAG, " offset     : %.2f", (double)(SETPOINT - g_pv));
    ESP_LOGW(TAG, " peak       : %.2f", (double)peak);
    ESP_LOGW(TAG, " overshoot  : %.2f",
             (double)(peak > SETPOINT ? peak - SETPOINT : 0.0f));
    ESP_LOGW(TAG, "=========================================");
    vTaskDelete(NULL);
    
}



void app_main(void)
{
    xTaskCreate(control_task, "control_task", 4096, NULL,5,NULL);
}