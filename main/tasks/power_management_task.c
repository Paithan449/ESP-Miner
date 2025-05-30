#include <string.h>
#include "INA260.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "math.h"
#include "mining.h"
#include "nvs_config.h"
#include "serial.h"
#include "TPS546.h"
#include "vcore.h"
#include "thermal.h"
#include "power.h"
#include "asic.h"

#define POLL_RATE 2000
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define THROTTLE_TEMP_RANGE (MAX_TEMP - THROTTLE_TEMP)

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define ADJUSTMENT_LOOPS 30

static const char * TAG = "power_management";

// static float _fbound(float value, float lower_bound, float upper_bound)
// {
//     if (value < lower_bound)
//         return lower_bound;
//     if (value > upper_bound)
//         return upper_bound;

//     return value;
// }

// Set the fan speed between 20% min and 100% max based on chip temperature as input.
// The fan speed increases from 20% to 100% proportionally to the temperature increase from 50 and THROTTLE_TEMP
static double automatic_fan_speed(float chip_temp, GlobalState * GLOBAL_STATE)
{
    double result = 0.0;
    double min_temp = 45.0;
    double min_fan_speed = 35.0;

    if (chip_temp < min_temp) {
        result = min_fan_speed;
    } else if (chip_temp >= THROTTLE_TEMP) {
        result = 100;
    } else {
        double temp_range = THROTTLE_TEMP - min_temp;
        double fan_range = 100 - min_fan_speed;
        result = ((chip_temp - min_temp) / temp_range) * fan_range + min_fan_speed;
    }
    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    power_management->fan_perc = result;
    Thermal_set_fan_percent(GLOBAL_STATE->device_model, result/100.0);

	return result;
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    power_management->frequency_multiplier = 1;

    //int last_frequency_increase = 0;
    //uint16_t frequency_target = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);

    vTaskDelay(500 / portTICK_PERIOD_MS);
    uint16_t last_core_voltage = 0.0;
    uint16_t last_asic_frequency = power_management->frequency_value;
    uint16_t adjustmet_counter = 0.0;
    int16_t corevoltage = 0;
    double fanspread = 0;
    double tempspread = 0;
    
    while (1) {
        adjustmet_counter++;
        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        power_management->power = Power_get_power(GLOBAL_STATE);

        power_management->fan_rpm = Thermal_get_fan_speed(GLOBAL_STATE->device_model);
        power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        corevoltage = VCORE_get_voltage_mv(GLOBAL_STATE);


        // ASIC Thermal Diode will give bad readings if the ASIC is turned off
        // if(power_management->voltage < tps546_config.TPS546_INIT_VOUT_MIN){
        //     goto looper;
        // }

        //overheat mode if the voltage regulator or ASIC is too hot
        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || power_management->chip_temp_avg > THROTTLE_TEMP) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC %fC", power_management->vr_temp, power_management->chip_temp_avg );
            power_management->fan_perc = 100;
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, 1);

            // Turn off core voltage
            Power_disable(GLOBAL_STATE);

            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1000);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 50);
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 100);
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 0);
            nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 1);
            exit(EXIT_FAILURE);
        }

        // New voltage and frequency adjustment code
        uint16_t core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE);
        uint16_t asic_frequency = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY);
    
        if(nvs_config_get_u16(NVS_CONFIG_AUTO_SPEED, 0)== 0)
        {
            if (nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1) == 1) {

                power_management->fan_perc = (float)automatic_fan_speed(power_management->chip_temp_avg, GLOBAL_STATE);
    
            } else {
                float fs = (float) nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, 100);
                power_management->fan_perc = fs;
                Thermal_set_fan_percent(GLOBAL_STATE->device_model, (float) fs / 100.0);
            }
        }
        else // Auto speed active
        {
            if(power_management->chip_temp_avg > nvs_config_get_u16(NVS_CONFIG_AST_LOW, 60))
            {
                fanspread = 100 - nvs_config_get_u16(NVS_CONFIG_FAN_TARGET, 80);
                tempspread = nvs_config_get_u16(NVS_CONFIG_AST_HIGH, 60) - nvs_config_get_u16(NVS_CONFIG_AST_LOW, 60);
                power_management->fan_perc = ((power_management->chip_temp_avg - nvs_config_get_u16(NVS_CONFIG_AST_LOW, 60)) / tempspread) * fanspread + nvs_config_get_u16(NVS_CONFIG_FAN_TARGET, 80);
            }
            else
            {
                power_management->fan_perc = (power_management->chip_temp_avg / nvs_config_get_u16(NVS_CONFIG_AST_LOW, 60)) * (nvs_config_get_u16(NVS_CONFIG_FAN_TARGET, 80));
            }

            if(power_management->fan_perc > 100)
            {
                power_management->fan_perc = 100;
            }                    
            Thermal_set_fan_percent(GLOBAL_STATE->device_model, (float) power_management->fan_perc / 100.0);                

            if(adjustmet_counter >= ADJUSTMENT_LOOPS)
            {
                adjustmet_counter=0;

                if(power_management->fan_perc > nvs_config_get_u16(NVS_CONFIG_FAN_TARGET, 80)+1)
                {
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, asic_frequency - 1);
                }
                else
                {
                    if(power_management->power < nvs_config_get_u16(NVS_CONFIG_POWER_LOW, 0) && 
                    corevoltage > nvs_config_get_u16(NVS_CONFIG_ASV_HIGH, 1000) &&
                    power_management->chip_temp_avg < nvs_config_get_u16(NVS_CONFIG_AST_LOW, 60) &&
                    power_management->vr_temp < nvs_config_get_u16(NVS_CONFIG_VRT_LOW, 60) &&
                    power_management->frequency_value < nvs_config_get_u16(NVS_CONFIG_HASH_HIGH, 625) &&
                    power_management->fan_perc < nvs_config_get_u16(NVS_CONFIG_FAN_TARGET, 0)
                    )
                    {
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, asic_frequency + 1);           
                    }    
                }
            }
            if(power_management->power > nvs_config_get_u16(NVS_CONFIG_POWER_HIGH, 0) || 
            corevoltage < nvs_config_get_u16(NVS_CONFIG_ASV_LOW, 1000) ||
            power_management->chip_temp_avg > nvs_config_get_u16(NVS_CONFIG_AST_HIGH, 60) ||
            power_management->vr_temp > nvs_config_get_u16(NVS_CONFIG_VRT_HIGH, 60)
            )
            {
                if(power_management->frequency_value > nvs_config_get_u16(NVS_CONFIG_HASH_LOW, 625))
                {
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, asic_frequency - 1);
                }
                       
            }               
        }





        if (core_voltage != last_core_voltage) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage((double) core_voltage / 1000.0, GLOBAL_STATE);
            last_core_voltage = core_voltage;
        }

        if (asic_frequency != last_asic_frequency) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %uMHz (current: %uMHz)", asic_frequency, last_asic_frequency);
            
            bool success = ASIC_set_frequency(GLOBAL_STATE, (float)asic_frequency);
            
            if (success) {
                power_management->frequency_value = (float)asic_frequency;
            }
            
            last_asic_frequency = asic_frequency;
        }

        // Check for changing of overheat mode
        uint16_t new_overheat_mode = nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
