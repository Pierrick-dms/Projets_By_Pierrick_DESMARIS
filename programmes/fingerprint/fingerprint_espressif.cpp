#include <cstdio>
#include <driver/uart.h>
#include <esp_log.h>
#include <cstring>

#define UART_PORT     UART_NUM_2
#define UART_BAUDRATE     57600
#define UART_TX_PIN        17
#define UART_RX_PIN        16
#define BUF_SIZE   1024

static const char *TAG="FINGERPRINT";


// Envoie une trame de commande au capteur
void send_fingerprint_command(uint8_t instruction, uint8_t *data, uint16_t data_len) {
    uint16_t length = data_len + 3; // données + code instruction (1) + checksum (2)

    uint8_t packet[64];
    int idx = 0;

    packet[idx++] = 0xEF; packet[idx++] = 0x01;             // En-tête
    packet[idx++] = 0xFF; packet[idx++] = 0xFF;             // Adresse
    packet[idx++] = 0xFF; packet[idx++] = 0xFF;
    packet[idx++] = 0x01;                                    // ID paquet (commande)
    packet[idx++] = (length >> 8) & 0xFF;                    // Longueur (octet fort)
    packet[idx++] = length & 0xFF;                           // Longueur (octet faible)
    packet[idx++] = instruction;                             // Code instruction

    uint16_t checksum = 0x01 + ((length >> 8) & 0xFF) + (length & 0xFF) + instruction;

    for (int i = 0; i < data_len; i++) {
        packet[idx++] = data[i];
        checksum += data[i];
    }

    packet[idx++] = (checksum >> 8) & 0xFF;
    packet[idx++] = checksum & 0xFF;

    uart_write_bytes(UART_PORT, (const char *)packet, idx);
}

// Lit la réponse du capteur
int read_fingerprint_response(void) {
    uint8_t response[32];
    int len = uart_read_bytes(UART_PORT, response, sizeof(response), pdMS_TO_TICKS(2000));

    if (len > 10) {
        return response[9]; // code de confirmation (index 9 dans la trame de réponse)
    }
    ESP_LOGW(TAG, "Aucune reponse du capteur (len+%d)", len);
    return -1;
    
}

void enroll_fingerprint(uint8_t id) {
    uint8_t no_data[0];

    // Etape 1 : première capture

    ESP_LOGI(TAG,"Posez votre doigt...");
    int result = -1;
    while (result !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result = read_fingerprint_response();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG,"Doigt détecté...");

    send_fingerprint_command(0x02, (uint8_t[]){0x01}, 1); // 0x02 = Img2Tz (convertir image en template)
    read_fingerprint_response();
    ESP_LOGI(TAG,"Image 1 convertie en template...");


    // Tempo pour retirer le doigt
    ESP_LOGI(TAG,"Retirez votre doigt...");
    vTaskDelay(pdMS_TO_TICKS(2000));


    // Etape 2 : deuxième capture
    ESP_LOGI(TAG,"Posez de nouveau votre doigt...");
    result = -1;
    while (result !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result = read_fingerprint_response();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG,"Doigt détecté...");

    send_fingerprint_command(0x02, (uint8_t[]){0x02}, 1); // 0x02 = Img2Tz (convertir image en template)
    read_fingerprint_response();
    ESP_LOGI(TAG,"Image 2 convertie en template...");


    // Etape 3 : fusion des templates
    send_fingerprint_command(0x05, no_data, 0); // 0x05 = RegModel (fusionner les templates)
    int reg_result = read_fingerprint_response();
    if (reg_result != 0) {
        ESP_LOGE(TAG,"Erreur lors de la fusion des templates: %d", reg_result);
        return;
    }
    ESP_LOGI(TAG,"Templates fusionnés avec succès...");


    // Etape 4 : sauvegarde en mémoire à l'empplacement "id"
    uint8_t store_data[3] = {0x01, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)}; // 0x01 = ID de l'empreinte
    send_fingerprint_command(0x06, store_data, 3); // Store
    int store_result = read_fingerprint_response();

    if (store_result != 0) {
        ESP_LOGE(TAG,"Erreur lors de la sauvegarde de l'empreinte: %d", store_result);
        return;
    }
    ESP_LOGI(TAG,"Empreinte sauvegardée avec succès à l'ID: %d", id);
}

bool check_fingerprint(void) {
    uint8_t no_data[0];

    // Capture de l'image
    int result2 = -1;
    while (result2 !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result2 = read_fingerprint_response();
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    // Convertir l'image en template
    send_fingerprint_command(0x02, (uint8_t[]){0x01}, 1);
    int convert_result = read_fingerprint_response();
    if (convert_result != 0) {
        ESP_LOGE(TAG,"Erreur lors de la conversion de l'image en template (code: %d)", convert_result);
        return false;
    }

    // Rechercher l'empreinte dans la base de données
    uint8_t search_data[5] = {0x01, 0x00, 0x00, 0x00, 0xC8}; // ID de l'empreinte à rechercher, position de départ et nombre d'empreintes à rechercher
    send_fingerprint_command(0x04, search_data, 5); // 0x04 = Search
    
    uint8_t response[32];
    int len = uart_read_bytes(UART_PORT, response, sizeof(response), pdMS_TO_TICKS(2000));
    
    if (len >= 10) {
        int confirmation = response[9];
        if (confirmation == 0) {
            int matched_id = (response[10] << 8) | response[11];
            ESP_LOGI(TAG,"Empreinte trouvée à l'ID: %d", matched_id);
            return true;
        } else {
            ESP_LOGW(TAG,"Aucune empreinte correspondante trouvée (code: %d)", confirmation);
            return false;
        }
    }

    return false;
}

void init_uart_fingerprint(void)
{
    // Étape 1 : définir la configuration (baudrate, format des données)
    uart_config_t uart_config {};
        uart_config.baud_rate = UART_BAUDRATE;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity    = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.rx_flow_ctrl_thresh = 0;
        uart_config.source_clk = UART_SCLK_DEFAULT;
    

    // Étape 2 : appliquer cette config au port UART choisi
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));

    // Étape 3 : assigner les broches physiques (TX, RX)
    // On ne branche pas les signaux RTS/CTS, donc UART_PIN_NO_CHANGE pour ces deux
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Étape 4 : installer le driver (alloue les buffers de réception/émission)
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART initialise pour le capteur d'empreinte");
}

extern "C" void app_main(void){
    init_uart_fingerprint();
    vTaskDelay(pdMS_TO_TICKS(500)); // Attendre 0,5 secondes pour que le capteur soit prêt

    ESP_LOGI(TAG, "Système prêt, posez votre doigt...");

    while (true) {
        bool success = check_fingerprint();
        if (success) {
            ESP_LOGI(TAG, "Empreinte reconnue !");
        } else {
            ESP_LOGI(TAG, "Empreinte non reconnue.");
            ESP_LOGW(TAG,"Reposez votre doigt...");

        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Attendre 1 seconde avant la prochaine itération
    }
}
 
