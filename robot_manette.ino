// Pilotage différentiel d'un robot avec une manette Xbox (Bluepad32) -> commandes moteurs en I2C
// Carte à choisir dans l'IDE : "ESP32 + Bluepad32 Arduino" > votre modèle
// Dossier du croquis à nommer "robot_manette" (même nom que ce fichier).

#include <Arduino.h>
#include <Wire.h>
#include <Bluepad32.h>

// ================= RÉGLAGES =================
// Attention : l'ESP32-S3 n'a pas de GPIO22. Sur S3, utilisez par exemple SDA=8, SCL=9.
#define PIN_SDA 21
#define PIN_SCL 22
#define ADRESSE_I2C 0x10          // À ADAPTER : adresse de la carte d'asservissement
#define FREQ_I2C 100000

#define PERIODE_MS 50             // boucle de commande à 20 Hz
const float ZONE_MORTE = 0.08f;   // fraction de course ignorée au centre
const float EXPO = 0.4f;          // 0 = linéaire, 1 = très progressif
const float RAMPE_PAR_S = 3.0f;   // variation max de vitesse par seconde
const float FACTEUR_LENT = 0.4f;  // vitesse max en mode lent (maintien de LB)
// ============================================

ControllerPtr manette = nullptr;

float gauche = 0.0f, droite = 0.0f;
bool urgence = false;
bool bPrecedent = false;
int dernierG = 1000, dernierD = 1000;
unsigned long prochain = 0;

float borner(float v, float mini = -1.0f, float maxi = 1.0f) {
  return max(mini, min(maxi, v));
}

// Axe brut (-511..512) -> -1.0..1.0 avec zone morte et courbe expo
float normaliser(int valeur) {
  float v = borner(valeur / 512.0f);
  if (fabsf(v) < ZONE_MORTE) return 0.0f;
  float signe = (v > 0) ? 1.0f : -1.0f;
  float a = (fabsf(v) - ZONE_MORTE) / (1.0f - ZONE_MORTE);
  return signe * ((1.0f - EXPO) * a + EXPO * a * a * a);
}

void calculerVitesses(int joyAvance, int joyDirection, float &g, float &d) {
  float y = -normaliser(joyAvance);   // haut = valeur négative -> avancer
  float x = normaliser(joyDirection);
  g = y + x;
  d = y - x;
  float maxi = max(max(fabsf(g), fabsf(d)), 1.0f);  // réduction proportionnelle
  g /= maxi;
  d /= maxi;
}

float rampe(float actuel, float cible, float pasMax) {
  return actuel + borner(cible - actuel, -pasMax, pasMax);
}

// Trame : [0xAA, gauche (int8), droite (int8), checksum] -- À adapter à votre carte
bool envoyerMoteurs(int8_t pctG, int8_t pctD) {
  uint8_t g = (uint8_t)pctG, d = (uint8_t)pctD;
  uint8_t checksum = (uint8_t)(0xAA + g + d);
  Wire.beginTransmission(ADRESSE_I2C);
  Wire.write((uint8_t)0xAA);
  Wire.write(g);
  Wire.write(d);
  Wire.write(checksum);
  return Wire.endTransmission() == 0;
}

void onConnected(ControllerPtr ctl) {
  if (manette == nullptr) {
    Serial.println("Manette connectée");
    manette = ctl;
  }
}

void onDisconnected(ControllerPtr ctl) {
  if (manette == ctl) {
    Serial.println("Manette déconnectée : arrêt des moteurs");
    manette = nullptr;
    gauche = droite = 0.0f;
    envoyerMoteurs(0, 0);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL, FREQ_I2C);
  BP32.setup(&onConnected, &onDisconnected);
  // BP32.forgetBluetoothKeys();  // à décommenter pour oublier les appairages (tests)
  envoyerMoteurs(0, 0);
  prochain = millis();
  Serial.println("En attente de connexion... (maintenez le bouton d'appairage de la manette)");
}

void loop() {
  BP32.update();   // à appeler le plus souvent possible

  if ((long)(millis() - prochain) < 0) return;
  prochain += PERIODE_MS;
  if ((long)(millis() - prochain) > PERIODE_MS) prochain = millis();  // on a pris du retard

  const float pasMax = RAMPE_PAR_S * PERIODE_MS / 1000.0f;

  if (manette == nullptr || !manette->isConnected()) {
    // Manette perdue : arrêt immédiat, sans rampe
    gauche = droite = 0.0f;
  } else {
    bool boutonB = manette->b();
    if (boutonB && !bPrecedent) {   // bascule sur front montant
      urgence = !urgence;
      Serial.println(urgence ? "ARRET D'URGENCE ACTIVE" : "Arret d'urgence leve");
    }
    bPrecedent = boutonB;

    if (urgence) {
      gauche = droite = 0.0f;
    } else {
      float cibleG, cibleD;
      calculerVitesses(manette->axisY(), manette->axisRX(), cibleG, cibleD);
      if (manette->l1()) {          // mode lent
        cibleG *= FACTEUR_LENT;
        cibleD *= FACTEUR_LENT;
      }
      gauche = rampe(gauche, cibleG, pasMax);
      droite = rampe(droite, cibleD, pasMax);
    }
  }

  int pctG = (int)lroundf(gauche * 100.0f);
  int pctD = (int)lroundf(droite * 100.0f);

  if (!envoyerMoteurs((int8_t)pctG, (int8_t)pctD)) {
    Serial.println("Erreur I2C");
  }

  if (pctG != dernierG || pctD != dernierD) {
    Serial.printf("Moteur G: %4d%% | Moteur D: %4d%%\n", pctG, pctD);
    dernierG = pctG;
    dernierD = pctD;
  }

  delay(1);
}
