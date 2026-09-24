#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // Set the LCD I2C address and dimensions
HardwareSerial Fingerprint(2); // Use UART2 for the fingerprint sensor

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

    Fingerprint.write(packet, idx);
}


int read_fingerprint_response() {
      uint8_t response[32];
    int len = Fingerprint.readBytes(response, sizeof(response));

    if (len > 10) {
        return response[9]; // code de confirmation (index 9 dans la trame de réponse)
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Aucune reponse ");
    lcd.setCursor(0, 1);
    lcd.print("du capteur (len=");
    lcd.print(len);
    lcd.print(")");
    return -1;

}
void enroll_fingerprint(uint8_t id) {
  uint8_t no_data[0];

    // Etape 1 : première capture

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Posez votre ");
    lcd.setCursor(0, 1);
    lcd.print("doigt...");
    int result = -1;
    while (result !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result = read_fingerprint_response();
        delay(300);
    }
    lcd.setCursor(0, 1);
    lcd.print("Doigt détecté...");

    uint8_t template_1[1] = {0x01};
    send_fingerprint_command(0x02, template_1, 1); // 0x02 = Img2Tz (convertir image en template)
    read_fingerprint_response();
    lcd.setCursor(0, 0);
    lcd.print("Image1 convertie");
    lcd.setCursor(0, 1);
    lcd.print("en template...");


    // Tempo pour retirer le doigt
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Retirez votre ");
    lcd.setCursor(0, 1);
    lcd.print("doigt...");
    delay(2000);


    // Etape 2 : deuxième capture
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Posez de encore ");
    lcd.setCursor(0, 1);
    lcd.print("votre doigt...");
    result = -1;
    while (result !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result = read_fingerprint_response();
        delay(300);
    }
    lcd.setCursor(0, 1);
    lcd.print("Doigt détecté...");

    uint8_t template_2[1] = {0x02};
    send_fingerprint_command(0x02, template_2, 1); // 0x02 = Img2Tz (convertir image en template)
    read_fingerprint_response();
    lcd.setCursor(0, 0);
    lcd.print("Image 2 convertie ");
    lcd.setCursor(0, 1);
    lcd.print("en template...");


    // Etape 3 : fusion des templates
    send_fingerprint_command(0x05, no_data, 0); // 0x05 = RegModel (fusionner les templates)
    int reg_result = read_fingerprint_response();
    if (reg_result != 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Erreur fusion ");
        lcd.setCursor(0, 1);
        lcd.print("des templates: ");
        lcd.print(reg_result);
        return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Templates fusion");
    lcd.setCursor(0, 1);
    lcd.print("OK...");


    // Etape 4 : sauvegarde en mémoire à l'empplacement "id"
    uint8_t store_data[3] = {0x01, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF)}; // 0x01 = ID de l'empreinte
    send_fingerprint_command(0x06, store_data, 3); // Store
    int store_result = read_fingerprint_response();

    if (store_result != 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Erreur saved ");
        lcd.setCursor(0, 1);
        lcd.print("empreinte: ");
        lcd.print(store_result);
        return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Empreinte saved ");
    lcd.setCursor(0, 1); 
    lcd.print("with ID: ");
    lcd.print(id);
    return;
}


int check_fingerprint() {
   uint8_t no_data[0];

    // Capture de l'image
    int result2 = -1;
    while (result2 !=0) {
        send_fingerprint_command(0x01, no_data, 0); // 0x01 = GenImg (générer image)
        result2 = read_fingerprint_response();
        delay(300);
    }

    // Convertir l'image en template
    uint8_t template_1[1] = {0x01};
    send_fingerprint_command(0x02, template_1, 1);
    int convert_result = read_fingerprint_response();
    if (convert_result != 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Erreur convers(code: ");
        lcd.setCursor(0, 1);
        lcd.print("(code: ");
        lcd.print(convert_result);
        lcd.print(")");
        return false;
    }

    // Rechercher l'empreinte dans la base de données
    uint8_t search_data[5] = {0x01, 0x00, 0x00, 0x00, 0xC8}; // ID de l'empreinte à rechercher, position de départ et nombre d'empreintes à rechercher
    send_fingerprint_command(0x04, search_data, 5); // 0x04 = Search
    
    uint8_t response[32];
    int len = Fingerprint.readBytes(response, sizeof(response));
    
    if (len >= 10) {
        int confirmation = response[9];
        if (confirmation == 0) {
            int matched_id = (response[10] << 8) | response[11];
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Empreinte ID: ");
            lcd.print(matched_id);
            return true;
        } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("no print code: ");
            lcd.setCursor(0, 1);
            lcd.print(confirmation);
            lcd.print(")");
            return false;
        }
    }

    return false;
}

void setup() {
  Serial.begin(115200); // Initialize Serial Monitor for debugging

  Wire.begin(21, 22); // Initialize I2C with SDA on GPIO21 and SCL on GPIO22

  Fingerprint.begin(57600, SERIAL_8N1, 16, 17); // Initialize UART2 with RX on GPIO16 and TX on GPIO17
  Fingerprint.setTimeout(2000);

  lcd.init(); // Initialize the LCD
  lcd.backlight(); // Turn on the backlight

  lcd.setCursor(0, 0);
  lcd.print("System ready...");

}

void loop() {

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Système prêt");
    lcd.setCursor(0, 1);
    lcd.print("posez le doigt");

    while (true) {
        bool success = check_fingerprint();
        if (success) {
            lcd.setCursor(0, 0);
            lcd.print("Empreinte reconnue !");
        } else {
            lcd.setCursor(0, 0);
            lcd.print("Empreinte non");
            lcd.setCursor(0, 1);
            lcd.print("reconnue.");
            delay(1000); // Attendre 1 seconde avant la prochaine itération

            lcd.setCursor(0, 0);
            lcd.print("Reposez votre doigt...");

        }
        delay(1000); // Attendre 1 seconde avant la prochaine itération
    }

}
