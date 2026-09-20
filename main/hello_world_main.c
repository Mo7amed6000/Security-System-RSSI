/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <esp_http_server.h>

#include "lwip/err.h"
#include "lwip/sys.h"

#include "driver/gpio.h"
// #include "driver/pwm.h"
#include "driver/mcpwm_prelude.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/queue.h"

#include "driver/touch_pad.h"
#include "soc/rtc_periph.h"
#include "soc/sens_periph.h"

static const char *TAG = "wifi softAP";

// serco config
mcpwm_cmpr_handle_t comparator = NULL;

// Please consult the datasheet of your servo before changing the following parameters
// #define SERVO_MIN_PULSEWIDTH_US 1000 // Minimum pulse width in microsecond
// #define SERVO_MAX_PULSEWIDTH_US 2500 // Maximum pulse width in microsecond
#define SERVO_MIN_PULSEWIDTH_US 750  // Minimum pulse width in microsecond SG90
#define SERVO_MAX_PULSEWIDTH_US 2250 // Maximum pulse width in microsecond SG90
// #define SERVO_MIN_DEGREE 0   // Minimum angle
// #define SERVO_MAX_DEGREE 180 // Maximum angle

#define SERVO_MIN_DEGREE 180 // Minimum angle SG90
#define SERVO_MAX_DEGREE 0   // Maximum angle SG90

#define SERVO_PULSE_GPIO GPIO_NUM_19         // GPIO connects to the PWM signal line
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000 // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD 10000          // 20000 ticks, 20ms

#define Led_Pin GPIO_NUM_21

static inline uint32_t example_angle_to_compare(int angle)
{
    return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void Sort(int8_t arr[], int n)
{
    int i, key, j;
    for (i = 1; i < n; i++)
    {
        key = arr[i];
        j = i - 1;
        while (j >= 0 && arr[j] > key)
        {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
    ESP_LOGI(TAG, "after sorting");
    for (i = 0; i < n; i++)
        ESP_LOGI(TAG, "%d ", arr[i]);
}

void Reverse(int8_t arr[], int n)
{
    for (int i = 0; i < n / 2; i++)
    {
        int8_t f = arr[i];
        arr[i] = arr[n - i - 1];
        arr[n - i - 1] = f;
    }

    ESP_LOGI(TAG, "after reversing");
    for (int i = 0; i < n; i++)
        ESP_LOGI(TAG, "%d ", arr[i]);
}

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID "Test3"
#define EXAMPLE_ESP_WIFI_PASS "Password"
#define EXAMPLE_ESP_WIFI_CHANNEL 0
#define EXAMPLE_MAX_STA_CONN 5

nvs_handle_t memory_handle;

TaskHandle_t UsersRSSI;
wifi_sta_list_t members;
int8_t aid;
int8_t aid1[5];

bool door = false;

uint32_t current_time;

// esp iner flag

// TaskHandle_t butten21isr;
TaskHandle_t butten16isr;

//

static bool s_pad_activated;
static uint32_t s_pad_init_val;

void restartAuths()
{
    ESP_LOGI(TAG, "Remove all connections");
    esp_wifi_deauth_sta(0);
}

// void IRAM_ATTR butten21handler(void *args)
// {
//     xTaskResumeFromISR(butten21isr);
// }

void IRAM_ATTR butten16handler(void *args)
{
    xTaskResumeFromISR(butten16isr);
}

// void butten21Task()
// {
//     while (true)
//     {
//         static uint32_t button_press_time = 0;
//         static uint32_t button_release_time = 0;

//         current_time = xTaskGetTickCountFromISR();
//         if (gpio_get_level(GPIO_NUM_25) == 0)
//         { // Button pressed
//             button_press_time = current_time;
//         }
//         else if (gpio_get_level(GPIO_NUM_25) == 1)
//         { // Button released
//             button_release_time = current_time;
//             ESP_LOGI(TAG, "Butten 21 is relesed with : %d time and %d and delay is %d", (int)button_release_time, (int)button_press_time, (int)pdMS_TO_TICKS(3000));
//             if (button_release_time - button_press_time >= pdMS_TO_TICKS(3000))
//             {
//                 ESP_LOGI(TAG, "Butten 21 is Pressed");
//                 restartAuths();
//                 button_press_time = button_release_time;
//                 vTaskDelay(pdMS_TO_TICKS(5000));
//                 vTaskSuspend(NULL);
//             }
//         }
//         vTaskDelay(pdMS_TO_TICKS(500));
//     }
// }

void initServo()
{
    ESP_LOGI(TAG, "Create timer and operator");
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t operator_config = {
        .group_id = 0, // operator must be in the same group to the timer
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

    ESP_LOGI(TAG, "Connect timer and operator");
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    ESP_LOGI(TAG, "Create comparator and generator from the operator");
    comparator = NULL;
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));

    mcpwm_gen_handle_t generator = NULL;
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = SERVO_PULSE_GPIO,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

    // set the initial compare value, so that the servo will spin to the center position
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(0)));

    ESP_LOGI(TAG, "Set generator action on timer and compare event");
    // go high on counter empty
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
                                                              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    // go low on compare threshold
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));

    ESP_LOGI(TAG, "Enable and start timer");
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}

void turnServo(bool s)
{
    int angle;

    if (s)
        angle = 180;
    else
        angle = 0;

    // ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    ESP_LOGI(TAG, "Angle of rotation: %d", angle);
    ESP_LOGI(TAG, "compare of angle: %d", (int)example_angle_to_compare(angle));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, example_angle_to_compare(angle)));
    // Add delay, since it takes time for servo to rotate, usually 200ms/60degree rotation under 5V power supply
    vTaskDelay(pdMS_TO_TICKS(500));
}

void butten16Task()
{
    while (true)
    {
        static uint32_t button_press_time = 0;
        static uint32_t button_release_time = 0;

        current_time = xTaskGetTickCountFromISR();
        if (gpio_get_level(GPIO_NUM_16) == 0)
        { // Button pressed
            button_press_time = current_time;
        }
        else if (gpio_get_level(GPIO_NUM_16) == 1)
        { // Button released
            button_release_time = current_time;
            ESP_LOGI(TAG, "Butten 16 is relesed with : %d time and %d and delay is %d", (int)button_release_time, (int)button_press_time, (int)pdMS_TO_TICKS(10));
            if (button_release_time - button_press_time >= pdMS_TO_TICKS(10))
            {

                ESP_LOGI(TAG, "Butten 16 is Pressed");
                ESP_ERROR_CHECK(nvs_flash_erase());
                ESP_LOGI(TAG, "memory erased");
                esp_restart();
                ESP_LOGI(TAG, "Restarting.....");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void configInterrupter()
{
    //     gpio_set_direction(GPIO_NUM_25, GPIO_MODE_INPUT);
    //     gpio_set_intr_type(GPIO_NUM_25, GPIO_INTR_POSEDGE);
    //     gpio_isr_handler_add(GPIO_NUM_25, butten21handler, NULL);
    //     xTaskCreate(butten21Task, "buttenTask", 4096, NULL, 10, &butten21isr);

    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT);
    gpio_set_intr_type(GPIO_NUM_16, GPIO_INTR_POSEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_16, butten16handler, NULL);
    xTaskCreate(butten16Task, "buttenTask", 4096, NULL, 10, &butten16isr);
}

void getUsersRSSI()
{
    while (1)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        esp_wifi_ap_get_sta_list(&members);
        if (members.num > 0)
        {
            ESP_LOGI(TAG, "----------------------------------------");
            for (size_t i = 0; i < members.num; i++)
            {
                ESP_LOGI(TAG, "device mac : " MACSTR " and its rssi : %d", MAC2STR(members.sta[i].mac), (int)aid1[i]);
                aid1[i] = members.sta[i].rssi;
            }
            Sort(aid1, members.num);
            Reverse(aid1, members.num);
            ESP_LOGI(TAG, "Lowest rssi : %d", (int)aid1[0]);
            if (aid1[0] > -65)
            {
                gpio_set_level(Led_Pin, 1);
                door = true;
            }
            else
            {
                door = false;
                turnServo(0);
                gpio_set_level(Led_Pin, 0);
            }

            ESP_LOGI(TAG, "----------------------------------------");
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        else
        {
            door = false;
            turnServo(0);
            gpio_set_level(Led_Pin, 0);
            vTaskSuspend(UsersRSSI);
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{

    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        esp_wifi_ap_get_sta_list(&members);
        ESP_LOGI(TAG, "numbers=%d", (int)members.num);
        if (members.num > 0)
        {
            if (UsersRSSI == NULL)
                xTaskCreate(getUsersRSSI, "UsersRSSI", 4096, NULL, 10, &UsersRSSI);
            else
                vTaskResume(UsersRSSI);
        }
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        ESP_LOGI(TAG, "numbers=%d", (int)members.num);
        if (members.num == 0)
        {
            gpio_set_level(Led_Pin, 0);
            vTaskDelete(UsersRSSI);
        }
    }
}

void wifi_init_softap(void)
{
    size_t pass_size = 64;
    char content[64];

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Open
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");

    esp_err_t err = nvs_open("storage", NVS_READWRITE, &memory_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");
        // Read
        printf("Reading Password from NVS ... ");
        err = nvs_get_str(memory_handle, "Password", content, &pass_size);
        switch (err)
        {
        case ESP_OK:
            printf("Done\n");
            printf("This is your Password = %s\n", content);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The value is not initialized yet!\n");
            content[5] = '\0';
            break;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        // Close
        nvs_close(memory_handle);
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    printf("Error (%s) reading!\n", content);
    if (content[5] > 0){
        strncpy((char *)wifi_config.ap.password, content, sizeof(wifi_config.ap.password));
    }
    // if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
    //     wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    // }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (sizeof(content) > 0){
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, content, EXAMPLE_ESP_WIFI_CHANNEL);
    }else
    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

static void tp_example_set_thresholds(void)
{
    uint16_t touch_value;
    // read filtered value
    touch_pad_read_filtered(TOUCH_PAD_NUM3, &touch_value);
    s_pad_init_val = touch_value;
    ESP_LOGI(TAG, "test init: touch pad [%d] val is %d", TOUCH_PAD_NUM3, touch_value);
    // set interrupt threshold.
    ESP_ERROR_CHECK(touch_pad_set_thresh(TOUCH_PAD_NUM3, touch_value * 2 / 3));
}

static void tp_example_touch_pad_init(void)
{
    touch_pad_config(TOUCH_PAD_NUM3, 0);
}

static void tp_example_rtc_intr(void *arg)
{
    uint32_t pad_intr = touch_pad_get_status();
    // clear interrupt
    touch_pad_clear_status();
    if ((pad_intr >> TOUCH_PAD_NUM3) & 0x01)
    {
        s_pad_activated = true;
    }
}

/* GPIO15 = MTDO = TOUCH_PAD_NUM3 */

static void tp_example_read_task(void *pvParameter)
{
    while (1)
    {
        // interrupt mode, enable touch interrupt
        touch_pad_intr_enable();
        if (s_pad_activated == true)
        {
            ESP_LOGI(TAG, "T%d activated!", TOUCH_PAD_NUM3);
            // Wait a while for the pad being released
            vTaskDelay(200 / portTICK_PERIOD_MS);
            if (door)
            {
                ESP_LOGI(TAG, "Door is Open");
                turnServo(1);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                turnServo(0);
                ESP_LOGI(TAG, "Door is Closed");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            // Clear information on pad activation
            s_pad_activated = false;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void initTouch()
{
    // Initialize touch pad peripheral, it will start a timer to run a filter
    ESP_LOGI(TAG, "Initializing touch pad");
    ESP_ERROR_CHECK(touch_pad_init());
    // If use interrupt trigger mode, should set touch sensor FSM mode at 'TOUCH_FSM_MODE_TIMER'.
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    // Set reference voltage for charging/discharging
    // For most usage scenarios, we recommend using the following combination:
    // the high reference valtage will be 2.7V - 1V = 1.7V, The low reference voltage will be 0.5V.
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    // Init touch pad IO
    tp_example_touch_pad_init();
    // Initialize and start a software filter to detect slight change of capacitance.
    touch_pad_filter_start(1000);
    // Set thresh hold
    tp_example_set_thresholds();
    // Register touch interrupt ISR
    touch_pad_isr_register(tp_example_rtc_intr, NULL);
    // Start a task to show what pads have been touched
    xTaskCreate(&tp_example_read_task, "touch_pad_read_task", 4096, NULL, 5, NULL);
}

static esp_err_t change_password_get_handler(httpd_req_t *req)
{
    const char *resp_str = "<!DOCTYPE html>"
                           "<html lang=\"en\">"
                           "<head>"
                           "<meta charset=\"UTF-8\">"
                           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                           "<title>Bouzid WebServer</title>"
                           "<style>"
                           "body {"
                           "  font-family: Arial, sans-serif;"
                           "  background-color: #f4f4f4;"
                           "  justify-content: center;"
                           "  align-items: center;"
                           "  height: 100vh;"
                           "  margin: 0;"
                           "}"
                           "form {"
                           "  background-color: #fff;"
                           "  padding: 20px;"
                           "  border-radius: 10px;"
                           "  box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);"
                           "}"
                           "h1 {"
                           "  color: #333;"
                           "  text-align: center;"
                           "}"
                           "input[type='password'], input[type='text'],input[type='submit'] {"
                           "  width: 100%;"
                           "  padding: 10px;"
                           "  margin: 10px 0;"
                           "  border-radius: 5px;"
                           "  border: 1px solid #ccc;"
                           "  font-size: 16px;"
                           "}"
                           "input[type='submit'] {"
                           "  background-color: #4CAF50;"
                           "  color: white;"
                           "  border: none;"
                           "  cursor: pointer;"
                           "}"
                           "input[type='submit']:hover {"
                           "  background-color: #45a049;"
                           "}"
                           "button {"
                           "  padding: 10px;"
                           "  margin-top: 10px;"
                           "  background-color: #008CBA;"
                           "  color: white;"
                           "  border: none;"
                           "  border-radius: 5px;"
                           "  cursor: pointer;"
                           "}"
                           "button:hover {"
                           "  background-color: #007B9E;"
                           "}"
                           "#error-message {"
                           "  color: red;"
                           "  font-size: 14px;"
                           "  margin-top: 5px;"
                           "}"
                           "</style>"
                           "</head>"
                           "<body>"
                           "<h1>Bouzid AL-Bagdadi WebServer</h1>\n"
                           "<form method=\"POST\" action=\"/change-password\" onsubmit=\"return validatePassword()\">"
                           "<input type=\"password\" id=\"newPassword\" name=\"newPassword\" placeholder=\"Enter new password\" minlength=\"8\" maxlength=\"64\" required />"
                           "<input type=\"password\" id=\"confirmPassword\" placeholder=\"Confirm new password\" minlength=\"8\" maxlength=\"64\" required />"
                           "<button type=\"button\" onclick=\"togglePasswordVisibility()\">Show Password</button>"
                           "<p id=\"error-message\"></p>"
                           "<input type=\"submit\" value=\"Change Password\" />"
                           "</form>"
                           "<script>"
                           "function validatePassword() {"
                           "  var password = document.getElementById('newPassword').value;"
                           "  var confirmPassword = document.getElementById('confirmPassword').value;"
                           "  var errorMessage = document.getElementById('error-message');"
                        //    "  var regex = /^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[@$!%*?&])[A-Za-z\\d@$!%*?&]{8,}$/;"
                        //    "  if (!regex.test(password)) {"
                        //    "    errorMessage.innerText = 'Password must be at least 8 characters, with one uppercase, one lowercase, one number, and one special character.';"
                        //    "    return false;"
                        //    "  }"
                           "  if (password !== confirmPassword) {"
                           "    errorMessage.innerText = 'Passwords do not match.';"
                           "    return false;"
                           "  }"
                           "  return true;"
                           "}"
                           "function togglePasswordVisibility() {"
                           "  var passwordField = document.getElementById('newPassword');"
                           "  var confirmPasswordField = document.getElementById('confirmPassword');"
                           "  if (passwordField.type === 'password') {"
                           "    passwordField.type = 'text';"
                           "    confirmPasswordField.type = 'text';"
                           "  } else {"
                           "    passwordField.type = 'password';"
                           "    confirmPasswordField.type = 'password';"
                           "  }"
                           "}"
                           "</script>"
                           "</body>"
                           "</html>";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

static esp_err_t change_password_post_handler(httpd_req_t *req)
{
    char content[100];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0)
    { // Check for errors
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req); // Timeout
        }
        return ESP_FAIL;
    }

    content[recv_size] = '\0'; // Null-terminate the received string

    // Log the new password
    ESP_LOGI(TAG, "Received new password: %s", content + 12);

    // Open
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &memory_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");
        // Write
        printf("Updating Password counter in NVS ... ");
        err = nvs_set_str(memory_handle, "Password", content + 12);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        printf("Committing updates in NVS ... ");
        err = nvs_commit(memory_handle);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

        // Close
        nvs_close(memory_handle);
    }
    const char *resp_str = "<!DOCTYPE html>"
                           "<html lang=\"en\">"
                           "<head>"
                           "<meta charset=\"UTF-8\">"
                           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
                           "<title>Password Changed</title>"
                           "<style>"
                           "body {"
                           "  font-family: Arial, sans-serif;"
                           "  background-color: #f4f4f4;"
                           "  display: flex;"
                           "  justify-content: center;"
                           "  align-items: center;"
                           "  height: 100vh;"
                           "  margin: 0;"
                           "}"
                           ".container {"
                           "  background-color: #fff;"
                           "  padding: 30px;"
                           "  border-radius: 10px;"
                           "  box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);"
                           "  text-align: center;"
                           "  max-width: 400px;"
                           "}"
                           "h1 {"
                           "  color: #4CAF50;"
                           "}"
                           "p {"
                           "  font-size: 18px;"
                           "  color: #333;"
                           "}"
                           "button {"
                           "  padding: 10px 20px;"
                           "  background-color: #4CAF50;"
                           "  color: white;"
                           "  border: none;"
                           "  border-radius: 5px;"
                           "  cursor: pointer;"
                           "  font-size: 16px;"
                           "}"
                           "button:hover {"
                           "  background-color: #45a049;"
                           "}"
                           "</style>"
                           "</head>"
                           "<body>"
                           "<div class=\"container\">"
                           "<h1>Success!</h1>"
                           "<p>Your password has been successfully changed.</p>"
                           "</div>"
                           "</body>"
                           "</html>";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t hello_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = change_password_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &hello_uri);

        httpd_uri_t change_password_uri = {
            .uri = "/change-password",
            .method = HTTP_POST,
            .handler = change_password_post_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &change_password_uri);
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    const TickType_t delay_in_ms = pdMS_TO_TICKS(6 * 3600 * 1000); // 6 hours in milliseconds
    wifi_init_softap();
    start_webserver();
    gpio_set_direction(Led_Pin, GPIO_MODE_OUTPUT);
    configInterrupter();
    initServo();
    initTouch();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(delay_in_ms));
        esp_restart();
    }
}
